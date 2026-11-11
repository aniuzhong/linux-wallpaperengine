#include "MaterialParser.h"

#include "TextureParser.h"
#include "WallpaperEngine/Data/Model/Material.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Parsers/ShaderConstantParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Model;

MaterialUniquePtr MaterialParser::load (const Project& project, const std::string& filename) {
    // Wallpaper Engine's json dialect allows comments (often used to
    // disable keys), let the JSON parser ignore them.
    const auto materialJson = JSON::parse (project.assetLocator->readString (filename), nullptr, true, true);

    return parse (materialJson, filename, project);
}

MaterialUniquePtr MaterialParser::parse (const JSON& it, const std::string& filename, const Project& project) {
    return std::make_unique<Material> (Material {
	.filename = filename,
	.passes = parsePasses (it.require ("passes", "Material must have passes to render"), project),
    });
}

std::vector<MaterialPassUniquePtr> MaterialParser::parsePasses (const JSON& it, const Project& project) {
    std::vector<MaterialPassUniquePtr> result = {};

    if (!it.is_array ()) {
	return result;
    }

    for (const auto& cur : it) {
	result.push_back (parsePass (cur, project));
    }

    return result;
}

MaterialPassUniquePtr MaterialParser::parsePass (const JSON& it, const Project& project) {
    const auto textures = it.optional ("textures");
    const auto usertextures = it.optional ("usertextures");
    const auto combos = it.optional ("combos");
    const auto constants = it.optional ("constantshadervalues");
    const auto userShaderValues = it.optional ("usershadervalues");

    std::map<std::string, std::string> parsedUserShaderValues = {};

    if (userShaderValues.has_value () && userShaderValues->is_object ()) {
	for (const auto& cur : userShaderValues->items ()) {
	    if (cur.value ().is_string ()) {
		parsedUserShaderValues.emplace (cur.key (), cur.value ().get<std::string> ());
	    }
	}
    }

    // depthtest/depthwrite/cullmode stay Unknown when the material does not
    // specify them; model objects enable depth and backface culling for those
    // while 2d content keeps the legacy disabled behavior, explicit values are
    // always respected
    // WE writes the depth keys in two spellings, depthtest/depthwrite is the
    // common form while depthtesting/depthwriting shows up in e.g. ricepod's
    // orbital materials; an unparsed alternate spelling would silently flip
    // the pass to the default depth state
    auto depthtestField = it.optional ("depthtest");

    if (!depthtestField.has_value ()) {
	depthtestField = it.optional ("depthtesting");
    }

    auto depthwriteField = it.optional ("depthwrite");

    if (!depthwriteField.has_value ()) {
	depthwriteField = it.optional ("depthwriting");
    }

    const auto cullmodeField = it.optional ("cullmode");

    return std::make_unique<MaterialPass> (MaterialPass {
	// TODO: REMOVE THIS UGLY STD::STRING CREATION
	.blending = parseBlendMode (it.optional ("blending", std::string ("normal"))),
	.cullmode
	= cullmodeField.has_value () ? parseCullMode (cullmodeField->get<std::string> ()) : CullingMode_Unknown,
	.depthtest = depthtestField.has_value () ? parseDepthtestMode (depthtestField->get<std::string> ())
						 : DepthtestMode_Unknown,
	.depthwrite = depthwriteField.has_value () ? parseDepthwriteMode (depthwriteField->get<std::string> ())
						   : DepthwriteMode_Unknown,
	.shader = it.require<std::string> ("shader", "Material pass must have a shader"),
	.textures = textures.has_value () ? TextureParser::parseTextureMap (*textures) : TextureMap {},
	.usertextures = usertextures.has_value () ? TextureParser::parseTextureMap (*usertextures) : TextureMap {},
	.combos = combos.has_value () ? parseCombos (*combos) : ComboMap {},
	.constants = constants.has_value () ? ShaderConstantParser::parse (*constants, project) : ShaderConstantMap {},
	.userShaderValues = std::move (parsedUserShaderValues),
    });
}

std::map<std::string, int> MaterialParser::parseCombos (const JSON& it) {
    std::map<std::string, int> result = {};

    if (!it.is_object ()) {
	return result;
    }

    for (const auto& cur : it.items ()) {
	result.emplace (cur.key (), cur.value ());
    }

    return result;
}

BlendingMode MaterialParser::parseBlendMode (const std::string& mode) {
    if (mode == "normal") {
	return BlendingMode_Normal;
    }

    if (mode == "additive") {
	return BlendingMode_Additive;
    }

    if (mode == "translucent") {
	return BlendingMode_Translucent;
    }

    sLog.error ("Unknown blending mode: ", mode, " defaulting to normal");
    return BlendingMode_Normal;
}

CullingMode MaterialParser::parseCullMode (const std::string& mode) {
    if (mode == "nocull") {
	return CullingMode_Disable;
    }

    if (mode == "normal") {
	return CullingMode_Normal;
    }

    sLog.error ("Unknown culling mode: ", mode, " defaulting to nocull");
    return CullingMode_Disable;
}

DepthtestMode MaterialParser::parseDepthtestMode (const std::string& mode) {
    if (mode == "disabled") {
	return DepthtestMode_Disabled;
    }

    if (mode == "enabled") {
	return DepthtestMode_Enabled;
    }

    sLog.error ("Unknown depthtest mode: ", mode, " defaulting to disabled");
    return DepthtestMode_Disabled;
}

DepthwriteMode MaterialParser::parseDepthwriteMode (const std::string& mode) {
    if (mode == "disabled") {
	return DepthwriteMode_Disabled;
    }

    if (mode == "enabled") {
	return DepthwriteMode_Enabled;
    }

    sLog.error ("Unknown depthwrite mode: ", mode, " defaulting to disabled");
    return DepthwriteMode_Disabled;
}
