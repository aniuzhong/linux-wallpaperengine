#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

#include "Camera.h"
#include "CameraProjection.h"

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
    this->m_lookat = glm::lookAt (this->m_currentEye, this->m_currentCenter, this->m_currentUp);

    // camera path fov/zoom channels drive the perspective projection too;
    // legacy paths fall back to the scene fov and zoom 1.0
    if (!this->m_isOrthogonal) {
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

float Camera::getWidth () const { return this->m_width; }

float Camera::getHeight () const { return this->m_height; }

float Camera::getFov () const { return this->m_camera.projection.fov->value->getFloat (); }

float Camera::getNearZ () const { return this->m_camera.projection.nearz->value->getFloat (); }

float Camera::getFarZ () const { return this->m_camera.projection.farz->value->getFloat (); }

void Camera::setOrthogonalProjection (const float width, const float height) {
    this->m_width = width;
    this->m_height = height;

    float nearz = this->m_camera.projection.nearz->value->getFloat ();
    float farz = this->m_camera.projection.farz->value->getFloat ();

    this->m_projection = glm::ortho<float> (-width / 2.0, width / 2.0, -height / 2.0, height / 2.0, nearz, farz);
    this->m_projection = glm::translate (this->m_projection, this->getEye ());
    this->m_isOrthogonal = true;
    this->m_isYFlipped = false;
}

void Camera::setPerspectiveProjection (const float width, const float height, const bool flipY) {
    this->m_width = width;
    this->m_height = height;
    this->m_isOrthogonal = false;
    this->m_isYFlipped = flipY;

    this->updatePerspectiveProjection (this->getFov (), 1.0f);
}

void Camera::updatePerspectiveProjection (const float fov, const float zoom) {
    const float aspect = this->m_height > 0.0f ? this->m_width / this->m_height : 1.0f;

    this->m_projection = CameraProjection::buildPerspectiveProjection (
	fov, aspect, this->getNearZ (), this->getFarZ (), this->m_isYFlipped, zoom
    );
}
