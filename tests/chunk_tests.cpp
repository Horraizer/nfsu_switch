#include "nfsu/chunk.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

// A fixture name no other process shares, built the way
// tests/arg_path_tests.cpp has built one since issue #87 and
// tests/track_object_tests.cpp since issue #122.
//
// This file used to name its fixture "nfsu_chunk_test.bin" under the system
// temporary directory with nothing in the name to make it this process's
// own. CLAUDE.md says several sessions are routinely live on this machine,
// each with its own build directory, so two copies of this binary overlap
// whenever two of them run ctest at the same time - and each one writes
// that single path with std::ios::trunc and removes it again. A copy that
// called inspect_chunk_file while another was between its open and its
// close read a file that was missing, empty or half written, and this test
// then reported a valid tree rejected, or a truncated tree accepted, for a
// reason that had nothing to do with the parser. Issue #124.
std::string unique_fixture(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return "nfsu_chunk_" + label + "_" + std::to_string(pid) + ".bin";
}

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

// The body of the test, with the fixture path handed in so that main can
// remove it on every exit path. Each check below returns early, and before
// issue #124 an early return left the fixture on disk; a fixed name hid
// that, because the next run overwrote it. A per-process name does not, so
// the removal has to be unconditional instead.
int run(const fs::path& path) {
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

    std::cout << "chunk tests passed\n";
    return 0;
}

} // namespace

int main() {
    const fs::path path = fs::temp_directory_path() / unique_fixture("test");
    const int status = run(path);
    // Only the file this process created, by exact name and never by glob.
    std::error_code remove_error;
    fs::remove(path, remove_error);
    return status;
}

