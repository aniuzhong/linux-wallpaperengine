#include "Puppet.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>

using namespace WallpaperEngine::Data::Model::Puppet;
using WallpaperEngine::Data::Model::MeshSection;
using WallpaperEngine::Data::Model::MeshVertexLayout;
using WallpaperEngine::Data::Model::ModelFile;

namespace WallpaperEngine::Render::Objects::Puppet {
namespace {
    const Rig* getRig (const ModelFile& model) { return model.puppet ? &*model.puppet : nullptr; }

    glm::mat4 bindLocal (const Bone& bone) {
	// The stored bind matrix is row-major with row-vector semantics. Copying
	// its bytes into a column-major matrix transposes it, which is the
	// representation the rest of the math expects.
	return glm::make_mat4 (bone.bind_matrix);
    }

    glm::mat4 composeLocal (const Keyframe& pose) {
	glm::mat4 out = glm::translate (
	    glm::mat4 (1.0f), glm::vec3 (pose.translation[0], pose.translation[1], pose.translation[2])
	);

	out = glm::rotate (out, pose.rotation[2], glm::vec3 (0.0f, 0.0f, 1.0f));
	out = glm::rotate (out, pose.rotation[1], glm::vec3 (0.0f, 1.0f, 0.0f));
	out = glm::rotate (out, pose.rotation[0], glm::vec3 (1.0f, 0.0f, 0.0f));
	out = glm::scale (out, glm::vec3 (pose.scale[0], pose.scale[1], pose.scale[2]));
	return out;
    }

    Keyframe keyframeFromBind (const Bone& bone) {
	const glm::mat4 bind = bindLocal (bone);
	Keyframe out;

	for (int axis = 0; axis < 3; ++axis) {
	    out.translation[axis] = bind[3][axis];
	    out.scale[axis] = glm::length (glm::vec3 (bind[axis]));
	}

	out.rotation[2] = std::atan2 (bind[0][1], bind[0][0]);
	return out;
    }

    float wrapFrame (float frame, uint32_t frameCount, const std::string& mode) {
	if (frameCount == 0 || !std::isfinite (frame)) {
	    return 0.0f;
	}

	frame = std::max (frame, 0.0f);
	const float length = static_cast<float> (frameCount);

	if (mode == "single") {
	    return std::clamp (frame, 0.0f, length);
	}

	if (mode == "mirror") {
	    const float phase = std::fmod (frame, length * 2.0f);
	    return phase <= length ? phase : length * 2.0f - phase;
	}

	return std::fmod (frame, length);
    }

    Keyframe sample (const std::vector<Keyframe>& track, float frame) {
	if (track.empty ()) {
	    return Keyframe {};
	}

	const float clamped = std::clamp (frame, 0.0f, static_cast<float> (track.size () - 1));
	const size_t lower = static_cast<size_t> (clamped);
	const size_t upper = std::min (lower + 1, track.size () - 1);
	const float t = clamped - static_cast<float> (lower);
	Keyframe out;

	for (int i = 0; i < 3; ++i) {
	    out.translation[i]
		= track[lower].translation[i] + (track[upper].translation[i] - track[lower].translation[i]) * t;
	    out.rotation[i] = track[lower].rotation[i] + (track[upper].rotation[i] - track[lower].rotation[i]) * t;
	    out.scale[i] = track[lower].scale[i] + (track[upper].scale[i] - track[lower].scale[i]) * t;
	}

	return out;
    }

    const AnimationClip* findClip (const Rig& rig, uint32_t id) {
	for (const AnimationClip& clip : rig.clips) {
	    if (clip.id == id) {
		return &clip;
	    }
	}

	return nullptr;
    }

    void seedPose (const Rig& rig, const AnimationClip* clip, std::vector<Keyframe>& pose) {
	pose.resize (rig.bones.size ());

	for (size_t bone = 0; bone < rig.bones.size (); ++bone) {
	    if (clip != nullptr && bone < clip->tracks.size () && !clip->tracks[bone].empty ()) {
		pose[bone] = clip->tracks[bone][0];
	    } else {
		pose[bone] = keyframeFromBind (rig.bones[bone]);
	    }
	}
    }

