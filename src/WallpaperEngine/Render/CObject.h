#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>

#include <glm/glm.hpp>

#include "WallpaperEngine/Render/Helpers/ContextAware.h"

#include "WallpaperEngine/Render/Wallpapers/CScene.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render {
class CObject : public Helpers::ContextAware, public TypeCaster {
public:
    CObject (Wallpapers::CScene& scene, const Object& object);
    virtual ~CObject () override = default;

    virtual void setup ();
    virtual void render ();

    /** Whether this object is visible on its own (ignoring its parents). */
    [[nodiscard]] virtual bool isVisible () const {
	return this->m_object.groupVisible == nullptr || this->m_object.groupVisible->value->getBool ();
    }
    /** Whether this object and every object in its parent chain are visible. */
    [[nodiscard]] bool isVisibleInTree () const;

    /** Hit test against the cursor position in normalized viewport coordinates (top-left origin).
     *  When provided, localPosition receives the cursor position in object-local coordinates. */
    [[nodiscard]] virtual bool hitTest (const glm::vec2& normalizedMouse, glm::vec2* localPosition = nullptr) const {
	return false;
    }

    [[nodiscard]] Wallpapers::CScene& getScene () const;
    [[nodiscard]] const AssetLocator& getAssetLocator () const;
    [[nodiscard]] int getId () const;
    [[nodiscard]] const Object& getObject () const;

protected:
    /**
     * Registers timeline animations for this object's settings.
     * Dynamic layers use negative ids and are skipped for now: their backing
     * data is destroyed with the layer, so they need an explicit unregister
     * path before they can participate.
     */
    void registerAnimations (std::initializer_list<UserSetting*> settings);

    /** Registers every timeline animation in a shader constant map. */
    void registerShaderConstants (const ShaderConstantMap& constants);

    /** Registers every timeline animation in a material's passes. */
    void registerMaterial (const Material& material);

    /**
     * Composed origin/scale/angle after folding the object's full parent chain
     * on top of its own local transform.
     */
    struct ResolvedTransform {
	glm::vec3 origin;
	glm::vec3 scale;
	float angle;
    };

    /**
     * The object's own transform, expressed in its parent's space. `angles`
     * keeps all three axes; the 2D compositor consumes only Z.
     */
    struct LocalTransform {
	glm::vec3 origin;
	glm::vec3 scale;
	glm::vec3 angles;
    };

    /**
     * Folds the object's parent chain (bounded, cycle-guarded) into a single
     * origin/scale/angle. Parents are created before their children
     * (CScene::createObject), so every link resolves here.
     */
    [[nodiscard]] ResolvedTransform resolveTransform (const Object& object) const;

    /**
     * Folds the full parent chain into a 4x4 local-to-world matrix. Unlike
     * resolveTransform (which keeps a single Z angle for the 2D compositor),
     * this keeps all three rotation axes so 3D children of rotated groups get
     * the correct world transform.
     */
    [[nodiscard]] glm::mat4 resolveWorldMatrix (const Object& object) const;

    /**
     * Computes the object's own transform without walking the parent chain.
     * Typed objects (image/particle/text) keep their own scale/angle fields;
     * generic objects use the group fields on ObjectData.
     */
    [[nodiscard]] static LocalTransform localTransform (const Object& object);

    /** Builds a local-to-parent matrix from a resolved local transform. */
    [[nodiscard]] static glm::mat4 localMatrix (const LocalTransform& transform);

private:
    static constexpr int kMaxParentDepth = 32;

    /**
     * The object followed by its parents, leaf-first. Bounded by
     * kMaxParentDepth so a malformed scene cannot loop forever.
     */
    struct ParentChain {
	std::array<const Object*, kMaxParentDepth + 1> objects = {};
	std::size_t count = 0;
    };

    [[nodiscard]] ParentChain collectParentChain (const Object& object) const;

    Wallpapers::CScene& m_scene;
    const Object& m_object;
};
} // namespace WallpaperEngine::Render