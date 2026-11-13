#include <fstream>
#include <memory>

#include "Package.h"

#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Data/Parsers/PackageParser.h"
#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Data/Utils/MemoryStream.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

using namespace WallpaperEngine::FileSystem;
using namespace WallpaperEngine::FileSystem::Adapters;

namespace {
/**
 * Wallpaper Engine assets are authored on Windows, where paths are
 * case-insensitive. Package entries therefore do not always match the case
 * used by scene/effect JSON files; fall back to an ASCII case-insensitive
 * comparison when an exact match is not found.
 */
bool equalsIgnoreCase (std::string_view left, std::string_view right) {
    return std::ranges::equal (left, right, [] (char a, char b) {
	return std::tolower (static_cast<unsigned char> (a)) == std::tolower (static_cast<unsigned char> (b));
    });
}

const FileEntry* findEntry (const Package& package, const std::string& path) {
    const FileEntry* fallback = nullptr;

    for (const auto& file : package.files) {
	if (file->filename == path) {
	    return file.get ();
	}

	if (fallback == nullptr && equalsIgnoreCase (file->filename, path)) {
	    fallback = file.get ();
	}
    }

    return fallback;
}
} // namespace

ReadStreamSharedPtr PackageAdapter::open (const std::filesystem::path& path) const {
    const FileEntry* entry = findEntry (*this->package, path.string ());

    if (entry == nullptr) {
	throw std::filesystem::filesystem_error ("Cannot find file", path, std::error_code ());
    }

    // read file into memory
    auto buffer = std::make_unique<char[]> (entry->length);

    // go to the file's position and read into the buffer
    this->package->file->base ().seekg (entry->offset + this->package->baseOffset, std::ios::beg);
    this->package->file->next (buffer.get (), entry->length);

    // create a memory stream and return that
    return std::make_shared<MemoryStream> (std::move (buffer), entry->length);
}

bool PackageAdapter::exists (const std::filesystem::path& path) const {
    return findEntry (*this->package, path.string ()) != nullptr;
}

std::filesystem::path PackageAdapter::physicalPath (const std::filesystem::path& path) const {
    throw std::filesystem::filesystem_error ("Package adapter does not support realpath", path, std::error_code ());
}

bool PackageFactory::handlesMountpoint (const std::filesystem::path& path) const {
    try {
	const auto finalpath = std::filesystem::canonical (path);
	const auto status = std::filesystem::status (finalpath);

	return std::filesystem::exists (finalpath) && std::filesystem::is_regular_file (status)
	    && finalpath.extension () == ".pkg";
    } catch (std::filesystem::filesystem_error&) {
	return false;
    }
}

AdapterSharedPtr PackageFactory::create (const std::filesystem::path& path) const {
    const auto stream = std::make_shared<std::ifstream> (path, std::ios::binary);
    auto package = Data::Parsers::PackageParser::parse (stream);

    return std::make_unique<PackageAdapter> (std::move (package));
}
