#include "nfsu/chunk.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace nfsu {
namespace {

constexpr std::uint32_t kContainerBit = 0x80000000U;
constexpr std::uint64_t kHeaderSize = 8;

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

class ChunkWalker {
public:
    ChunkWalker(
        std::ifstream& stream,
        ChunkReport& report,
        std::size_t max_depth,
        std::size_t max_entries)
        : stream_(stream),
          report_(report),
          max_depth_(max_depth),
          max_entries_(max_entries) {}

    bool walk(std::uint64_t begin, std::uint64_t end, std::size_t depth) {
        if (depth > max_depth_) {
            add_issue(begin, "maximum chunk depth exceeded");
            return false;
        }

        std::uint64_t offset = begin;
        while (offset < end) {
            if (end - offset < kHeaderSize) {
                add_issue(offset, "trailing bytes are smaller than a chunk header");
                return false;
            }

            if (report_.entries.size() >= max_entries_) {
                add_issue(offset, "maximum chunk count exceeded");
                return false;
            }

            std::array<unsigned char, kHeaderSize> header{};
            stream_.clear();
            stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!stream_.read(
                    reinterpret_cast<char*>(header.data()),
                    static_cast<std::streamsize>(header.size()))) {
                add_issue(offset, "failed to read chunk header");
                return false;
            }

            const std::uint32_t id = read_u32_le(header.data());
            const std::uint32_t payload_size = read_u32_le(header.data() + 4);
            const std::uint64_t payload_offset = offset + kHeaderSize;

            if (payload_size > end - payload_offset) {
                add_issue(offset, "chunk payload extends beyond its parent");
                return false;
            }

            const std::uint64_t next = payload_offset + payload_size;
            const bool is_container = (id & kContainerBit) != 0;
            report_.entries.push_back(
                ChunkEntry{id, payload_size, offset, depth, is_container});

            if (is_container && payload_size != 0) {
                if (!walk(payload_offset, next, depth + 1)) {
                    return false;
                }
            }

            offset = next;
        }

        return true;
    }

private:
    void add_issue(std::uint64_t offset, std::string message) {
        report_.issues.push_back(ParseIssue{offset, std::move(message)});
    }

    std::ifstream& stream_;
    ChunkReport& report_;
    std::size_t max_depth_;
    std::size_t max_entries_;
};

} // namespace

ChunkReport inspect_chunk_file(
    const std::filesystem::path& path,
    std::size_t max_depth,
    std::size_t max_entries) {
    ChunkReport report;

    std::error_code size_error;
    report.file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        report.issues.push_back(ParseIssue{0, "failed to read file size"});
        return report;
    }

    if (report.file_size < kHeaderSize) {
        report.issues.push_back(ParseIssue{0, "file is smaller than a chunk header"});
        return report;
    }

    if (report.file_size >
        static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        report.issues.push_back(ParseIssue{0, "file is too large for this build"});
        return report;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        report.issues.push_back(ParseIssue{0, "failed to open file"});
        return report;
    }

    ChunkWalker walker(stream, report, max_depth, max_entries);
    walker.walk(0, report.file_size, 0);
    return report;
}

std::string format_chunk_id(std::uint32_t id) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << id;
    return output.str();
}

} // namespace nfsu

