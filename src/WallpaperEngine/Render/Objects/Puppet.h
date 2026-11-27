#pragma once

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "WallpaperEngine/Data/Model/ModelFile.h"
#include "WallpaperEngine/Data/Model/Puppet.h"

namespace WallpaperEngine::Render::Objects::Puppet {
using Rig = WallpaperEngine::Data::Model::Puppet::Rig;
using Keyframe = WallpaperEngine::Data::Model::Puppet::Keyframe;
using AnimationClip = WallpaperEngine::Data::Model::Puppet::AnimationClip;
using Controller = WallpaperEngine::Data::Model::Puppet::Controller;
using ModelFile = WallpaperEngine::Data::Model::ModelFile;

struct AnimationLayer {
    uint32_t animation_id = 0;
    float rate = 1.0f;
    float blend = 1.0f;
    bool visible = true;
    /** Seconds on this layer's own clock (already scaled by rate). */
    float time = 0.0f;
};

/**
 * Full xy footprint of the mesh in model coordinates: the rest-pose bounding
 * box unioned with the skinned bounds of every animation clip (sampled at up
 * to 256 frames per clip). For puppets the rest pose is the first frame of the
 * first clip; the raw atlas vertex positions are not used because they do not
 * represent the assembled character.
 * Returns (minX, minY, maxX, maxY); (0, 0, 0, 0) when the model has no mesh.
 */
glm::vec4 computeBounds (const ModelFile& model);

/**
 * Advances the given animation layers by dt (rate applied per layer) and
 * writes the skinned xyz positions for every vertex of the model. Vertices are
 * ordered by section. When no layer matches a clip the model falls back to its
 * rest pose (the first frame of the first clip, or the bind pose when there is
 * no animation).
 */
class Pose {
public:
    void init (const ModelFile& model);
    void advance (const Rig& rig, std::vector<AnimationLayer>& layers, float dt) const;
    void skin (const ModelFile& model, const std::vector<AnimationLayer>& layers, std::vector<float>& out) const;

private:
    std::vector<glm::mat4>
    computeBoneMatrices (const ModelFile& model, const std::vector<AnimationLayer>& layers) const;

    std::vector<glm::mat4> inverse_bind_world;
    std::vector<uint32_t> bone_order;
};
} // namespace WallpaperEngine::Render::Objects::Puppet
