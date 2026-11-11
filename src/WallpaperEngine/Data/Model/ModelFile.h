#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Material.h"
#include "Types.h"

namespace WallpaperEngine::Data::Model {
/**
 * A single mesh section inside a compiled .mdl file.
 *
 * Each section holds one interleaved vertex buffer plus its index buffer and
 * one material per skin (the scene object's "skin" field picks one). The
 * attributes present in the vertex buffer are described by attrMask.
 */
struct MeshSection {
    /** Paths of the material jsons this section renders with, one per skin */
    std::vector<std::string> materialPaths;
    /** Parsed materials, aligned with materialPaths */
    std::vector<MaterialUniquePtr> materials;
    /** Interleaved vertex data, layout decided by attrMask */
    std::vector<float> vertices;
    /** Triangle indices into the vertex buffer, widened to 32 bit when needed */
    std::vector<uint32_t> indices;
    /**
     * Vertex layout tag for this section. MDLV0017 stores the attribute mask
     * here, MDLV0023 stores the per-submesh tag (0x0180000F for skinned meshes).
     */
    uint32_t attrMask;
    /** Bounding box, only present in newer versions */
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
};

/**
 * Material a section renders with for the given skin index, clamped to the
 * skins the section actually carries (the parser guarantees at least one)
 */
[[nodiscard]] inline const Material& sectionMaterial (const MeshSection& section, int skin) {
    const size_t index = skin <= 0 ? 0 : std::min (static_cast<size_t> (skin), section.materials.size () - 1);
    return *section.materials[index];
}

/**
 * Vertex attribute bits used by static .mdl layouts.
 *
 * Empirically verified against source .obj files and exact buffer-size
 * division across five wallpapers (masks 9/11/15/39): position and a
 * primary uv channel are always present, bit 1 adds a normal, bit 2 adds
 * a tangent, bit 5 adds the lightmap uv channel. Lightmap materials
 * declare a vec4 texcoord spanning both packed uv channels.
 *
 * Skinned MDLV0023 submeshes do not use this bitmask; they carry the
 * separate MeshFormat_Skinned layout tag below.
 */
enum MeshAttribute : uint32_t {
    MeshAttribute_Position = 1u << 0, // 3 floats, always present
    MeshAttribute_Normal = 1u << 1, // 3 floats
    MeshAttribute_Tangent = 1u << 2, // 4 floats, w holds the handedness
    MeshAttribute_UV = 1u << 3, // informational: uv is always present
    MeshAttribute_LightmapUV = 1u << 5 // 2 floats, second uv channel
};

/** Layout tag used by skinned MDLV0023 submeshes (puppet models). */
constexpr uint32_t MeshFormat_Skinned = 0x0180000F;

/**
 * Float offsets of each attribute inside one interleaved vertex; stride is in
 * floats as well.
 */
struct MeshVertexLayout {
    uint32_t stride = 0;
    uint32_t positionOffset = 0;
    /** -1 when the attribute is absent */
    int32_t normalOffset = -1;
    int32_t tangentOffset = -1;
    /** -1 unless the layout is skinned; 8 floats of bone indices and weights */
    int32_t skinOffset = -1;
    uint32_t uvOffset = 0;
    int32_t lightmapUVOffset = -1;
};

/**
 * Resolves the interleaved vertex layout for the given attribute mask or
 * skinned layout tag.
 */
[[nodiscard]] inline MeshVertexLayout meshVertexLayout (uint32_t attrMask) {
    MeshVertexLayout layout {};

    if (attrMask == MeshFormat_Skinned) {
	layout.stride = 20;
	layout.normalOffset = 3;
	layout.tangentOffset = 6;
	layout.skinOffset = 10;
	layout.uvOffset = 18;
	return layout;
    }

    uint32_t offset = 0;

    layout.positionOffset = offset;
    offset += 3;

    if (attrMask & MeshAttribute_Normal) {
	layout.normalOffset = static_cast<int32_t> (offset);
	offset += 3;
    }

    if (attrMask & MeshAttribute_Tangent) {
	layout.tangentOffset = static_cast<int32_t> (offset);
	offset += 4;
    }

    layout.uvOffset = offset;
    offset += 2;

    if (attrMask & MeshAttribute_LightmapUV) {
	layout.lightmapUVOffset = static_cast<int32_t> (offset);
	offset += 2;
    }

    layout.stride = offset;
    return layout;
}

/**
 * Number of floats a single vertex takes for the given attribute mask or
 * layout tag.
 */
[[nodiscard]] inline uint32_t meshVertexStride (uint32_t attrMask) { return meshVertexLayout (attrMask).stride; }

/**
 * A parsed .mdl model file (MDLV0004/0014/0017/0023 share this container).
 */
struct ModelFile {
    /** Version tag as found in the file, e.g. "MDLV0014" */
    std::string version;
    /**
     * Header format word. Versions without a per-section tag use it as the
     * layout; MDLV0017/0023 override it with the section tag.
     */
    uint32_t headerAttrMask;
    std::vector<MeshSection> sections;
};
} // namespace WallpaperEngine::Data::Model
