#pragma once

#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/Animation.h"

namespace WallpaperEngine::Data::Parsers {
class AnimationParser {
public:
    [[nodiscard]] static Model::Animation parse (const WallpaperEngine::Data::JSON::JSON& data);
};
} // namespace WallpaperEngine::Data::Parsers