    void accumulateLayer (const AnimationClip& clip, const AnimationLayer& layer, std::vector<Keyframe>& pose) {
	// Each layer adds its clip's motion relative to the clip's first frame on
	// top of the assembled pose.
	const float frame = wrapFrame (layer.time * clip.fps, clip.frame_count, clip.loop_mode);
	const size_t bones = std::min (pose.size (), clip.tracks.size ());

	for (size_t bone = 0; bone < bones; ++bone) {
	    const std::vector<Keyframe>& track = clip.tracks[bone];
	    if (track.empty ()) {
		continue;
	    }

	    const Keyframe now = sample (track, frame);
	    for (int axis = 0; axis < 3; ++axis) {
		pose[bone].translation[axis] += layer.blend * (now.translation[axis] - track[0].translation[axis]);
		pose[bone].rotation[axis] += layer.blend * (now.rotation[axis] - track[0].rotation[axis]);
		pose[bone].scale[axis] += layer.blend * (now.scale[axis] - track[0].scale[axis]);
	    }
	}
    }

    bool buildBoneOrder (const Rig& rig, std::vector<uint32_t>& order) {
	const size_t count = rig.bones.size ();
	order.clear ();
	order.reserve (count);

	std::vector<uint32_t> inDegree (count, 0);
	std::vector<std::vector<uint32_t>> children (count);
	std::vector<uint32_t> queue;

	for (size_t bone = 0; bone < count; ++bone) {
	    const uint32_t parent = rig.bones[bone].parent;

	    if (parent >= count) {
		queue.push_back (static_cast<uint32_t> (bone));
	    } else {
		children[parent].push_back (static_cast<uint32_t> (bone));
		++inDegree[bone];
	    }
	}

	for (size_t head = 0; head < queue.size (); ++head) {
	    const uint32_t bone = queue[head];
	    order.push_back (bone);

	    for (const uint32_t child : children[bone]) {
		if (--inDegree[child] == 0) {
		    queue.push_back (child);
		}
	    }
	}

	return order.size () == count;
    }

