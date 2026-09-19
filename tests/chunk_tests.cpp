#include "nfsu/chunk.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

void append_u32_le(std::vector<unsigned char>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

bool write_file(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

int fail(const char* message) {
    std::cerr << "test failure: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    const fs::path path = fs::temp_directory_path() / "nfsu_chunk_test.bin";

    std::vector<unsigned char> bytes;
    append_u32_le(bytes, 0x80000001U);
    append_u32_le(bytes, 24U);
    append_u32_le(bytes, 0x00000011U);
    append_u32_le(bytes, 4U);
    bytes.insert(bytes.end(), {1U, 2U, 3U, 4U});
    append_u32_le(bytes, 0x00000022U);
    append_u32_le(bytes, 4U);
    bytes.insert(bytes.end(), {5U, 6U, 7U, 8U});

    if (!write_file(path, bytes)) {
        return fail("could not write valid fixture");
    }

    const nfsu::ChunkReport valid = nfsu::inspect_chunk_file(path);
    if (!valid.ok()) {
        return fail("valid tree was rejected");
    }
    if (valid.entries.size() != 3U) {
        return fail("valid tree returned the wrong entry count");
    }
    if (!valid.entries[0].is_container || valid.entries[0].depth != 0U) {
        return fail("root container metadata is wrong");
    }
    if (valid.entries[1].depth != 1U || valid.entries[2].depth != 1U) {
        return fail("child depth is wrong");
    }

    bytes.pop_back();
    if (!write_file(path, bytes)) {
        return fail("could not write truncated fixture");
    }

    const nfsu::ChunkReport truncated = nfsu::inspect_chunk_file(path);
    if (truncated.ok()) {
        return fail("truncated tree was accepted");
    }

    std::error_code remove_error;
    fs::remove(path, remove_error);
    std::cout << "chunk tests passed\n";
    return 0;
}

