#include "WallpaperParser.h"

#include "CameraPathParser.h"
#include "ObjectParser.h"
#include "WallpaperEngine/Data/Builders/UserSettingBuilder.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/FileSystem/Container.h"
#include "WallpaperEngine/Logging/Log.h"

#include <limits>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>

using namespace WallpaperEngine::Data::Parsers;

namespace {
/**
 * Loads a camera path file and normalizes its contents. A malformed or
 * unsupported file must not abort the whole scene.
 */
std::vector<CameraPath> loadCameraPathFile (const Project& project, const std::string& path) {
    try {
	const auto data = JSON::parse (project.assetLocator->readString (path));
	return CameraPathParser::parse (data);
    } catch (const std::exception& e) {
	sLog.error ("Cannot parse camera path file ", path, ": ", e.what ());
	return {};
    }
}

/**
 * Finds the camera object that drives a 3D scene. Wallpaper Engine selects the
 * last visible "default" camera object; if none is active the top-level camera
 * block remains in use.
 * Runtime user-property switching is handled later.
 */
const CameraObject* findActiveCameraObject (const ObjectList& objects) {
    const CameraObject* activeCamera = nullptr;

    for (const auto& object : objects) {
	if (!object->is<CameraObject> ()) {
	    continue;
	}

	const auto* camera = object->as<CameraObject> ();

	if (camera->camera != "default" || !camera->groupVisible->value->getBool ()) {
	    continue;
	}

	activeCamera = camera;
    }

    return activeCamera;
}
} // namespace

WallpaperUniquePtr WallpaperParser::parse (const JSON& file, Project& project) {
    switch (project.type) {
	case Project::Type_Scene:
	    return parseScene (file, project);
	case Project::Type_Video:
	    return parseVideo (file, project);
	case Project::Type_Web:
	    return parseWeb (file, project);
	default:
	    sLog.exception ("Unexpected project type value found... This is likely a bug");
    }
}

bool WallpaperParser::isPerspectiveScene (const JSON& general) {
    const auto projection = general.find ("orthogonalprojection");
    return projection == general.end () || projection->is_null () || !projection->is_object ();
}

