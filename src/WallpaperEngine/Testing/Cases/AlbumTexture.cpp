#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

#include "WallpaperEngine/Render/AlbumTexture.h"

using WallpaperEngine::Render::AlbumTexture;

TEST_CASE ("AlbumTexture rejects empty readbacks") {
    CHECK_FALSE (AlbumTexture::readbackSize (0, 10).has_value ());
    CHECK_FALSE (AlbumTexture::readbackSize (10, 0).has_value ());
    CHECK_FALSE (AlbumTexture::readbackSize (0, 0).has_value ());
}

TEST_CASE ("AlbumTexture computes RGBA8 readback sizes") {
    CHECK (AlbumTexture::readbackSize (1, 1).value () == 4);
    CHECK (AlbumTexture::readbackSize (2, 2).value () == 16);
    CHECK (AlbumTexture::readbackSize (256, 256).value () == 256 * 256 * 4);
}

TEST_CASE ("AlbumTexture rejects readbacks that overflow size_t") {
    CHECK_FALSE (
	AlbumTexture::readbackSize (std::numeric_limits<uint32_t>::max (), std::numeric_limits<uint32_t>::max ())
	    .has_value ()
    );
}
