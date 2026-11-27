#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "WallpaperEngine/Data/Model/ModelFile.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Data/Utils/BufferReader.h"

namespace WallpaperEngine::Data::Parsers {
using namespace WallpaperEngine::Data::Model;
using ReadStreamSharedPtr = WallpaperEngine::Data::Utils::ReadStreamSharedPtr;

/**
 * Parses the MDLV header and mesh sections of an in-memory model buffer.
 * Materials are left as paths; the caller decides whether to resolve them.
 */
bool parseMeshData (WallpaperEngine::Data::Utils::BufferReader& reader, ModelFile& out, std::string& error);

/**
 * Parser for Wallpaper Engine's compiled model files (.mdl).
 *
 * The format is an undocumented binary container; this implementation was
 * built by comparing compiled models against their source .obj files and
 * covers versions MDLV0004, MDLV0014, MDLV0016, MDLV0017 and MDLV0023.
 * Puppet files are parsed completely, including their skeleton and clips.
 */
class MeshParser {
public:
    static ModelFileUniquePtr load (const Project& project, const std::string& filename);
    static ModelFileUniquePtr
    parse (const ReadStreamSharedPtr& file, const Project& project, const std::string& filename);
    /** Parses an in-memory model; materials are only resolved when project is set. */
    static ModelFileUniquePtr
    parseBuffer (const uint8_t* data, size_t size, const Project* project, const std::string& filename);
};
} // namespace WallpaperEngine::Data::Parsers
