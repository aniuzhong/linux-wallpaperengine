#pragma once

#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Scripting/ScriptableObject.h"

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
/**
 * Scene layer without a renderer of its own. Group/container objects, lights
 * and cameras still need a real CObject in the scene graph so that:
 *  - their origin/scale/angles/visible scripts run every frame,
 *  - `thisScene.getLayer(id|name)` can return them,
 *  - the values scripted here stay visible to the systems that read the
 *    authored settings (CScene::updateLights, resolveTransform, ...).
 *
 * Camera pose/fov are snapshotted by the parser before scripts run; this class
 * exposes the camera layer but does not drive the scene camera.
 */
class CScriptHost final : public Scripting::ScriptableObject {
public:
    CScriptHost (Wallpapers::CScene& scene, const Data::Model::Object& object);
};
} // namespace WallpaperEngine::Render::Objects
