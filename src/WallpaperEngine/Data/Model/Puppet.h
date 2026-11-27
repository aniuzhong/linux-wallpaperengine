#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WallpaperEngine::Data::Model::Puppet {
struct Bone {
    uint32_t parent = 0xFFFFFFFFu;
    /** Row-major 4x4 bind transform, translation in row 3. */
    float bind_matrix[16] = {};
    std::string name;
    uint32_t ik_depth = 0;
};

/**
 * Local bone transform sampled from an animation track. Rotations are Euler
 * angles in radians, applied as Rz * Ry * Rx.
 */
struct Keyframe {
    float translation[3] = { 0, 0, 0 };
    float rotation[3] = { 0, 0, 0 };
    float scale[3] = { 1, 1, 1 };
};

struct AnimationClip {
    uint32_t id = 0;
    std::string name;
    std::string loop_mode;
    float fps = 0.0f;
    uint32_t frame_count = 0;
    /**
     * One track per bone in bone order, followed by one track per controller.
     * Each track holds frame_count + 1 keyframes.
     */
    std::vector<std::vector<Keyframe>> tracks;
};

/** An IK control track appended after the bone tracks of an animation clip. */
struct Controller {
    uint32_t bone_index = 0;
    bool pole = false;
};

/**
 * Skeleton and animation data stored after the mesh sections of a puppet .mdl.
 * The mesh itself lives in ModelFile so both static and puppet models share the
 * same section parser.
 */
struct Rig {
    std::vector<Bone> bones;
    std::vector<Controller> controllers;
    std::vector<AnimationClip> clips;
};
} // namespace WallpaperEngine::Data::Model::Puppet