    void solveControllers (
	const Rig& rig, const std::vector<AnimationLayer>& layers, std::vector<Keyframe>& pose,
	const std::vector<uint32_t>& boneOrder
    ) {
	if (rig.controllers.empty () || boneOrder.size () != pose.size ()) {
	    return;
	}

	std::vector<Keyframe> controls (rig.controllers.size ());
	std::vector<bool> available (rig.controllers.size (), false);
	bool seeded = false;

	for (const AnimationLayer& layer : layers) {
	    const AnimationClip* clip = layer.visible ? findClip (rig, layer.animation_id) : nullptr;

	    if (clip == nullptr) {
		continue;
	    }

	    const float frame = wrapFrame (layer.time * clip->fps, clip->frame_count, clip->loop_mode);

	    for (size_t control = 0; control < controls.size (); ++control) {
		const size_t trackIndex = rig.bones.size () + control;

		if (trackIndex >= clip->tracks.size () || clip->tracks[trackIndex].empty ()) {
		    continue;
		}

		const std::vector<Keyframe>& track = clip->tracks[trackIndex];

		if (!available[control]) {
		    controls[control] = track[0];
		}

		available[control] = true;
		const Keyframe now = sample (track, frame);

		for (int axis = 0; axis < 3; ++axis) {
		    controls[control].translation[axis]
			+= layer.blend * (now.translation[axis] - track[0].translation[axis]);
		    controls[control].rotation[axis] += layer.blend * (now.rotation[axis] - track[0].rotation[axis]);
		    controls[control].scale[axis] += layer.blend * (now.scale[axis] - track[0].scale[axis]);
		}
	    }

	    seeded = true;
	}

	if (!seeded) {
	    return;
	}

	std::vector<glm::mat4> world (pose.size (), glm::mat4 (1.0f));
	const auto updateWorld = [&] {
	    for (const uint32_t bone : boneOrder) {
		const glm::mat4 local = composeLocal (pose[bone]);
		const uint32_t parent = rig.bones[bone].parent;
		world[bone] = parent < world.size () ? world[parent] * local : local;
	    }
	};

	for (size_t control = 0; control < controls.size (); ++control) {
	    const Controller& controller = rig.controllers[control];

	    if (!available[control] || controller.bone_index >= pose.size ()) {
		continue;
	    }
	    if (controller.pole) {
		continue; // pole controls select the limb's bend direction
	    }

	    const uint32_t end = controller.bone_index;
	    if (rig.bones[end].ik_depth == 0 || rig.bones[end].ik_depth > 2) {
		continue;
	    }

	    const uint32_t mid = rig.bones[end].parent;
	    if (mid >= pose.size ()) {
		continue;
	    }

	    const uint32_t base = rig.bones[end].ik_depth == 2 ? rig.bones[mid].parent : mid;
	    if (base >= pose.size ()) {
		continue;
	    }

	    updateWorld ();
	    const glm::mat4& reference = world[base];
	    const glm::mat4 target = composeLocal (controls[control]);
	    const float targetAngle = std::atan2 (target[0][1], target[0][0]);

	    if (base != mid) {
		const float l1 = std::hypot (pose[mid].translation[0], pose[mid].translation[1]);
		const float l2 = std::hypot (pose[end].translation[0], pose[end].translation[1]);
		const float dx = target[3][0] - reference[3][0];
		const float dy = target[3][1] - reference[3][1];
		const float distance = std::hypot (dx, dy);

		if (l1 > 1e-5f && l2 > 1e-5f && distance > 1e-5f) {
		    float side = 1.0f;

		    for (size_t pole = 0; pole < controls.size (); ++pole) {
			if (!available[pole] || !rig.controllers[pole].pole
			    || rig.controllers[pole].bone_index != end) {
			    continue;
			}

			const glm::mat4 poleWorld = composeLocal (controls[pole]);
			const float px = poleWorld[3][0] - reference[3][0];
			const float py = poleWorld[3][1] - reference[3][1];
			side = dx * py - dy * px < 0.0f ? -1.0f : 1.0f;
			break;
		    }

		    const float reach = std::clamp (distance, std::fabs (l1 - l2) + 1e-5f, l1 + l2);
		    const float cosine
			= std::clamp ((l1 * l1 + reach * reach - l2 * l2) / (2.0f * l1 * reach), -1.0f, 1.0f);
		    const float direction = std::atan2 (dy, dx) + side * std::acos (cosine);
		    const uint32_t parent = rig.bones[base].parent;
		    const float parentAngle
			= parent < world.size () ? std::atan2 (world[parent][0][1], world[parent][0][0]) : 0.0f;

		    pose[base].rotation[2]
			= direction - parentAngle - std::atan2 (pose[mid].translation[1], pose[mid].translation[0]);
		    updateWorld ();

		    const float endpointX = reference[3][0] + dx * reach / distance;
		    const float endpointY = reference[3][1] + dy * reach / distance;
		    const float secondDirection
			= std::atan2 (endpointY - world[mid][3][1], endpointX - world[mid][3][0]);
		    const float baseAngle = std::atan2 (world[base][0][1], world[base][0][0]);
		    pose[mid].rotation[2]
			= secondDirection - baseAngle - std::atan2 (pose[end].translation[1], pose[end].translation[0]);
		}
	    } else {
		const float dx = target[3][0] - world[mid][3][0];
		const float dy = target[3][1] - world[mid][3][1];
		const uint32_t parent = rig.bones[mid].parent;
		const float parentAngle
		    = parent < world.size () ? std::atan2 (world[parent][0][1], world[parent][0][0]) : 0.0f;

		pose[mid].rotation[2] = std::atan2 (dy, dx) - parentAngle
		    - std::atan2 (pose[end].translation[1], pose[end].translation[0]);
	    }

	    updateWorld ();
	    pose[end].rotation[2] = targetAngle - std::atan2 (world[mid][0][1], world[mid][0][0]);
	}
    }

