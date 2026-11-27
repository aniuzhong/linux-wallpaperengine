#include "CObject.h"

#include <utility>

#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#undef GLM_ENABLE_EXPERIMENTAL

#include "WallpaperEngine/Data/Model/Object.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;
using namespace WallpaperEngine::Render::Wallpapers;

CObject::CObject (Wallpapers::CScene& scene, const Object& object) :
    Helpers::ContextAware (scene), m_scene (scene), m_object (object) {
    this->registerAnimations ({ this->m_object.origin.get () });
}

void CObject::setup () { }
void CObject::render () { }

bool CObject::isVisibleInTree () const {
    const CObject* current = this;

    for (int depth = 0; current != nullptr && depth < kMaxParentDepth; ++depth) {
	if (!current->isVisible ()) {
	    return false;
	}

	if (!current->m_object.parent.has_value ()) {
	    return true;
	}

	current = this->m_scene.getObject (current->m_object.parent.value ());
    }

    return true;
}

void CObject::registerAnimations (const std::initializer_list<UserSetting*> settings) {
    // Dynamic layers have a negative id and their data is owned by
    // m_dynamicObjects. They are skipped until AnimationRuntime can unregister
    // entries when the layer is destroyed.
    if (this->m_object.id <= 0) {
	return;
    }

    AnimationRuntime& runtime = this->m_scene.getAnimationRuntime ();

    for (UserSetting* setting : settings) {
	if (setting != nullptr) {
	    runtime.registerSetting (*setting);
	}
    }
}

void CObject::registerShaderConstants (const ShaderConstantMap& constants) {
    if (this->m_object.id > 0) {
	this->m_scene.getAnimationRuntime ().registerSettings (constants);
    }
}

void CObject::registerMaterial (const Material& material) {
    for (const auto& pass : material.passes) {
	this->registerShaderConstants (pass->constants);
    }
}

Wallpapers::CScene& CObject::getScene () const { return this->m_scene; }

const AssetLocator& CObject::getAssetLocator () const { return this->getScene ().getAssetLocator (); }

int CObject::getId () const { return this->m_object.id; }

const Object& CObject::getObject () const { return this->m_object; }

CObject::LocalTransform CObject::localTransform (const Object& object) {
    LocalTransform transform = {
	.origin = object.origin->value->getVec3 (),
	.scale = object.groupScale->value->getVec3 (),
	.angles = object.groupAngles->value->getVec3 (),
    };

    if (object.is<Image> ()) {
	const auto* image = object.as<Image> ();
	transform.scale = image->scale->value->getVec3 ();
	transform.angles = image->angles->value->getVec3 ();
    } else if (object.is<Particle> ()) {
	const auto* particle = object.as<Particle> ();
	transform.scale = particle->scale->value->getVec3 ();
	transform.angles = particle->angles->value->getVec3 ();
    } else if (object.is<Text> ()) {
	const auto* text = object.as<Text> ();
	transform.scale = text->scale->value->getVec3 ();
    }

    return transform;
}

CObject::ParentChain CObject::collectParentChain (const Object& object) const {
    ParentChain chain = {};
    const Object* current = &object;

    chain.objects[chain.count++] = current;

    while (current->parent.has_value ()) {
	if (chain.count > kMaxParentDepth) {
	    sLog.error ("Parent chain is too deep; possible cycle at object id=", current->id);
	    break;
	}

	const auto* parentObject = this->getScene ().getObject (current->parent.value ());

	if (parentObject == nullptr) {
	    break;
	}

	current = &parentObject->getObject ();
	chain.objects[chain.count++] = current;
    }

    return chain;
}

CObject::ResolvedTransform CObject::resolveTransform (const Object& object) const {
    // The 2D compositor and CText consume only the Z angle. Text layers stay
    // upright in that path, so keep that behaviour explicit here.
    const auto localZAngle = [] (const Object& source, const LocalTransform& transform) {
	return source.is<Text> () ? 0.0f : transform.angles.z;
    };

    const ParentChain chain = this->collectParentChain (object);

    // Accumulate top-down: the root's local transform is already its resolved
    // transform, then fold each child onto its already-resolved parent.
    const Object* rootObject = chain.objects[chain.count - 1];
    const LocalTransform root = localTransform (*rootObject);
    ResolvedTransform resolved = { root.origin, root.scale, localZAngle (*rootObject, root) };

    for (int i = static_cast<int> (chain.count) - 2; i >= 0; --i) {
	const Object* currentObject = chain.objects[i];
	const LocalTransform local = localTransform (*currentObject);
	const float angle = localZAngle (*currentObject, local);
	const glm::vec2 offset = glm::rotate (
	    glm::vec2 { local.origin.x * resolved.scale.x, local.origin.y * resolved.scale.y }, resolved.angle
	);

	const glm::vec3 origin = {
	    resolved.origin.x + offset.x,
	    resolved.origin.y + offset.y,
	    resolved.origin.z + local.origin.z * resolved.scale.z,
	};

	resolved = { origin, local.scale * resolved.scale, angle + resolved.angle };
    }

    return resolved;
}

glm::mat4 CObject::localToParentMatrix (const LocalTransform& transform) {
    glm::mat4 matrix = glm::translate (glm::mat4 (1.0f), transform.origin);

    // Scene angles are authored as Rz * Ry * Rx in the same convention as the
    // camera parser. Do not negate X/Z here: doing so flips the orientation of
    // models whose parent chain carries rotations (e.g. Saturn's ring plane).
    matrix = glm::rotate (matrix, transform.angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
    matrix = glm::rotate (matrix, transform.angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
    matrix = glm::rotate (matrix, transform.angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));
    matrix = glm::scale (matrix, transform.scale);

    return matrix;
}

glm::mat4 CObject::resolveWorldMatrix (const Object& object) const {
    const ParentChain chain = this->collectParentChain (object);

    glm::mat4 world = glm::mat4 (1.0f);

    for (int i = static_cast<int> (chain.count) - 1; i >= 0; --i) {
	world = world * localToParentMatrix (localTransform (*chain.objects[i]));
    }

    return world;
}
