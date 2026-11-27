#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace WallpaperEngine::Data::Utils {
/**
 * Bounds-checked little-endian cursor over an in-memory buffer.
 *
 * Parsers of untrusted binary data should use this instead of raw pointer
 * arithmetic. The optional limit makes it possible to constrain a reader to a
 * section of the containing file.
 */
class BufferReader {
public:
    BufferReader (
	const uint8_t* data, size_t size, size_t position = 0, size_t limit = std::numeric_limits<size_t>::max ()
    ) : m_data (data), m_size (size), m_end (std::min (limit, size)), m_position (std::min (position, this->m_end)) { }

    [[nodiscard]] const uint8_t* data () const { return this->m_data; }
    [[nodiscard]] size_t size () const { return this->m_size; }
    [[nodiscard]] size_t end () const { return this->m_end; }
    [[nodiscard]] size_t pos () const { return this->m_position; }
    [[nodiscard]] size_t remaining () const {
	return this->m_position <= this->m_end ? this->m_end - this->m_position : 0;
    }
    [[nodiscard]] bool has (size_t count) const {
	return this->m_position <= this->m_end && count <= this->m_end - this->m_position;
    }

    void seek (size_t position) { this->m_position = std::min (position, this->m_end); }

    bool skip (size_t count) {
	if (!this->has (count)) {
	    return false;
	}

	this->m_position += count;
	return true;
    }

    bool read (void* out, size_t count) {
	if (!this->has (count)) {
	    return false;
	}

	std::memcpy (out, this->m_data + this->m_position, count);
	this->m_position += count;
	return true;
    }

    bool u8 (uint8_t& out) {
	if (!this->has (1)) {
	    return false;
	}

	out = this->m_data[this->m_position++];
	return true;
    }

    bool u16 (uint16_t& out) {
	if (!this->has (2)) {
	    return false;
	}

	out = static_cast<uint16_t> (this->m_data[this->m_position])
	    | static_cast<uint16_t> (static_cast<uint16_t> (this->m_data[this->m_position + 1]) << 8);
	this->m_position += 2;
	return true;
    }

    bool u32 (uint32_t& out) {
	if (!this->has (4)) {
	    return false;
	}

	out = static_cast<uint32_t> (this->m_data[this->m_position])
	    | (static_cast<uint32_t> (this->m_data[this->m_position + 1]) << 8)
	    | (static_cast<uint32_t> (this->m_data[this->m_position + 2]) << 16)
	    | (static_cast<uint32_t> (this->m_data[this->m_position + 3]) << 24);
	this->m_position += 4;
	return true;
    }

    bool f32 (float& out) {
	uint32_t bits = 0;

	if (!this->u32 (bits)) {
	    return false;
	}

	std::memcpy (&out, &bits, sizeof (out));
	return true;
    }

    bool cstring (std::string& out) {
	if (!this->has (1)) {
	    return false;
	}

	const uint8_t* begin = this->m_data + this->m_position;
	const uint8_t* terminator
	    = static_cast<const uint8_t*> (std::memchr (begin, 0, this->m_end - this->m_position));

	if (terminator == nullptr) {
	    return false;
	}

	out.assign (reinterpret_cast<const char*> (begin), static_cast<size_t> (terminator - begin));
	this->m_position = static_cast<size_t> (terminator - this->m_data) + 1;
	return true;
    }

    [[nodiscard]] bool peekU32 (uint32_t& out) const {
	if (!this->has (4)) {
	    return false;
	}

	out = static_cast<uint32_t> (this->m_data[this->m_position])
	    | (static_cast<uint32_t> (this->m_data[this->m_position + 1]) << 8)
	    | (static_cast<uint32_t> (this->m_data[this->m_position + 2]) << 16)
	    | (static_cast<uint32_t> (this->m_data[this->m_position + 3]) << 24);
	return true;
    }

    [[nodiscard]] bool find (std::string_view needle, size_t from, size_t& out) const {
	if (needle.empty () || from > this->m_end || needle.size () > this->m_end - from) {
	    return false;
	}

	const size_t needleSize = needle.size ();

	for (size_t offset = from; offset + needleSize <= this->m_end; ++offset) {
	    if (std::memcmp (this->m_data + offset, needle.data (), needleSize) == 0) {
		out = offset;
		return true;
	    }
	}

	return false;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_end;
    size_t m_position;
};
} // namespace WallpaperEngine::Data::Utils