    void expandBounds (glm::vec4& bounds, const float x, const float y) {
	bounds.x = std::min (bounds.x, x);
	bounds.y = std::min (bounds.y, y);
	bounds.z = std::max (bounds.z, x);
	bounds.w = std::max (bounds.w, y);
    }

    void expandBounds (glm::vec4& bounds, const std::vector<float>& positions) {
	for (size_t index = 0; index + 2 < positions.size (); index += 3) {
	    expandBounds (bounds, positions[index], positions[index + 1]);
	}
    }
} // namespace

void Pose::init (const ModelFile& model) {
    this->inverse_bind_world.clear ();
    this->bone_order.clear ();

    const Rig* rig = getRig (model);

    if (rig == nullptr) {
	return;
    }

    const size_t count = rig->bones.size ();

    if (count == 0 || !buildBoneOrder (*rig, this->bone_order)) {
	this->bone_order.clear ();
	return;
    }

    this->inverse_bind_world.assign (count, glm::mat4 (1.0f));
    std::vector<glm::mat4> world (count, glm::mat4 (1.0f));

    for (const uint32_t bone : this->bone_order) {
	const glm::mat4 local = bindLocal (rig->bones[bone]);
	const uint32_t parent = rig->bones[bone].parent;
	world[bone] = parent < count ? world[parent] * local : local;

	if (std::fabs (glm::determinant (world[bone])) > 1e-12f) {
	    this->inverse_bind_world[bone] = glm::inverse (world[bone]);
	}
    }
}

void Pose::advance (const Rig& rig, std::vector<AnimationLayer>& layers, float dt) const {
    for (AnimationLayer& layer : layers) {
	if (!layer.visible) {
	    continue;
	}

	layer.time = std::max (0.0f, layer.time + dt * layer.rate);
	const AnimationClip* clip = findClip (rig, layer.animation_id);

	if (clip != nullptr && clip->loop_mode == "single" && clip->fps > 0.0f
	    && layer.time * clip->fps >= static_cast<float> (clip->frame_count)) {
	    layer.time = static_cast<float> (clip->frame_count) / clip->fps;
	}
    }
}

std::vector<glm::mat4>
Pose::computeBoneMatrices (const ModelFile& model, const std::vector<AnimationLayer>& layers) const {
    const Rig* rig = getRig (model);

    if (rig == nullptr) {
	return {};
    }

    const size_t count = rig->bones.size ();
    std::vector<glm::mat4> matrices (count, glm::mat4 (1.0f));

    if (count == 0 || this->bone_order.size () != count || this->inverse_bind_world.size () != count) {
	return matrices;
    }

    std::vector<Keyframe> pose;
    bool seeded = false;

    for (const AnimationLayer& layer : layers) {
	const AnimationClip* clip = layer.visible ? findClip (*rig, layer.animation_id) : nullptr;

	if (clip == nullptr) {
	    continue;
	}

	if (!seeded) {
	    seedPose (*rig, clip, pose);
	    seeded = true;
	}

	accumulateLayer (*clip, layer, pose);
    }

    // No active clip: use the first clip's first frame as the assembled rest
    // pose. Falling back to the raw atlas vertices here would split the puppet
    // back into its texture pieces.
    if (!seeded) {
	seedPose (*rig, rig->clips.empty () ? nullptr : &rig->clips.front (), pose);
    }

    solveControllers (*rig, layers, pose, this->bone_order);

    std::vector<glm::mat4> world (count, glm::mat4 (1.0f));

    for (const uint32_t bone : this->bone_order) {
	const glm::mat4 local = composeLocal (pose[bone]);
	const uint32_t parent = rig->bones[bone].parent;
	world[bone] = parent < count ? world[parent] * local : local;
	matrices[bone] = world[bone] * this->inverse_bind_world[bone];
    }

    return matrices;
}

void Pose::skin (const ModelFile& model, const std::vector<AnimationLayer>& layers, std::vector<float>& out) const {
    const std::vector<glm::mat4> matrices = this->computeBoneMatrices (model, layers);
    out.resize (model.vertexCount () * 3);

    size_t vertexIndex = 0;

    for (const MeshSection& section : model.sections) {
	const MeshVertexLayout& layout = section.layout;

	if (layout.stride == 0) {
	    continue;
	}

	const size_t vertexCount = section.vertices.size () / layout.stride;

	for (size_t index = 0; index < vertexCount; ++index) {
	    const float* vertex = section.vertices.data () + index * layout.stride;
	    const float* position = vertex + layout.positionOffset;
	    float sum[3] = { 0.0f, 0.0f, 0.0f };
	    float weightTotal = 0.0f;

	    if (layout.skinOffset >= 0 && !matrices.empty ()) {
		uint32_t indices[4];
		std::memcpy (indices, vertex + layout.skinOffset, sizeof (indices));
		const float* weights = vertex + layout.skinOffset + 4;

		for (int k = 0; k < 4; ++k) {
		    const float weight = weights[k];

		    if (weight <= 0.0f || indices[k] >= matrices.size ()) {
			continue;
		    }

		    const glm::mat4& matrix = matrices[indices[k]];

		    for (int axis = 0; axis < 3; ++axis) {
			sum[axis] += weight
			    * (matrix[0][axis] * position[0] + matrix[1][axis] * position[1]
			       + matrix[2][axis] * position[2] + matrix[3][axis]);
		    }

		    weightTotal += weight;
		}
	    }

	    for (int axis = 0; axis < 3; ++axis) {
		out[vertexIndex * 3 + axis] = weightTotal > 0.0f ? sum[axis] / weightTotal : position[axis];
	    }

	    ++vertexIndex;
	}
    }
}

glm::vec4 computeBounds (const ModelFile& model) {
    if (model.vertexCount () == 0) {
	return glm::vec4 (0.0f);
    }

    glm::vec4 bounds (
	std::numeric_limits<float>::max (), std::numeric_limits<float>::max (), -std::numeric_limits<float>::max (),
	-std::numeric_limits<float>::max ()
    );
    const Rig* rig = getRig (model);

    if (rig == nullptr) {
	// Static models have no skeleton: the vertex positions are already in
	// model space.
	for (const MeshSection& section : model.sections) {
	    if (section.layout.stride == 0) {
		continue;
	    }

	    const size_t vertexCount = section.vertices.size () / section.layout.stride;

	    for (size_t index = 0; index < vertexCount; ++index) {
		const float* position
		    = section.vertices.data () + index * section.layout.stride + section.layout.positionOffset;
		expandBounds (bounds, position[0], position[1]);
	    }
	}

	return bounds;
    }

    Pose pose;
    pose.init (model);
    std::vector<float> positions;
    std::vector<AnimationLayer> restPose;

    // Rest pose first: the raw vertices are in atlas layout, skinning them
    // with the assembled first frame is what produces the real character.
    pose.skin (model, restPose, positions);
    expandBounds (bounds, positions);

    for (const AnimationClip& clip : rig->clips) {
	if (clip.fps <= 0.0f || clip.frame_count == 0) {
	    continue;
	}

	const uint32_t sampleCount = clip.frame_count >= 256 ? 257 : clip.frame_count + 1;
	std::vector<AnimationLayer> layers (1);

	for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
	    const float progress
		= sampleCount > 1 ? static_cast<float> (sampleIndex) / static_cast<float> (sampleCount - 1) : 0.0f;

	    layers[0] = AnimationLayer {};
	    layers[0].animation_id = clip.id;
	    layers[0].time = static_cast<float> (clip.frame_count) * progress / clip.fps;
	    pose.skin (model, layers, positions);
	    expandBounds (bounds, positions);
	}
    }

    return bounds;
}
} // namespace WallpaperEngine::Render::Objects::Puppet
