#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "WallpaperEngine/Media/MediaSource.h"

using namespace WallpaperEngine::Media;

namespace {
class CountingMediaSource final : public MediaSource {
public:
    explicit CountingMediaSource (std::chrono::milliseconds interval) : MediaSource (interval) { }

    int updateCount = 0;

protected:
    void performUpdate () override { ++this->updateCount; }
};
} // namespace

TEST_CASE ("MediaSource update honours its update interval") {
    CountingMediaSource source (std::chrono::seconds (60));

    // Simulate a render loop: update() is called every frame, but the source
    // should only actually poll once per interval.
    for (int i = 0; i < 1000; ++i) {
	source.update ();
    }

    CHECK (source.updateCount == 1);
}
