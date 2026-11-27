#include "MeshParser.h"

#include "MaterialParser.h"
#include "PuppetParser.h"

#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Utils/BufferReader.h"
#include "WallpaperEngine/Logging/Log.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Model;
using WallpaperEngine::Data::Utils::BufferReader;

namespace {
std::vector<uint8_t> readAll (const ReadStreamSharedPtr& file) {
    return { std::istreambuf_iterator<char> (*file), std::istreambuf_iterator<char> () };
}

void loadMaterials (ModelFile& model, const Project& project) {
    for (MeshSection& section : model.sections) {
	section.materials.reserve (section.materialPaths.size ());

	for (const std::string& path : section.materialPaths) {
	    section.materials.push_back (MaterialParser::load (project, path));
	}
    }
}
} // namespace

bool WallpaperEngine::Data::Parsers::parseMeshData (BufferReader& reader, ModelFile& out, std::string& error) {
    const auto fail = [&error] (std::string message) {
	error = std::move (message);
	return false;
    };

    out = ModelFile {};

    std::string version;

    if (!reader.cstring (version) || version.size () != 8 || version.rfind ("MDLV", 0) != 0) {
	return fail ("expected an MDLV model header");
    }

    for (size_t i = 4; i < 8; ++i) {
	if (version[i] < '0' || version[i] > '9') {
	    return fail ("malformed model version tag " + version);
	}

	out.versionNumber = out.versionNumber * 10 + static_cast<uint32_t> (version[i] - '0');
    }

    out.version = std::move (version);

    uint32_t skinCount = 0;
    uint32_t sectionCount = 0;

    if (!reader.u32 (out.headerAttrMask) || !reader.u32 (skinCount) || !reader.u32 (sectionCount)) {
	return fail ("truncated model header");
    }

    if (skinCount == 0 || skinCount > 64) {
	return fail ("invalid skin count " + std::to_string (skinCount));
    }

    if (sectionCount == 0 || sectionCount > 4096) {
	return fail ("invalid section count " + std::to_string (sectionCount));
    }

    out.sections.reserve (sectionCount);

    for (uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
	MeshSection section;
	section.materialPaths.reserve (skinCount);

	for (uint32_t skin = 0; skin < skinCount; ++skin) {
	    std::string materialPath;

	    if (!reader.cstring (materialPath)) {
		return fail ("truncated material path in section " + std::to_string (sectionIndex));
	    }

	    section.materialPaths.push_back (std::move (materialPath));
	}

	// 1 marks 32-bit indices (meshes with more than 65535 vertices), 0 marks
	// 16-bit indices
	uint32_t wideIndices = 0;

	if (!reader.u32 (wideIndices) || wideIndices > 1) {
	    return fail ("invalid index width in section " + std::to_string (sectionIndex));
	}

	// MDLV0017 moved the layout tag to a per-section field and added a
	// bounding box; MDLV0016 also stores the per-section tag. MDLV0023 uses
	// the same per-submesh tag and appends a 6 byte trailer after the
	// indices. Skinned puppet submeshes tag their layout as
	// MeshFormat_Skinned (0x0180000f), not as the header word.
	uint32_t attrMask = out.headerAttrMask;

	if (out.versionNumber >= 17) {
	    float aabb[6];

	    for (float& value : aabb) {
		if (!reader.f32 (value)) {
		    return fail ("truncated bounding box in section " + std::to_string (sectionIndex));
		}
	    }

	    section.aabbMin = glm::vec3 (aabb[0], aabb[1], aabb[2]);
	    section.aabbMax = glm::vec3 (aabb[3], aabb[4], aabb[5]);

	    if (!reader.u32 (attrMask)) {
		return fail ("truncated layout tag in section " + std::to_string (sectionIndex));
	    }
	} else if (out.versionNumber == 16) {
	    if (!reader.u32 (attrMask)) {
		return fail ("truncated layout tag in section " + std::to_string (sectionIndex));
	    }
	}

	uint32_t vertexBytes = 0;

	if (!reader.u32 (vertexBytes) || vertexBytes == 0) {
	    return fail ("invalid vertex buffer size in section " + std::to_string (sectionIndex));
	}

	const MeshVertexLayout layout = out.versionNumber >= 17
	    ? meshVertexLayout (attrMask)
	    : legacyMeshVertexLayout (out.versionNumber, attrMask, vertexBytes);

	if (layout.stride == 0 || vertexBytes % (layout.stride * sizeof (float)) != 0) {
	    return fail (
		"vertex buffer size " + std::to_string (vertexBytes) + " does not match the layout for attribute mask "
		+ std::to_string (attrMask)
	    );
	}

	const size_t vertexCount = vertexBytes / (layout.stride * sizeof (float));

	if (vertexCount == 0 || vertexCount > 10000000) {
	    return fail ("invalid vertex count " + std::to_string (vertexCount));
	}

	section.vertices.resize (vertexBytes / sizeof (float));

	if (!reader.read (section.vertices.data (), vertexBytes)) {
	    return fail ("truncated vertex buffer in section " + std::to_string (sectionIndex));
	}

	uint32_t indexBytes = 0;

	if (!reader.u32 (indexBytes) || indexBytes == 0) {
	    return fail ("invalid index buffer size in section " + std::to_string (sectionIndex));
	}

	const size_t indexWidth = wideIndices != 0 ? sizeof (uint32_t) : sizeof (uint16_t);

	if (indexBytes % indexWidth != 0) {
	    return fail ("index buffer size is not a multiple of the index width");
	}

	const size_t indexCount = indexBytes / indexWidth;

	if (indexCount > 30000000) {
	    return fail ("index count is out of range");
	}

	section.indices.resize (indexCount);

	if (wideIndices != 0) {
	    if (!reader.read (section.indices.data (), indexBytes)) {
		return fail ("truncated index buffer in section " + std::to_string (sectionIndex));
	    }
	} else {
	    std::vector<uint16_t> narrow (indexCount);

	    if (!reader.read (narrow.data (), indexBytes)) {
		return fail ("truncated index buffer in section " + std::to_string (sectionIndex));
	    }

	    std::copy (narrow.begin (), narrow.end (), section.indices.begin ());
	}

	for (const uint32_t index : section.indices) {
	    if (index >= vertexCount) {
		return fail ("index " + std::to_string (index) + " is outside the vertex buffer");
	    }
	}

	// MDLV0023 appends a 6 byte trailer after the index buffer.
	if (out.versionNumber >= 23 && !reader.skip (6)) {
	    return fail ("truncated section trailer");
	}

	section.attrMask = attrMask;
	section.layout = layout;
	out.sections.push_back (std::move (section));
    }

    return true;
}

