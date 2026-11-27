#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

#include "Camera.h"
#include "CameraProjection.h"
#include "WallpaperEngine/Logging/Log.h"

using namespace WallpaperEngine;
using namespace WallpaperEngine::Render;

Camera::Camera (Wallpapers::CScene& scene, const SceneData::Camera& camera) :
    m_width (0), m_height (0), m_camera (camera), m_scene (scene), m_pathStart (std::chrono::steady_clock::now ()),
    m_currentEye (camera.configuration.eye), m_currentCenter (camera.configuration.center),
    m_currentUp (camera.configuration.up) {
    // get the lookat position
    // TODO: ENSURE THIS IS ONLY USED WHEN NOT DOING AN ORTOGRAPHIC CAMERA AS IT THROWS OFF POINTS
    this->m_lookat = glm::lookAt (this->getEye (), this->getCenter (), this->getUp ());

    for (const auto& source : this->m_camera.paths) {
	for (const auto& path : source.paths) {
	    this->m_totalPathDuration += path.duration;
	}
    }
}

void Camera::tick () {
    if (this->m_camera.paths.empty () || this->m_totalPathDuration <= 0.0f) {
	return;
    }

    // paths play back to back and loop as a whole
    const float elapsed = std::chrono::duration<float> (std::chrono::steady_clock::now () - this->m_pathStart).count ();
    float pathTime = std::fmod (elapsed, this->m_totalPathDuration);

    // find the path holding this time slice
    const CameraPath* currentPath = nullptr;
    const CameraPath* lastPath = nullptr;

    for (const auto& source : this->m_camera.paths) {
	for (const auto& path : source.paths) {
	    lastPath = &path;

	    if (pathTime < path.duration) {
		currentPath = &path;
		break;
	    }

	    pathTime -= path.duration;
	}

	if (currentPath != nullptr) {
	    break;
	}
    }

    if (currentPath == nullptr) {
	currentPath = lastPath;
    }

    if (currentPath == nullptr) {
	return;
    }

    const CameraTransform fallback = {
	.center = this->m_currentCenter,
	.eye = this->m_currentEye,
	.up = this->m_currentUp,
	.fov = this->getFov (),
	.zoom = 1.0f,
    };
    const CameraTransform sample = currentPath->evaluate (pathTime, fallback);

    // store the interpolated values so uniforms that follow the camera
    // (g_EyePosition, skybox shaders) stay in sync with the animation
    this->m_currentEye = sample.eye;
    this->m_currentCenter = sample.center;
    this->m_currentUp = sample.up;
    this->m_currentZoom = sample.zoom;
    this->m_lookat = glm::lookAt (this->m_currentEye, this->m_currentCenter, this->m_currentUp);

    // camera path fov/zoom channels drive both projections; legacy paths fall
    // back to the scene fov and zoom 1.0. 2D and 3D use different projection
    // builders, but the zoom channel is shared by Wallpaper Engine.
    if (this->m_isOrthogonal) {
	this->updateOrthogonalProjection ();
    } else {
	this->updatePerspectiveProjection (sample.fov, sample.zoom);
    }
}

Camera::~Camera () = default;

const glm::vec3& Camera::getCenter () const { return this->m_currentCenter; }

const glm::vec3& Camera::getEye () const { return this->m_currentEye; }

const glm::vec3& Camera::getUp () const { return this->m_currentUp; }

const glm::mat4& Camera::getProjection () const { return this->m_projection; }

const glm::mat4& Camera::getLookAt () const { return this->m_lookat; }

bool Camera::isOrthogonal () const { return this->m_isOrthogonal; }

bool Camera::isYFlipped () const { return this->m_isYFlipped; }

Wallpapers::CScene& Camera::getScene () const { return this->m_scene; }

float Camera::getWidth () const {
    // Callers use these values as the world region currently shown on screen
    // (mouse mapping, parallax), so 2D zoom has to affect them.
    return this->m_isOrthogonal ? this->m_width / this->getOrthogonalZoom () : this->m_width;
}

float Camera::getHeight () const {
    return this->m_isOrthogonal ? this->m_height / this->getOrthogonalZoom () : this->m_height;
}

