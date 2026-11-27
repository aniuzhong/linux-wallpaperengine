#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "WallpaperEngine/Data/Model/ModelFile.h"
#include "WallpaperEngine/Data/Utils/BufferReader.h"

namespace WallpaperEngine::Data::Parsers {
using PuppetModel = WallpaperEngine::Data::Model::ModelFile;

/**
 * Parses the MDLS skeleton and MDLA animation sections that follow the mesh
 * sections of a puppet model. Returns true when the sections are absent.
 */
bool parsePuppetData (WallpaperEngine::Data::Utils::BufferReader& reader, PuppetModel& out, std::string& error);

/**
 * Parses a complete Wallpaper Engine puppet model from memory. The mesh is
 * parsed by the shared MeshParser code, then the optional skeleton and
 * animation sections are added. Materials are left unresolved because this
 * entry point has no project context.
 */
bool parsePuppetModel (const uint8_t* data, size_t size, PuppetModel& out);
} // namespace WallpaperEngine::Data::Parsers
