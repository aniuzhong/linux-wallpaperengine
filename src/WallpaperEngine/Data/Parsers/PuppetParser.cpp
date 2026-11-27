#include "PuppetParser.h"

#include "MeshParser.h"

#include "WallpaperEngine/Data/JSON.h"

#include <algorithm>
#include <limits>
#include <utility>

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Model::Puppet;
using WallpaperEngine::Data::Utils::BufferReader;

namespace WallpaperEngine::Data::Parsers {
namespace {
    constexpr uint32_t kMaxBones = 4096;
    constexpr uint32_t kMaxTracks = 4096;
    constexpr uint32_t kMaxAnimations = 64;
    constexpr uint32_t kMaxFrames = 1000000;

    bool readKeyframes (BufferReader& reader, std::vector<Keyframe>& track, size_t count) {
	if (count == 0 || count > kMaxFrames + 1 || count > std::numeric_limits<size_t>::max () / 36
	    || !reader.has (count * 36)) {
	    return false;
	}

	track.resize (count);
	for (Keyframe& keyframe : track) {
	    if (!reader.f32 (keyframe.translation[0]) || !reader.f32 (keyframe.translation[1])
		|| !reader.f32 (keyframe.translation[2]) || !reader.f32 (keyframe.rotation[0])
		|| !reader.f32 (keyframe.rotation[1]) || !reader.f32 (keyframe.rotation[2])
		|| !reader.f32 (keyframe.scale[0]) || !reader.f32 (keyframe.scale[1])
		|| !reader.f32 (keyframe.scale[2])) {
		return false;
	    }
	}
	return true;
    }

    bool parseClip (BufferReader& reader, uint32_t id, size_t extraTrackCount, AnimationClip& clip) {
	uint32_t reserved = 0;
	uint32_t trackCount = 0;
	if (!reader.cstring (clip.name) || !reader.cstring (clip.loop_mode) || !reader.f32 (clip.fps)
	    || !reader.u32 (clip.frame_count) || !reader.u32 (reserved) || !reader.u32 (trackCount)) {
	    return false;
	}

	clip.id = id;
	if (clip.name.empty () || clip.loop_mode.empty () || clip.loop_mode.size () > 32
	    || !(clip.fps > 0.0f && clip.fps <= 1000.0f) || clip.frame_count == 0 || clip.frame_count > kMaxFrames
	    || trackCount == 0 || trackCount > kMaxTracks || extraTrackCount > kMaxTracks - trackCount) {
	    return false;
	}

	const size_t keyframeCount = static_cast<size_t> (clip.frame_count) + 1;
	const size_t expectedBytes = keyframeCount * 36;
	clip.tracks.clear ();
	clip.tracks.reserve (trackCount + extraTrackCount);
	clip.tracks.resize (trackCount);

	for (uint32_t track = 0; track < trackCount; ++track) {
	    uint32_t trackReserved = 0;
	    uint32_t keyframeBytes = 0;
	    if (!reader.u32 (trackReserved) || !reader.u32 (keyframeBytes) || keyframeBytes != expectedBytes) {
		return false;
	    }
	    if (!readKeyframes (reader, clip.tracks[track], keyframeCount)) {
		return false;
	    }
	}

	// Controller tracks follow the bone tracks and use the same keyframe
	// layout. Reading at most one per controller prevents wandering into the
	// next clip when the padding happens to look like a track header.
	for (size_t extra = 0; extra < extraTrackCount && reader.has (8); ++extra) {
	    uint32_t declared = 0;
	    if (!reader.peekU32 (declared)) {
		return false;
	    }
	    if (declared != expectedBytes) {
		break;
	    }
	    if (!reader.skip (8)) {
		return false;
	    }
	    clip.tracks.emplace_back ();
	    if (!readKeyframes (reader, clip.tracks.back (), keyframeCount)) {
		return false;
	    }
	}

	return true;
    }

    bool parseNextClip (BufferReader& reader, size_t extraTrackCount, std::vector<AnimationClip>& clips) {
	size_t firstNonZero = reader.pos ();
	while (firstNonZero < reader.end () && reader.data ()[firstNonZero] == 0) {
	    ++firstNonZero;
	}
	if (firstNonZero >= reader.end ()) {
	    return false;
	}

	// The clip id is a little-endian u32, so leading zero bytes are
	// indistinguishable from the padding that separates clips. Try every
	// possible start within those four bytes and accept the first candidate
	// that produces a well-formed clip. The separator's reserved word is
	// always zero, which keeps wrong candidates from being accepted.
	for (size_t back = 0; back <= 3 && firstNonZero >= back; ++back) {
	    const size_t start = firstNonZero - back;
	    if (start < reader.pos ()) {
		break;
	    }
	    BufferReader candidate { reader.data (), reader.size (), start, reader.end () };
	    uint32_t id = 0;
	    uint32_t reserved = 0;
	    if (!candidate.u32 (id) || !candidate.u32 (reserved) || reserved != 0) {
		continue;
	    }
	    AnimationClip clip;
	    if (!parseClip (candidate, id, extraTrackCount, clip)) {
		continue;
	    }
	    reader.seek (candidate.pos ());
	    clips.push_back (std::move (clip));
	    return true;
	}
	return false;
    }