float Camera::getFov () const { return this->m_camera.projection.fov->value->getFloat (); }

float Camera::getNearZ () const { return this->m_camera.projection.nearz->value->getFloat (); }

float Camera::getFarZ () const { return this->m_camera.projection.farz->value->getFloat (); }

float Camera::getZoom () const { return this->m_currentZoom; }

bool Camera::usesCameraPath () const { return !this->m_camera.paths.empty () && this->m_totalPathDuration > 0.0f; }

void Camera::setStaticTransforms (
    const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up, const float zoom
) {
    // SceneScript documentation explicitly says not to mix static camera
    // transforms with camera paths. Paths keep ownership so interactive
    // scripts cannot fight the authored cinematic.
    if (this->usesCameraPath ()) {
	if (!this->m_pathConflictWarned) {
	    sLog.debug ("Ignoring SceneScript camera transforms because a camera path is active");
	    this->m_pathConflictWarned = true;
	}

	return;
    }

    this->m_currentEye = eye;
    this->m_currentCenter = center;

    if (glm::dot (up, up) > 0.000001f) {
	this->m_currentUp = glm::normalize (up);
    }

    // CameraTransforms.zoom is relative: values above 1 zoom in. It applies to
    // both projection modes (the helper scales the perspective axes too). A
    // camera path owns every channel and never reaches this point.
    if (zoom > 0.0f) {
	this->m_currentZoom = zoom;
    }

    this->m_lookat = glm::lookAt (this->m_currentEye, this->m_currentCenter, this->m_currentUp);

    if (this->m_isOrthogonal) {
	this->updateOrthogonalProjection ();
    } else {
	this->updatePerspectiveProjection (this->getFov (), this->m_currentZoom);
    }
}

void Camera::setFov (const float fov) {
    if (this->m_camera.projection.fov == nullptr || this->m_camera.projection.fov->value == nullptr
	|| !std::isfinite (fov)) {
	return;
    }

    const float clamped = glm::clamp (fov, 1.0f, 179.0f);

    // updateCameraFromObject() calls this every frame; avoid rewriting the
    // authored value and rebuilding the projection when nothing changed.
    if (std::abs (this->getFov () - clamped) <= 1e-6f) {
	return;
    }

    this->m_camera.projection.fov->value->update (clamped, DynamicValue::UpdateSource::Script);

    if (!this->m_isOrthogonal) {
	this->updatePerspectiveProjection (clamped, this->m_currentZoom);
    }
}

void Camera::setOrthogonalProjection (const float width, const float height) {
    this->m_width = width;
    this->m_height = height;
    this->m_isOrthogonal = true;
    this->m_isYFlipped = false;

    this->updateOrthogonalProjection ();
}

void Camera::updateOrthogonalProjection () {
    const float nearz = this->m_camera.projection.nearz->value->getFloat ();
    const float farz = this->m_camera.projection.farz->value->getFloat ();

    // The authored eye is the projection's fixed origin. Camera paths and
    // SceneScript move m_currentEye relative to it; using the animated eye here
    // would make the 2D view jump whenever a window resize rebuilds the
    // projection (and would double-apply the camera translation each tick).
    this->m_projection = CameraProjection::buildOrthogonalProjection (
	this->m_width, this->m_height, nearz, farz, this->m_camera.configuration.eye, this->getOrthogonalZoom ()
    );
}

float Camera::getOrthogonalZoom () const { return this->m_currentZoom > 0.0f ? this->m_currentZoom : 1.0f; }

void Camera::setPerspectiveProjection (const float width, const float height, const bool flipY) {
    this->m_width = width;
    this->m_height = height;
    this->m_isOrthogonal = false;
    this->m_isYFlipped = flipY;

    this->updatePerspectiveProjection (this->getFov (), this->m_currentZoom);
}

void Camera::updatePerspectiveProjection (const float fov, const float zoom) {
    const float aspect = this->m_height > 0.0f ? this->m_width / this->m_height : 1.0f;

    this->m_projection = CameraProjection::buildPerspectiveProjection (
	fov, aspect, this->getNearZ (), this->getFarZ (), this->m_isYFlipped, zoom
    );
}
