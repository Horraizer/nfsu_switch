#include "nfsu/image.h"

#include <array>
#include <cstddef>
#include <ostream>

namespace nfsu {

bool write_tga(
    std::ostream& output,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgba,
    std::string& error) {
    if (width == 0 || height == 0 || width > 0xFFFFU || height > 0xFFFFU) {
        error = "TGA dimensions must be in 1..65535";
        return false;
    }
    if (rgba.size() != std::size_t{width} * height * 4U) {
        error = "TGA pixel buffer size does not match the dimensions";
        return false;
    }

    std::array<unsigned char, 18> header{};
    header[2] = 2;
    header[12] = static_cast<unsigned char>(width);
    header[13] = static_cast<unsigned char>(width >> 8U);
    header[14] = static_cast<unsigned char>(height);
    header[15] = static_cast<unsigned char>(height >> 8U);
    header[16] = 32;
    header[17] = 0x28;
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));

    std::vector<unsigned char> row(std::size_t{width} * 4U);
    for (std::size_t y = 0; y < height; ++y) {
        const unsigned char* source = &rgba[y * width * 4U];
        for (std::size_t x = 0; x < width; ++x) {
            row[x * 4U + 0] = source[x * 4U + 2];
            row[x * 4U + 1] = source[x * 4U + 1];
            row[x * 4U + 2] = source[x * 4U + 0];
            row[x * 4U + 3] = source[x * 4U + 3];
        }
        output.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size()));
    }
    output.flush();
    if (!output) {
        error = "failed to write TGA output";
        return false;
    }
    return true;
}

} // namespace nfsu
