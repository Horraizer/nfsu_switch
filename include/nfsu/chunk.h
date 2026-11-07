#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

struct ChunkEntry {
    std::uint32_t id = 0;
    std::uint32_t payload_size = 0;
    std::uint64_t offset = 0;
    std::size_t depth = 0;
    bool is_container = false;
};

struct ParseIssue {
    std::uint64_t offset = 0;
    std::string message;
};

struct ChunkReport {
    std::uint64_t file_size = 0;
    std::vector<ChunkEntry> entries;
    std::vector<ParseIssue> issues;

    bool ok() const noexcept { return issues.empty(); }
};

ChunkReport inspect_chunk_file(
    const std::filesystem::path& path,
    std::size_t max_depth = 64,
    std::size_t max_entries = 200000);

std::string format_chunk_id(std::uint32_t id);

} // namespace nfsu

