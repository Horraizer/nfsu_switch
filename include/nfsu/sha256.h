#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace nfsu {

// SHA-256, as FIPS 180-4 defines it, over bytes in memory.
//
// Every reference frame this project pins is named by the SHA-256 of its
// TGA file, printed by a host tool from the file on disk. A program that
// writes no file - the Switch application writes nothing on the SD card -
// needs its own digest to report the same number, and this is it. Standard
// library only, integer arithmetic only, and no state outside the object.
using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256() noexcept;

    // Appends size bytes at data to the message; data may be null when size
    // is zero. Any number of calls, of any sizes, give the digest of their
    // concatenation.
    void update(const void* data, std::size_t size) noexcept;

    // Pads the message as FIPS 180-4 section 5.1.1 says, returns its digest
    // and leaves the object holding a new, empty message.
    Sha256Digest finish() noexcept;

private:
    void reset() noexcept;
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_used_ = 0;
    // Message length in bytes, modulo 2^64. The padding stores it in bits,
    // modulo 2^64, which is what the standard asks of a message shorter
    // than 2^61 bytes.
    std::uint64_t length_ = 0;
};

// The digest of one buffer.
Sha256Digest sha256(const void* data, std::size_t size) noexcept;

// The 64 hexadecimal digits of a digest, upper case: the spelling docs/
// uses for every pinned frame, so a printed value can be compared with a
// pinned one as text.
std::string sha256_hex(const Sha256Digest& digest);

} // namespace nfsu
