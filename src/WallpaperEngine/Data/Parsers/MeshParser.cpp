#include "MeshParser.h"

#include "MaterialParser.h"

#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Logging/Log.h"

#include <memory>

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Model;

ModelFileUniquePtr MeshParser::load (const Project& project, const std::string& filename) {
    ReadStreamSharedPtr stream = project.assetLocator->read (filename);

    if (!stream) {
	sLog.exception ("Failed to open model file: ", filename);
    }

    return parse (stream, project, filename);
}

ModelFileUniquePtr
MeshParser::parse (const ReadStreamSharedPtr& file, const Project& project, const std::string& filename) {
    const auto reader = std::make_unique<BinaryReader> (file);

    const std::string version = reader->nextNullTerminatedString ();

    if (version.rfind ("MDLV", 0) != 0 || version.size () < 8) {
	sLog.exception ("Expected model header to start with MDLV in ", filename, ", got ", version);
    }

    // the four digits after MDLV drive the layout differences
    int versionNumber = 0;

    for (size_t i = 4; i < 8; i++) {
	const char c = version[i];

	if (c < '0' || c > '9') {
	    sLog.exception ("Model ", filename, " has a malformed version tag ", version);
	}

	versionNumber = versionNumber * 10 + (c - '0');
    }

    const uint32_t headerAttrMask = reader->nextUInt32 ();
    // skins are alternative material sets for the same mesh, the scene
    // object's "skin" field picks which one renders (e.g. audiophile's grid
    // carries two and selects the second)
    const uint32_t skinCount = reader->nextUInt32 ();

    if (skinCount == 0 || skinCount > 64) {
	sLog.exception ("Model ", filename, " has an invalid skin count ", skinCount);
    }

    const uint32_t sectionCount = reader->nextUInt32 ();

    if (sectionCount == 0 || sectionCount > 4096) {
	sLog.exception ("Model ", filename, " has an invalid section count ", sectionCount);
    }

    auto result = std::make_unique<ModelFile> (ModelFile {
	.version = version,
	.headerAttrMask = headerAttrMask,
	.sections = {},
    });

    result->sections.reserve (sectionCount);

    for (uint32_t i = 0; i < sectionCount; i++) {
	// one material per skin
	std::vector<std::string> materialPaths;
	materialPaths.reserve (skinCount);

	for (uint32_t s = 0; s < skinCount; s++) {
	    materialPaths.push_back (reader->nextNullTerminatedString ());
	}

	// 1 marks 32-bit indices (meshes with more than 65535 vertices), 0 marks
	// 16-bit indices
	const uint32_t wideIndices = reader->nextUInt32 ();

	if (wideIndices > 1) {
	    sLog.exception ("Model ", filename, " section ", i, " has invalid index width ", wideIndices);
	}

	// MDLV0017 moved the layout tag to a per-section field and added a
	// bounding box; MDLV0023 uses the same per-submesh tag and appends a
	// 6 byte trailer after the indices. Skinned puppet submeshes tag their
	// layout as MeshFormat_Skinned (0x0180000f), not as the header word.
	uint32_t attrMask = headerAttrMask;
	glm::vec3 aabbMin (0.0f);
	glm::vec3 aabbMax (0.0f);

	if (versionNumber >= 17) {
	    const float aabb[6] = {
		reader->nextFloat (), reader->nextFloat (), reader->nextFloat (),
		reader->nextFloat (), reader->nextFloat (), reader->nextFloat (),
	    };
	    aabbMin = glm::vec3 (aabb[0], aabb[1], aabb[2]);
	    aabbMax = glm::vec3 (aabb[3], aabb[4], aabb[5]);
	    attrMask = reader->nextUInt32 ();
	}

	const uint32_t vertexBufferSize = reader->nextUInt32 ();
	const uint32_t stride = meshVertexStride (attrMask);

	if (stride == 0 || vertexBufferSize % (stride * sizeof (float)) != 0) {
	    sLog.exception (
		"Model ", filename, " section ", i, " has invalid vertex buffer size ", vertexBufferSize,
		" for attribute mask ", attrMask
	    );
	}

	std::vector<float> vertices (vertexBufferSize / sizeof (float));
	reader->next (reinterpret_cast<char*> (vertices.data ()), vertexBufferSize);

	const uint32_t indexBufferSize = reader->nextUInt32 ();
	const uint32_t indexWidth = wideIndices == 1 ? sizeof (uint32_t) : sizeof (uint16_t);

	if (indexBufferSize % indexWidth != 0) {
	    sLog.exception ("Model ", filename, " section ", i, " has invalid index buffer size ", indexBufferSize);
	}

	std::vector<uint32_t> indices (indexBufferSize / indexWidth);

	if (wideIndices == 1) {
	    reader->next (reinterpret_cast<char*> (indices.data ()), indexBufferSize);
	} else {
	    std::vector<uint16_t> narrow (indexBufferSize / sizeof (uint16_t));
	    reader->next (reinterpret_cast<char*> (narrow.data ()), indexBufferSize);
	    indices.assign (narrow.begin (), narrow.end ());
	}

	for (const uint32_t index : indices) {
	    if (index >= vertices.size () / stride) {
		sLog.exception (
		    "Model ", filename, " section ", i, " has index ", index, " out of range ",
		    vertices.size () / stride
		);
	    }
	}

	// MDLV0023 appends a 6 byte trailer after the index buffer
	if (versionNumber >= 23) {
	    char trailer[6];
	    reader->next (trailer, sizeof (trailer));
	}

	std::vector<MaterialUniquePtr> materials;
	materials.reserve (materialPaths.size ());

	for (const auto& materialPath : materialPaths) {
	    materials.push_back (MaterialParser::load (project, materialPath));
	}

	result->sections.push_back (
	    MeshSection {
		.materialPaths = std::move (materialPaths),
		.materials = std::move (materials),
		.vertices = std::move (vertices),
		.indices = std::move (indices),
		.attrMask = attrMask,
		.aabbMin = aabbMin,
		.aabbMax = aabbMax,
	    }
	);
    }

    return result;
}