ModelFileUniquePtr MeshParser::load (const Project& project, const std::string& filename) {
    ReadStreamSharedPtr stream = project.assetLocator->read (filename);

    if (!stream) {
	sLog.exception ("Failed to open model file: ", filename);
    }

    return parse (stream, project, filename);
}

ModelFileUniquePtr
MeshParser::parse (const ReadStreamSharedPtr& file, const Project& project, const std::string& filename) {
    if (!file) {
	sLog.exception ("Failed to read model file: ", filename);
    }

    const std::vector<uint8_t> data = readAll (file);

    if (file->bad ()) {
	sLog.exception ("Failed to read model file: ", filename);
    }

    return parseBuffer (data.data (), data.size (), &project, filename);
}

ModelFileUniquePtr
MeshParser::parseBuffer (const uint8_t* data, size_t size, const Project* project, const std::string& filename) {
    BufferReader reader { data, size };
    ModelFile model;
    std::string error;

    if (!parseMeshData (reader, model, error)) {
	sLog.exception ("Model ", filename, ": ", error);
    }

    if (!parsePuppetData (reader, model, error)) {
	sLog.exception ("Puppet model ", filename, ": ", error);
    }

    if (project != nullptr) {
	loadMaterials (model, *project);
    }

    return std::make_unique<ModelFile> (std::move (model));
}
