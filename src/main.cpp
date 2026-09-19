#include "nfsu/chunk.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kNfsuGeometryId = 0x80134000U;

std::uint16_t read_u16_le(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8U);
}

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string format_bytes(std::uintmax_t bytes) {
    static constexpr std::array<const char*, 5> units{
        "B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream output;
    output << std::fixed << std::setprecision(unit == 0 ? 0 : 2)
           << value << ' ' << units[unit];
    return output.str();
}

bool read_first_chunk_id(const fs::path& path, std::uint32_t& id) {
    std::ifstream stream(path, std::ios::binary);
    std::array<unsigned char, 4> bytes{};
    if (!stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) {
        return false;
    }
    id = read_u32_le(bytes.data());
    return true;
}

struct PeSummary {
    bool valid = false;
    std::uint16_t machine = 0;
    std::uint16_t section_count = 0;
    std::uint32_t timestamp = 0;
};

PeSummary inspect_pe(const fs::path& path) {
    PeSummary result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return result;
    }

    std::array<unsigned char, 64> dos_header{};
    if (!stream.read(
            reinterpret_cast<char*>(dos_header.data()),
            static_cast<std::streamsize>(dos_header.size()))) {
        return result;
    }
    if (dos_header[0] != 'M' || dos_header[1] != 'Z') {
        return result;
    }

    const std::uint32_t pe_offset = read_u32_le(dos_header.data() + 0x3C);
    stream.seekg(static_cast<std::streamoff>(pe_offset), std::ios::beg);

    std::array<unsigned char, 24> pe_header{};
    if (!stream.read(
            reinterpret_cast<char*>(pe_header.data()),
            static_cast<std::streamsize>(pe_header.size()))) {
        return result;
    }
    if (pe_header[0] != 'P' || pe_header[1] != 'E' ||
        pe_header[2] != 0 || pe_header[3] != 0) {
        return result;
    }

    result.valid = true;
    result.machine = read_u16_le(pe_header.data() + 4);
    result.section_count = read_u16_le(pe_header.data() + 6);
    result.timestamp = read_u32_le(pe_header.data() + 8);
    return result;
}

int run_summary(const fs::path& root) {
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        std::cerr << "error: game root is not a directory\n";
        return 2;
    }

    const std::array<const char*, 8> required_directories{
        "Cars", "FrontEnd", "Global", "Languages",
        "Movies", "Sound", "SpeechStreams", "Tracks"};

    bool layout_ok = true;
    for (const char* name : required_directories) {
        if (!fs::is_directory(root / name, error)) {
            std::cerr << "missing directory: " << name << '\n';
            layout_ok = false;
        }
    }

    std::uintmax_t total_bytes = 0;
    std::uintmax_t file_count = 0;
    std::map<std::string, std::uintmax_t> extension_counts;

    fs::recursive_directory_iterator iterator(
        root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            std::cerr << "warning: directory scan skipped an entry\n";
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error)) {
            continue;
        }

        ++file_count;
        total_bytes += iterator->file_size(error);
        std::string extension = lower_ascii(iterator->path().extension().string());
        if (extension.empty()) {
            extension = "<none>";
        }
        ++extension_counts[extension];
    }

    std::uintmax_t car_directories = 0;
    const fs::path cars = root / "Cars";
    if (fs::is_directory(cars, error)) {
        for (const auto& entry : fs::directory_iterator(cars, error)) {
            if (entry.is_directory(error) &&
                fs::is_regular_file(entry.path() / "GEOMETRY.BIN", error)) {
                ++car_directories;
            }
        }
    }

    std::cout << "NFSU installation summary\n";
    std::cout << "root: " << root.string() << '\n';
    std::cout << "files: " << file_count << '\n';
    std::cout << "bytes: " << total_bytes << " ("
              << format_bytes(total_bytes) << ")\n";
    std::cout << "car directories: " << car_directories << '\n';

    const std::array<const char*, 5> tracked_extensions{
        ".bin", ".bun", ".lzc", ".mad", ".abk"};
    for (const char* extension : tracked_extensions) {
        std::cout << extension << ": " << extension_counts[extension] << '\n';
    }

    const PeSummary pe = inspect_pe(root / "speed.exe");
    if (pe.valid) {
        std::cout << "speed.exe machine: 0x" << std::uppercase << std::hex
                  << std::setw(4) << std::setfill('0') << pe.machine
                  << std::dec << '\n';
        std::cout << "speed.exe sections: " << pe.section_count << '\n';
        std::cout << "speed.exe timestamp: " << pe.timestamp << '\n';
    } else {
        std::cout << "speed.exe: unavailable or not PE\n";
        layout_ok = false;
    }

    const fs::path sample_geometry = root / "Cars" / "240SX" / "GEOMETRY.BIN";
    std::uint32_t first_id = 0;
    if (read_first_chunk_id(sample_geometry, first_id)) {
        std::cout << "240SX geometry root: " << nfsu::format_chunk_id(first_id)
                  << '\n';
        if (first_id != kNfsuGeometryId) {
            std::cout << "warning: unexpected NFSU geometry root id\n";
            layout_ok = false;
        }

        const nfsu::ChunkReport report = nfsu::inspect_chunk_file(sample_geometry);
        std::cout << "240SX geometry chunks: " << report.entries.size() << '\n';
        std::cout << "240SX geometry parse: "
                  << (report.ok() ? "ok" : "failed") << '\n';
        if (!report.ok()) {
            const nfsu::ParseIssue& issue = report.issues.front();
            std::cout << "first parse issue at " << issue.offset << ": "
                      << issue.message << '\n';
            layout_ok = false;
        }
    } else {
        std::cout << "240SX geometry: unavailable\n";
        layout_ok = false;
    }

    std::cout << "layout: " << (layout_ok ? "supported" : "needs review")
              << '\n';
    return layout_ok ? 0 : 3;
}

int run_chunks(const fs::path& path, std::size_t display_limit) {
    const nfsu::ChunkReport report = nfsu::inspect_chunk_file(path);
    std::cout << "file: " << path.string() << '\n';
    std::cout << "bytes: " << report.file_size << '\n';
    std::cout << "chunks: " << report.entries.size() << '\n';

    const std::size_t count = std::min(display_limit, report.entries.size());
    for (std::size_t index = 0; index < count; ++index) {
        const nfsu::ChunkEntry& entry = report.entries[index];
        std::cout << std::string(entry.depth * 2, ' ')
                  << entry.offset << ' '
                  << nfsu::format_chunk_id(entry.id) << ' '
                  << entry.payload_size;
        if (entry.is_container) {
            std::cout << " container";
        }
        std::cout << '\n';
    }
    if (count < report.entries.size()) {
        std::cout << "... " << (report.entries.size() - count)
                  << " more chunks\n";
    }

    for (const nfsu::ParseIssue& issue : report.issues) {
        std::cerr << "parse issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    return report.ok() ? 0 : 3;
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  nfsu_probe summary <game-root>\n"
        << "  nfsu_probe chunks <file> [display-limit]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    const std::string command = argv[1];
    if (command == "summary") {
        return run_summary(fs::path(argv[2]));
    }
    if (command == "chunks") {
        std::size_t limit = 128;
        if (argc >= 4) {
            try {
                limit = static_cast<std::size_t>(std::stoull(argv[3]));
            } catch (const std::exception&) {
                std::cerr << "error: display-limit must be an integer\n";
                return 1;
            }
        }
        return run_chunks(fs::path(argv[2]), limit);
    }

    print_usage();
    return 1;
}

