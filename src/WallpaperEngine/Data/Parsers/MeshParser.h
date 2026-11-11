#pragma once

#include "WallpaperEngine/Data/Model/ModelFile.h"
#include "WallpaperEngine/Data/Model/Types.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"

namespace WallpaperEngine::Data::Parsers {
using namespace WallpaperEngine::Data::Model;
using ReadStreamSharedPtr = WallpaperEngine::Data::Utils::ReadStreamSharedPtr;

/**
 * Parser for Wallpaper Engine's compiled model files (.mdl).
 *
 * The format is an undocumented binary container; this implementation was
 * built by comparing compiled models against their source .obj files and
 * covers versions MDLV0004, MDLV0014, MDLV0017 and MDLV0023.
 */
class MeshParser {
public:
    static ModelFileUniquePtr load (const Project& project, const std::string& filename);
    static ModelFileUniquePtr
    parse (const ReadStreamSharedPtr& file, const Project& project, const std::string& filename);
};
} // namespace WallpaperEngine::Data::Parsers
