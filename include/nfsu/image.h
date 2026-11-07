#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace nfsu {

// Writes an uncompressed 32-bit TGA with a top-left origin and 8 alpha bits.
bool write_tga(
    std::ostream& output,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgba,
    std::string& error);

} // namespace nfsu
