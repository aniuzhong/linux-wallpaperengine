#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace WallpaperEngine::Data::Model {
/**
 * One side of a timeline animation's Bezier handle.
 *
 * Wallpaper Engine normalises the time coordinates: `x` is relative to half
 * the segment duration, while `y` is a value offset from the keyframe.
 */
struct AnimationHandle {
    bool enabled = false;
    bool magic = false;
    float x = 0.0f;
    float y = 0.0f;
};

/** A single keyframe of one timeline animation channel. */
struct AnimationKeyframe {
    float frame = 0.0f;
    float value = 0.0f;
    AnimationHandle front = {};
    AnimationHandle back = {};
    bool step = false;
    bool lockAngle = false;
    bool lockLength = false;
};

/** One scalar timeline animation channel (x, y or z of a vector property). */
struct AnimationChannel {
    std::vector<AnimationKeyframe> keyframes = {};

    /**
     * Evaluates this channel at a frame.
     *
     * @param wrapLength Loop length in frames, or 0 when the channel should
     *                   clamp at its first/last keyframe.
     */
    [[nodiscard]] float evaluate (float frame, float fallback, float wrapLength = 0.0f) const;
};

enum class AnimationMode {
    Single = 0,
    Loop = 1,
    Mirror = 2,
};

struct AnimationEvent {
    float frame = 0.0f;
    std::string name;
};

/**
 * A timeline animation bound to a UserSetting.
 *
 * Wallpaper Engine stores these under the `animation` key of a property:
 * three value channels (c0/c1/c2), a frame rate, a frame length and playback
 * options. Animations that are not explicitly paused are auto-played; named
 * playback and timeline events are not wired to SceneScript yet.
 */
struct Animation {
    std::array<AnimationChannel, 3> channels = {};
    float length = 0.0f;
    float fps = 30.0f;
    AnimationMode mode = AnimationMode::Single;
    bool relative = false;
    bool startPaused = false;
    bool wrapLoop = false;
    std::string name;
    std::vector<AnimationEvent> events = {};

    [[nodiscard]] float duration () const;
    [[nodiscard]] bool empty () const;
    /**
     * Evaluates the animation at `seconds` since it started.
     *
     * @param fallback Value used for channels without keyframes. Callers pass
     *                 the setting's current value for absolute animations and
     *                 zero for relative animations.
     */
    [[nodiscard]] glm::vec3 evaluate (float seconds, const glm::vec3& fallback) const;
};
} // namespace WallpaperEngine::Data::Model
