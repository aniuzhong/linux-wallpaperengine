#pragma once

#include "WallpaperEngine/Data/Model/Object.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace WallpaperEngine::Render::Utils {
/**
 * Composition layers are passthrough images whose material samples the scene
 * and whose effects receive child objects as their input. The model filename is
 * used here instead of `passthrough` alone because fullscreen layers are
 * passthrough too but do not composite children.
 */
inline bool isCompositionLayerModel (const Data::Model::ModelStruct& model) {
    if (!model.passthrough) {
	return false;
    }

    std::string filename = model.filename;
    std::ranges::transform (filename, filename.begin (), [] (const unsigned char c) {
	return static_cast<char> (std::tolower (c));
    });
    std::ranges::replace (filename, '\\', '/');

    return filename == "models/util/composelayer.json" || filename == "models/util/composelayer_depthtest.json"
	|| filename == "models/util/projectlayer.json";
}

inline bool isCompositionLayer (const Data::Model::Image& image) {
    return image.model != nullptr && isCompositionLayerModel (*image.model);
}

inline bool isCompositionLayer (const Data::Model::Object& object) {
    return object.is<Data::Model::Image> () && isCompositionLayer (*object.as<Data::Model::Image> ());
}

inline bool isTintShader (const std::string& shader) { return shader.find ("tint") != std::string::npos; }

/** Whether any pass of the effect uses the built-in tint shader. */
inline bool isTintEffect (const Data::Model::ImageEffect& effect) {
    if (effect.effect == nullptr) {
	return false;
    }

    return std::ranges::any_of (effect.effect->passes, [] (const auto& pass) {
	return pass->material.has_value ()
	    && std::ranges::any_of (pass->material.value ()->passes, [] (const auto& materialPass) {
		   return isTintShader (materialPass->shader);
	       });
    });
}

/**
 * A tint-only effect is an effect made exclusively of tint material passes.
 * Any copy/command pass or non-tint material pass disqualifies it, otherwise a
 * combined tint+blur effect would incorrectly take the no-capture shortcut.
 */
inline bool isTintOnlyEffect (const Data::Model::ImageEffect& effect) {
    if (effect.effect == nullptr || effect.effect->passes.empty ()) {
	return false;
    }

    for (const auto& pass : effect.effect->passes) {
	if (!pass->material.has_value () || pass->material.value ()->passes.empty ()) {
	    return false;
	}

	for (const auto& materialPass : pass->material.value ()->passes) {
	    if (!isTintShader (materialPass->shader)) {
		return false;
	    }
	}
    }

    return true;
}

inline bool hasVisibleEffects (const Data::Model::Image& image) {
    return std::ranges::any_of (image.effects, [] (const auto& effect) {
	return effect != nullptr && effect->visible != nullptr && effect->visible->value->getBool ()
	    && effect->effect != nullptr;
    });
}

/** Whether every visible effect is a pure tint effect. */
inline bool isTintOnlyCompositionLayer (const Data::Model::Image& image) {
    bool hasVisibleEffect = false;

    for (const auto& effect : image.effects) {
	if (effect == nullptr || effect->visible == nullptr || !effect->visible->value->getBool ()) {
	    continue;
	}

	hasVisibleEffect = true;

	if (!isTintOnlyEffect (*effect)) {
	    return false;
	}
    }

    return hasVisibleEffect;
}
} // namespace WallpaperEngine::Render::Utils
