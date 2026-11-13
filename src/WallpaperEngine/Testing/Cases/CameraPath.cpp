#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "WallpaperEngine/Assets/AssetLocator.h"
#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Data/Model/CameraPath.h"
#include "WallpaperEngine/Data/Model/Project.h"
#include "WallpaperEngine/Data/Model/Wallpaper.h"
#include "WallpaperEngine/Data/Parsers/CameraPathParser.h"
#include "WallpaperEngine/Data/Parsers/WallpaperParser.h"
#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Data::Model;
using namespace WallpaperEngine::Data::Parsers;
using WallpaperEngine::Assets::AssetLocator;
using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::FileSystem::Container;

TEST_CASE ("Arsenal legacy camera path parses and samples authored keyframes") {
    // Arsenal scripts/camera_00.json path 0: duration 30, two authored transforms.
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "duration": 30,
            "name": "",
            "transforms": [
                {
                    "center": "-2.968 1.777 1.556",
                    "eye": "-3.544 2.168 2.274",
                    "timestamp": 0,
                    "up": "0.000 1.000 0.000"
                },
                {
                    "center": "-2.275 1.020 1.505",
                    "eye": "-2.618 1.308 2.399",
                    "timestamp": 30,
                    "up": "0.000 1.000 0.000"
                }
            ]
        }]
    })json");

    const auto paths = CameraPathParser::parse (data);

    REQUIRE (paths.size () == 1);
    CHECK (paths[0].duration == Catch::Approx (30.0f));

    CameraTransform fallback;
    fallback.fov = 50.0f;
    fallback.zoom = 1.0f;
    const CameraTransform sampled = paths[0].evaluate (15.0f, fallback);

    CHECK (sampled.center.x == Catch::Approx ((-2.968f + -2.275f) / 2.0f));
    CHECK (sampled.center.y == Catch::Approx ((1.777f + 1.020f) / 2.0f));
    CHECK (sampled.center.z == Catch::Approx ((1.556f + 1.505f) / 2.0f));
    CHECK (sampled.eye.x == Catch::Approx ((-3.544f + -2.618f) / 2.0f));
    CHECK (sampled.up.y == Catch::Approx (1.0f));
    CHECK (sampled.fov == Catch::Approx (50.0f));
    CHECK (sampled.zoom == Catch::Approx (1.0f));

    CHECK (paths[0].evaluate (-1.0f, fallback).center.x == Catch::Approx (-2.968f));
    CHECK (paths[0].evaluate (31.0f, fallback).center.x == Catch::Approx (-2.275f));
}

TEST_CASE ("2886819832 (Sonic CD - Special Stage) Lap channel clamps authored keyframes") {
    // 2886819832 scripts/camera_paths_319.json path 7 ("Lap") center.c0:
    // first three of 11 authored keyframes, at 30 fps.
    constexpr float fps = 30.0f;
    const CameraPathChannel channel = {
	.keyframes = {
	    { .time = 0.0f, .value = -2.56211f },
	    { .time = 64.0f / fps, .value = -7.01999f },
	    { .time = 116.0f / fps, .value = -3.5733f },
	},
    };

    CHECK (channel.evaluate (-1.0f, 42.0f) == Catch::Approx (-2.56211f));
    CHECK (channel.evaluate (32.0f / fps, 42.0f) == Catch::Approx ((-2.56211f + -7.01999f) / 2.0f));
    CHECK (channel.evaluate (90.0f / fps, 42.0f) == Catch::Approx ((-7.01999f + -3.5733f) / 2.0f));
    CHECK (channel.evaluate (200.0f / fps, 42.0f) == Catch::Approx (-3.5733f));
}

TEST_CASE ("2886819832 modern camera curve path is detected but not parsed yet") {
    // 2886819832 scripts/camera_paths_319.json path 0 ("Zoom Out"),
    // reduced to the fields that identify a modern curve path.
    const JSON data = JSON::parse (R"json({
        "paths": [{
            "name": "Zoom Out",
            "visible": true,
            "options": {"fps": 30.0, "length": 300, "mode": "single", "cameramode": "fly"},
            "center": {
                "c0": [
                    {"frame": 0, "value": 2.23088, "front": {"enabled": true, "x": 1, "y": 0}},
                    {"frame": 300, "value": 0.90703, "back": {"enabled": true, "x": -1, "y": 0}}
                ]
            },
            "eye": {
                "c0": [
                    {"frame": 0, "value": 1.64439},
                    {"frame": 300, "value": 1.22857}
                ]
            },
            "up": {
                "c1": [
                    {"frame": 0, "value": 1},
                    {"frame": 300, "value": 1}
                ]
            },
            "fov": [
                {"frame": 0, "value": 50},
                {"frame": 300, "value": 50}
            ]
        }]
    })json");

    CHECK (CameraPathParser::parse (data).empty ());
}

TEST_CASE ("2886819832 camera object drives the scene camera pose and fov") {
    // Real CameraObject 319 from 2886819832. CameraObject 483 is invisible and
    // must not win the active-camera selection.
    const std::string sceneFile = R"json({
        "camera": {
            "center": "1.91962 0.22819 -3.03675",
            "eye": "1.91918 0.26950 -4.03589",
            "up": "0 1 0"
        },
        "general": {
            "ambientcolor": "0 0 0",
            "clearcolor": "0 0 0",
            "orthogonalprojection": null,
            "skylightcolor": "0 0 0"
        },
        "objects": [
            {
                "id": 319,
                "name": "Dynamic Cam",
                "camera": "default",
                "path": "scripts/camera_paths_319.json",
                "queuemode": "random",
                "fov": 50.0,
                "zoom": 1.0,
                "origin": "0.00000 0.71910 0.00000",
                "angles": "0.00000 3.14159 0.00000",
                "visible": {
                    "user": {"condition": "0", "name": "cameratype"},
                    "value": true
                }
            },
            {
                "id": 483,
                "name": "Sonic Cam",
                "camera": "default",
                "path": "scripts/camera_paths_483.json",
                "queuemode": "random",
                "fov": 39.48,
                "zoom": 1.0,
                "origin": "1.94643 0.31207 -4.58597",
                "angles": "-3.07859 -0.08959 3.14159",
                "visible": {
                    "user": {"condition": "1", "name": "cameratype"},
                    "value": false
                }
            }
        ]
    })json";

    auto container = std::make_unique<Container> ();
    container->getVFS ().add ("scene.json", sceneFile);
    container->getVFS ().add ("scripts/camera_paths_319.json", std::string ("{\"paths\":[]}"));
    container->getVFS ().add ("scripts/camera_paths_483.json", std::string ("{\"paths\":[]}"));

    Project project {
	.title = "Sonic CD - Special Stage",
	.type = Project::Type_Scene,
	.workshopId = "",
	.supportsAudioProcessing = false,
	.properties = {},
	.wallpaper = nullptr,
	.assetLocator = std::make_unique<AssetLocator> (std::move (container)),
    };

    const auto wallpaper = WallpaperParser::parse ("scene.json", project);

    REQUIRE (wallpaper->is<Scene> ());
    const auto& scene = *wallpaper->as<Scene> ();

    CHECK (scene.camera.configuration.eye.x == Catch::Approx (0.0f));
    CHECK (scene.camera.configuration.eye.y == Catch::Approx (0.7191f));
    CHECK (scene.camera.configuration.center.z == Catch::Approx (1.0f));
    CHECK (scene.camera.configuration.up.y == Catch::Approx (1.0f));
    CHECK (scene.camera.projection.fov->value->getFloat () == Catch::Approx (50.0f));
    CHECK (scene.camera.paths.empty ());
}