    bool parseAnimations (BufferReader& reader, Rig& rig) {
	std::string header;
	uint32_t endOffset = 0;
	uint32_t numAnimations = 0;
	uint32_t firstId = 0;
	uint32_t reserved = 0;
	if (!reader.cstring (header) || header.rfind ("MDLA", 0) != 0 || !reader.u32 (endOffset)
	    || !reader.u32 (numAnimations) || !reader.u32 (firstId) || !reader.u32 (reserved)) {
	    return false;
	}
	if (numAnimations == 0 || numAnimations > kMaxAnimations) {
	    return false;
	}

	// endOffset points at the last byte of the MDLA section. Zero means the
	// writer did not fill it in, in which case the file end is the safe limit.
	size_t sectionEnd = reader.size ();
	if (endOffset != 0 && endOffset >= reader.pos () && endOffset <= reader.size ()) {
	    sectionEnd = std::min (reader.size (), static_cast<size_t> (endOffset) + 1);
	}

	BufferReader clipReader { reader.data (), reader.size (), reader.pos (), sectionEnd };
	AnimationClip first;
	if (!parseClip (clipReader, firstId, rig.controllers.size (), first)) {
	    return false;
	}
	rig.clips.push_back (std::move (first));

	for (uint32_t animation = 1; animation < numAnimations; ++animation) {
	    if (!parseNextClip (clipReader, rig.controllers.size (), rig.clips)) {
		return false;
	    }
	}

	reader.seek (sectionEnd);
	return true;
    }

    bool parseBones (BufferReader& reader, Rig& rig) {
	std::string header;
	uint32_t nextOffset = 0;
	uint32_t numBones = 0;
	uint8_t reserved = 0;
	if (!reader.cstring (header) || header.rfind ("MDLS", 0) != 0 || !reader.u32 (nextOffset)
	    || !reader.u32 (numBones) || !reader.u8 (reserved)) {
	    return false;
	}
	if (numBones == 0 || numBones > kMaxBones || nextOffset < reader.pos () || nextOffset > reader.size ()) {
	    return false;
	}

	BufferReader body { reader.data (), reader.size (), reader.pos (), nextOffset };
	std::vector<Bone> bones;
	bones.reserve (numBones);

	for (uint32_t i = 0; i < numBones; ++i) {
	    Bone bone;
	    uint32_t type = 0;
	    uint32_t parent = 0;
	    uint32_t byteLength = 0;
	    if (!body.u32 (type) || !body.u32 (parent) || !body.u32 (byteLength)) {
		return false;
	    }
	    if (byteLength < 16 * sizeof (float) || byteLength > 4096 || byteLength % sizeof (float) != 0) {
		return false;
	    }
	    if (parent != 0xFFFFFFFFu && parent >= numBones) {
		return false;
	    }

	    const uint32_t floats = byteLength / sizeof (float);
	    for (uint32_t slot = 0; slot < floats; ++slot) {
		float value = 0.0f;
		if (!body.f32 (value)) {
		    return false;
		}
		if (slot < 16) {
		    bone.bind_matrix[slot] = value;
		}
	    }

	    std::string info;
	    if (!body.cstring (info) || !body.cstring (bone.name)) {
		return false;
	    }

	    const auto infoJson = WallpaperEngine::Data::JSON::JSON::parse (info, nullptr, false);

	    if (infoJson.is_object ()) {
		const auto ik = infoJson.find ("ik");
		const auto depth = infoJson.find ("ikd");

		if (ik != infoJson.end () && ik->is_boolean () && ik->get<bool> () && depth != infoJson.end ()
		    && depth->is_number ()) {
		    bone.ik_depth = static_cast<uint32_t> (depth->get<double> ());
		}
	    }
	    bone.parent = parent;
	    bones.push_back (std::move (bone));
	}

	// Older skeletons append IK records; their animation tracks follow the
	// ordinary bone tracks.
	std::vector<Controller> controllers;
	while (body.has (73) && body.data ()[body.pos ()] == 0) {
	    BufferReader controllerReader { body.data (), body.size (), body.pos () + 1, body.end () };
	    uint32_t bone = 0;
	    uint32_t target = 0;
	    if (!controllerReader.u32 (bone) || !controllerReader.u32 (target) || bone >= bones.size () || target > 1
		|| bones[bone].ik_depth == 0) {
		break;
	    }
	    float matrix[16];
	    bool valid = true;
	    for (float& value : matrix) {
		valid = controllerReader.f32 (value) && valid;
	    }
	    if (!valid || matrix[3] != 0.0f || matrix[7] != 0.0f || matrix[11] != 0.0f || matrix[15] != 1.0f) {
		break;
	    }
	    controllers.push_back ({ bone, target != 0 });
	    body.seek (controllerReader.pos ());
	}

	rig.bones = std::move (bones);
	rig.controllers = std::move (controllers);
	reader.seek (nextOffset);
	return true;
    }
} // namespace

bool parsePuppetData (BufferReader& reader, PuppetModel& out, std::string& error) {
    const size_t searchFrom = reader.pos ();
    size_t offset = 0;
    Rig rig;
    bool found = false;

    if (reader.find ("MDLS", searchFrom, offset)) {
	BufferReader bonesReader { reader.data (), reader.size (), offset };

	if (!parseBones (bonesReader, rig)) {
	    error = "invalid MDLS skeleton section";
	    return false;
	}

	found = true;
    }

    if (reader.find ("MDLA", searchFrom, offset)) {
	BufferReader animationsReader { reader.data (), reader.size (), offset };

	if (!parseAnimations (animationsReader, rig)) {
	    error = "invalid MDLA animation section";
	    return false;
	}

	found = true;
    }

    if (found) {
	out.puppet = std::move (rig);
    }

    return true;
}

bool parsePuppetModel (const uint8_t* data, size_t size, PuppetModel& out) {
    out = PuppetModel {};

    if (data == nullptr || size == 0) {
	return false;
    }

    BufferReader reader { data, size };
    PuppetModel model;
    std::string error;

    if (!parseMeshData (reader, model, error)) {
	return false;
    }

    if (!parsePuppetData (reader, model, error)) {
	return false;
    }

    out = std::move (model);
    return true;
}
} // namespace WallpaperEngine::Data::Parsers