SceneUniquePtr WallpaperParser::parseScene (const JSON& file, Project& project) {
    const auto scene = JSON::parse (project.assetLocator->readString (file));
    const auto camera = scene.require ("camera", "Scenes must have a camera section");
    const auto general = scene.require ("general", "Scenes must have a general section");
    const bool isPerspective = isPerspectiveScene (general);
    const auto projectionIt = general.find ("orthogonalprojection");
    const auto projection = isPerspective ? JSON::object () : *projectionIt;
    const auto objects = scene.require ("objects", "Scenes must have an objects section");
    const auto& properties = project.properties;
    // 3D scenes author the projection values in "general", 2D scenes in "camera"
    const JSON& projectionSource = isPerspective ? general : camera;

    auto parsedObjects = parseObjects (objects, project);

    // camera paths drive the camera of 3D scenes, each entry points to a path file
    auto cameraPaths = parseCameraPaths (camera, project);

    auto eye = camera.require<glm::vec3> ("eye", "Camera must have an eye position");
    auto center = camera.require<glm::vec3> ("center", "Camera must have a center position");
    auto up = camera.require<glm::vec3> ("up", "Camera must have an up position");
    auto fov = projectionSource.user ("fov", properties, 50.0f);

    // 3D scenes can carry a dedicated camera object. Its origin/angles define
    // the runtime pose, and its fov overrides the general section. Wallpaper
    // Engine ignores the top-level camera block (editor viewport) in that case.
    if (isPerspective) {
	if (const CameraObject* cameraObject = findActiveCameraObject (parsedObjects); cameraObject != nullptr) {
	    const auto origin = cameraObject->origin->value->getVec3 ();
	    const auto angles = cameraObject->groupAngles->value->getVec3 ();

	    // same rotation order as object transforms: Rz * Ry * Rx
	    glm::mat4 rotation = glm::rotate (glm::mat4 (1.0f), angles.z, glm::vec3 (0.0f, 0.0f, 1.0f));
	    rotation = glm::rotate (rotation, angles.y, glm::vec3 (0.0f, 1.0f, 0.0f));
	    rotation = glm::rotate (rotation, angles.x, glm::vec3 (1.0f, 0.0f, 0.0f));

	    // the camera object looks down -Z with +Y up in its own space
	    eye = origin;
	    center = origin + glm::vec3 (rotation * glm::vec4 (0.0f, 0.0f, -1.0f, 0.0f));
	    up = glm::vec3 (rotation * glm::vec4 (0.0f, 1.0f, 0.0f, 0.0f));

	    // The camera object owns its fov setting. Copy its current value for
	    // now; camera-object property animation is not wired into Camera yet.
	    if (cameraObject->hasFov) {
		fov = WallpaperEngine::Data::Builders::UserSettingBuilder::fromValue (
		    cameraObject->fov->value->getFloat ()
		);
	    }

	    if (!cameraObject->path.empty ()) {
		auto paths = loadCameraPathFile (project, cameraObject->path);

		if (!paths.empty ()) {
		    // An active camera object owns the camera animation; do not
		    // keep the top-level editor paths playing alongside it.
		    cameraPaths.clear ();
		    cameraPaths.push_back (
			CameraPathSource {
			    .objectId = cameraObject->id,
			    .queueMode = cameraObject->queueMode,
			    .paths = std::move (paths),
			}
		    );
		}
	    }
	}
    }

    // TODO: FIND IF THESE DEFAULTS ARE SENSIBLE OR NOT AND PERFORM PROPER VALIDATION WHEN CAMERA PREVIEW AND CAMERA
    // PARALLAX ARE PRESENT

    return std::make_unique <Scene> (
        WallpaperData {
            .filename = "",
            .project = project
        }, SceneData {
            .colors = {
                .ambient  = general.user ("ambientcolor", properties, glm::vec3 (0.0f)),
                .skylight = general.user ("skylightcolor", properties, glm::vec3 (0.0f)),
                .clear = general.user ("clearcolor", properties, glm::vec3 (1.0f)),
            },
            .camera = {
                .fade = general.user ("camerafade", properties, false),
                .preview = general.optional ("camerapreview", false),
                .bloom = {
                    .enabled = general.user ("bloom", properties, false),
                    .strength = general.user ("bloomstrength", properties, 0.0f),
                    .threshold = general.user ("bloomthreshold", properties, 0.0f),
                },
                .parallax = {
                    .enabled = general.user ("cameraparallax", properties, false),
                    .amount = general.user ("cameraparallaxamount", properties, 1.0f),
                    .delay = general.user ("cameraparallaxdelay", properties, 0.0f),
                    .mouseInfluence = general.user ("cameraparallaxmouseinfluence", properties, 1.0f),
                },
                .shake = {
                    .enabled = general.user ("camerashake", properties, false),
                    .amplitude = general.user ("camerashakeamplitude", properties, 0.0f),
                    .roughness = general.user ("camerashakeroughness", properties, 0.0f),
                    .speed = general.user ("camerashakespeed", properties, 0.0f),
                },
                .configuration = {
                    .center = center,
                    .eye = eye,
                    .up = up,
                },
                .projection = {
                    .width  = isPerspective || projection.optional ("auto", false) ? 0 : projection.require <int> ("width",  "Projection must have a width"),
                    .height = isPerspective || projection.optional ("auto", false) ? 0 : projection.require <int> ("height", "Projection must have a height"),
                    .isAuto = !isPerspective && projection.optional ("auto", false),
                    .isPerspective = isPerspective,
                    .nearz = projectionSource.user ("nearz", properties, 0.0f),
                    .farz = projectionSource.user ("farz", properties, 1000.0f),
                    .fov = std::move (fov)
                },
                .paths = std::move (cameraPaths),
            },
            .objects = std::move (parsedObjects),
        }
    );
}

std::vector<CameraPathSource> WallpaperParser::parseCameraPaths (const JSON& camera, const Project& project) {
    std::vector<CameraPathSource> result = {};

    const auto pathsIt = camera.find ("paths");

    if (pathsIt == camera.end () || !pathsIt->is_array ()) {
	return result;
    }

    for (const auto& pathFile : *pathsIt) {
	if (!pathFile.is_string ()) {
	    continue;
	}

	auto paths = loadCameraPathFile (project, pathFile.get<std::string> ());

	if (paths.empty ()) {
	    continue;
	}

	result.push_back (
	    CameraPathSource {
		.objectId = std::nullopt,
		.queueMode = "sequence",
		.paths = std::move (paths),
	    }
	);
    }

    return result;
}

VideoUniquePtr WallpaperParser::parseVideo (const JSON& file, Project& project) {
    return std::make_unique<Video> (WallpaperData { .filename = file, .project = project });
}

WebUniquePtr WallpaperParser::parseWeb (const JSON& file, Project& project) {
    return std::make_unique<Web> (WallpaperData {
	.filename = file,
	.project = project,
    });
}

ObjectList WallpaperParser::parseObjects (const JSON& objects, const Project& project) {
    ObjectList result = {};
    // Keep synthetic ids away from authored ids and CScene's bloom object (-1).
    int syntheticObjectId = std::numeric_limits<int>::min ();

    for (const auto& cur : objects) {
	result.emplace_back (ObjectParser::parse (cur, project, syntheticObjectId));
    }

    return result;
}