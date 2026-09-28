#include "nfsu/arg_path.h"
#include "nfsu/assets.h"
#include "nfsu/car_parts.h"
#include "nfsu/chunk.h"
#include "nfsu/drive_session.h"
#include "nfsu/geometry.h"
#include "nfsu/memory_budget.h"
#include "nfsu/part_placement.h"
#include "nfsu/race_events.h"
#include "nfsu/race_route.h"
#include "nfsu/race_timing.h"
#include "nfsu/skin_inputs.h"
#include "nfsu/stock_frame.h"
#include "nfsu/stock_scene.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"
#include "nfsu/texture_export.h"
#include "nfsu/track.h"
#include "nfsu/track_budget.h"
#include "nfsu/track_collision.h"
#include "nfsu/track_contact.h"
#include "nfsu/track_draw_list.h"
#include "nfsu/track_objects.h"
#include "nfsu/track_probe.h"
#include "nfsu/track_render.h"
#include "nfsu/track_residency.h"
#include "nfsu/track_scene.h"
#include "nfsu/track_stream.h"
#include "nfsu/track_visibility.h"
#include "nfsu/track_zones.h"
#include "nfsu/vehicle.h"
#include "nfsu/vehicle_tuning.h"
#include "nfsu/vehicle_track.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kNfsuGeometryId = 0x80134000U;

// track-region: meshes printed before the listing is summarized, and the
// total bytes read for all texture sources of one run.
constexpr std::size_t kTrackMeshListLimit = 64;
constexpr std::uint64_t kMaxTrackTextureInputBytes = 512ULL * 1024ULL * 1024ULL;

// track-objects: sections printed before the listing is summarized, and
// unresolved mesh hashes printed before the same.
constexpr std::size_t kTrackSectionListLimit = 32;
constexpr std::size_t kTrackUnresolvedListLimit = 32;

std::string hex32(std::uint32_t value);

// Decimal, or hexadecimal with a leading "0x" or "0X". The whole argument
// must be consumed and the value must fit in 64 bits.
bool parse_u64(const std::string& text, std::uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    int base = 10;
    std::size_t start = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        start = 2;
    }
    for (std::size_t i = start; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        const bool digit = ch >= '0' && ch <= '9';
        const bool hex = base == 16 && ((ch >= 'a' && ch <= 'f') ||
                                        (ch >= 'A' && ch <= 'F'));
        if (!digit && !hex) {
            return false;
        }
    }
    try {
        std::size_t used = 0;
        const unsigned long long value =
            std::stoull(text.substr(start), &used, base);
        if (used != text.size() - start) {
            return false;
        }
        out = static_cast<std::uint64_t>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Shortest round-trip decimal, the spelling src/viewer_main.cpp uses for
// every camera value it reports. Six significant digits would make two
// cameras that render different frames print the same line.
std::string format_double(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

// The same for a float, over the FLOAT's own shortest round trip rather
// than the widened double's. Every decoded field of a track record is a
// float, and format_double would spell one of them as its exact double
// value - 0.1F reads 0.10000000149011612 - which is a true number that
// no reader can compare against the file.
std::string format_float(float value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

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

// asset-check: whether `root` holds the retail files one set names, each
// found by name without regard to ASCII case, sized, and hashed only when
// its size is right, through the desktop file system service. It prints
// the report the console application shows, with every hash in full, and
// reads nothing the set does not name. Exit 0 when every file matches, 2
// when the root cannot be listed, 3 for any other install.
// docs/SWITCH_ASSETS.md.
int run_asset_check(const fs::path& root, const nfsu::AssetManifest& manifest) {
    nfsu::AssetCheckReport report;
    std::string reason;
    if (!nfsu::check_asset_install(nfsu::desktop_file_system(), root, manifest, report,
                                   reason)) {
        std::cerr << "error: " << reason << '\n';
        return 1;
    }
    const std::string root_text = root.generic_string();
    std::cout << "asset-check: root " << root_text << '\n';
    for (const nfsu::AssetLine& line :
         nfsu::asset_report_lines(report, root_text, nfsu::AssetReportForm::desktop)) {
        std::cout << line.text << '\n';
    }
    if (!report.root_listed) {
        return 2;
    }
    return report.verdict == nfsu::AssetVerdict::complete ? 0 : 3;
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

void print_geometry_issues(const nfsu::GeometryReport& report) {
    for (const nfsu::GeometryIssue& issue : report.issues) {
        std::cerr << "geometry issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
}

int run_meshes(const fs::path& path) {
    const nfsu::GeometryReport report = nfsu::load_geometry_file(path);
    std::cout << "file: " << path.string() << '\n';
    std::cout << "bytes: " << report.file_size << '\n';
    std::cout << "meshes: " << report.meshes.size() << '\n';
    std::cout << "valid meshes: " << report.valid_mesh_count() << '\n';

    std::uint64_t total_vertices = 0;
    std::uint64_t total_faces = 0;
    for (const nfsu::Mesh& mesh : report.meshes) {
        std::cout << std::setw(4) << std::setfill(' ') << mesh.index << ' '
                  << (mesh.valid ? "ok     " : "invalid") << ' '
                  << "vertices=" << mesh.declared_vertex_count << ' '
                  << "faces=" << mesh.declared_face_count << ' '
                  << "offset=" << mesh.offset << ' '
                  << "directory="
                  << (mesh.has_directory_entry ? hex32(mesh.directory_hash) : "none")
                  << ' ';
        if (mesh.has_directory_entry) {
            std::cout << "range=" << mesh.directory_offset << '+'
                      << mesh.directory_size << ' ';
        }
        std::cout
                  << (mesh.name.empty() ? "<unnamed>" : mesh.name) << '\n';
        if (mesh.valid) {
            total_vertices += mesh.vertices.size();
            total_faces += mesh.triangles.size();
        }
    }
    std::cout << "valid vertices: " << total_vertices << '\n';
    std::cout << "valid faces: " << total_faces << '\n';

    print_geometry_issues(report);
    return report.ok() ? 0 : 3;
}

bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code error;
    if (fs::equivalent(a, b, error) && !error) {
        return true;
    }
    const fs::path abs_a = fs::absolute(a, error).lexically_normal();
    const fs::path abs_b = fs::absolute(b, error).lexically_normal();
    return !error && abs_a == abs_b;
}

int run_obj(const fs::path& input, const fs::path& output_path) {
    if (same_file(input, output_path)) {
        std::cerr << "error: output path must differ from the input file\n";
        return 1;
    }

    const nfsu::GeometryReport report = nfsu::load_geometry_file(input);
    print_geometry_issues(report);
    if (report.valid_mesh_count() == 0) {
        std::cerr << "error: no valid meshes to export\n";
        return 2;
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "error: failed to open output file\n";
        return 2;
    }

    nfsu::ObjExportStats stats;
    std::string error;
    const bool written = nfsu::write_obj(output, report, stats, error);
    output.close();
    if (!written || !output) {
        std::cerr << "error: " << (error.empty() ? "OBJ write failed" : error)
                  << '\n';
        std::error_code remove_error;
        fs::remove(output_path, remove_error);
        return 2;
    }

    std::cout << "wrote: " << output_path.string() << '\n';
    std::cout << "meshes exported: " << stats.meshes << " of "
              << report.meshes.size() << '\n';
    std::cout << "vertices: " << stats.vertices << '\n';
    std::cout << "triangles: " << stats.triangles << '\n';
    return report.ok() ? 0 : 3;
}

std::string hex32(std::uint32_t value) {
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex << std::setw(8)
         << std::setfill('0') << value;
    return text.str();
}

void print_texture_issues(const nfsu::TexturePack& pack) {
    for (const nfsu::TextureIssue& issue : pack.issues) {
        std::cerr << "texture issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
}

// "yes" or "no" when the name is complete; "n/a" when the name fills its
// field and may be truncated, so a mismatch proves nothing.
const char* name_hash_status(const nfsu::TextureRecord& texture) {
    const bool match = nfsu::bin_hash(texture.name) == texture.hash;
    if (match) {
        return "yes";
    }
    return texture.name_field_full ? "n/a" : "no";
}

int run_textures(const fs::path& path) {
    std::vector<unsigned char> bytes;
    const nfsu::TexturePack pack = nfsu::load_texture_pack(path, bytes);
    std::cout << "file: " << path.string() << '\n';
    std::cout << "bytes: " << pack.file_size << '\n';
    std::cout << "pack: " << pack.pack_name << " version "
              << pack.header_version << " hash " << hex32(pack.pack_hash)
              << " path " << pack.pack_path << " (path hash "
              << (nfsu::bin_hash(pack.pack_path) == pack.pack_hash ? "matches"
                                                                   : "differs")
              << ")\n";
    std::cout << "data payload: offset " << pack.data_payload_offset
              << " size " << pack.data_payload_size << " prefix "
              << pack.data_prefix_bytes << '\n';
    std::cout << "textures: " << pack.textures.size() << '\n';
    std::cout << "valid textures: " << pack.valid_texture_count() << '\n';

    std::map<std::string, std::size_t> histogram;
    std::size_t name_matches = 0;
    std::size_t full_names = 0;
    for (const nfsu::TextureRecord& texture : pack.textures) {
        std::ostringstream key;
        key << nfsu::pixel_format_name(texture.format) << ' ' << texture.width
            << 'x' << texture.height << " mips "
            << static_cast<unsigned int>(texture.mip_count);
        ++histogram[key.str()];
        if (nfsu::bin_hash(texture.name) == texture.hash) {
            ++name_matches;
        }
        if (texture.name_field_full) {
            ++full_names;
        }
        std::cout << std::setw(4) << std::setfill(' ') << texture.index << ' '
                  << (texture.valid ? "ok     " : "invalid") << ' '
                  << hex32(texture.hash) << ' '
                  << std::left << std::setw(8)
                  << nfsu::pixel_format_name(texture.format) << std::right
                  << ' ' << texture.width << 'x' << texture.height
                  << " mips=" << static_cast<unsigned int>(texture.mip_count)
                  << " size=" << texture.data_size
                  << " blob=" << texture.blob_offset << '+' << texture.blob_size
                  << " info=" << texture.info_offset
                  << " alpha_test="
                  << (nfsu::texture_alpha_test_enabled(texture) ? 1 : 0)
                  << "(" << static_cast<unsigned int>(texture.alpha_test_mode)
                  << ") alpha_blend="
                  << nfsu::texture_alpha_blend_name(
                         nfsu::texture_alpha_blend(texture))
                  << '(' << static_cast<unsigned int>(texture.alpha_blend_mode)
                  << ')'
                  << " layer=" << nfsu::texture_draw_layer(texture)
                  << '(' << static_cast<unsigned int>(texture.layer_group)
                  << ',' << static_cast<unsigned int>(texture.layer_order)
                  << ')'
                  << " name_hash=" << name_hash_status(texture) << ' '
                  << texture.name << '\n';
    }
    std::cout << "name hash matches: " << name_matches << '\n';
    std::cout << "names filling the 23-character field: " << full_names << '\n';
    for (const auto& [key, count] : histogram) {
        std::cout << "format " << key << ": " << count << '\n';
    }

    print_texture_issues(pack);
    return pack.ok() ? 0 : 3;
}

// Metadata only: never decodes or writes pixel data.
int run_pack_set(const fs::path& path) {
    std::vector<unsigned char> bytes;
    const nfsu::TexturePackSet set = nfsu::load_texture_pack_set(path, bytes);
    std::cout << "file: " << path.string() << '\n';
    std::cout << "bytes: " << set.file_size << '\n';
    std::cout << "packs: " << set.packs.size() << " complete "
              << (set.complete ? 1 : 0) << '\n';
    for (std::size_t p = 0; p < set.packs.size(); ++p) {
        const nfsu::TexturePack& pack = set.packs[p];
        std::cout << "pack " << p << ": root " << pack.root_offset << " storage "
                  << nfsu::texture_storage_name(pack.storage) << " name "
                  << pack.pack_name << " version " << pack.header_version
                  << " hash " << hex32(pack.pack_hash) << " path "
                  << pack.pack_path << " (path hash "
                  << (nfsu::bin_hash(pack.pack_path) == pack.pack_hash ? "matches"
                                                                       : "differs")
                  << ")\n";
        std::cout << "  data payload: offset " << pack.data_payload_offset
                  << " size " << pack.data_payload_size << " prefix "
                  << pack.data_prefix_bytes << " start " << pack.data_start;
        if (pack.storage == nfsu::TextureStorage::raw) {
            std::cout << " raw_extent " << pack.raw_extent;
        }
        std::cout << '\n';
        std::cout << "  textures: " << pack.textures.size() << " valid "
                  << pack.valid_texture_count() << " issues "
                  << pack.issues.size() << '\n';
        for (const nfsu::TextureRecord& texture : pack.textures) {
            std::cout << "  " << std::setw(4) << std::setfill(' ')
                      << texture.index << ' '
                      << (texture.valid ? "ok     " : "invalid") << ' '
                      << hex32(texture.hash) << ' ' << std::left << std::setw(8)
                      << nfsu::pixel_format_name(texture.format) << std::right
                      << " code=0x" << std::hex << std::uppercase
                      << static_cast<unsigned int>(texture.format_code)
                      << std::dec << ' ' << texture.width << 'x' << texture.height
                      << " mips=" << static_cast<unsigned int>(texture.mip_count)
                      << " size=" << texture.data_size
                      << " virtual=" << texture.virtual_offset
                      << " data=" << texture.blob_offset << '+' << texture.blob_size
                      << " info=" << texture.info_offset
                      << " alpha_test="
                      << (nfsu::texture_alpha_test_enabled(texture) ? 1 : 0)
                      << "(" << static_cast<unsigned int>(texture.alpha_test_mode)
                      << ") alpha_blend="
                      << nfsu::texture_alpha_blend_name(
                             nfsu::texture_alpha_blend(texture))
                      << '(' << static_cast<unsigned int>(texture.alpha_blend_mode)
                      << ')'
                      << " layer=" << nfsu::texture_draw_layer(texture)
                      << '(' << static_cast<unsigned int>(texture.layer_group)
                      << ',' << static_cast<unsigned int>(texture.layer_order)
                      << ')'
                      << " name_hash=" << name_hash_status(texture) << ' '
                      << texture.name << '\n';
        }
    }
    for (const nfsu::TextureIssue& issue : set.issues) {
        std::cerr << "texture set issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    for (std::size_t p = 0; p < set.packs.size(); ++p) {
        for (const nfsu::TextureIssue& issue : set.packs[p].issues) {
            std::cerr << "pack " << p << " issue at " << issue.offset << ": "
                      << issue.message << '\n';
        }
    }
    return set.ok() ? 0 : 3;
}

int run_export_textures(
    const fs::path& input,
    const fs::path& output_dir,
    const std::string& mode) {
    if (mode != "tga" && mode != "dds") {
        std::cerr << "error: format must be tga or dds\n";
        return 1;
    }

    std::vector<unsigned char> bytes;
    const nfsu::TexturePack pack = nfsu::load_texture_pack(input, bytes);
    print_texture_issues(pack);
    if (pack.valid_texture_count() == 0) {
        std::cerr << "error: no valid textures to export\n";
        return 2;
    }

    nfsu::TextureExportResult result;
    const bool exported = nfsu::export_textures(
        bytes, pack, input, output_dir,
        mode == "dds" ? nfsu::ExportFormat::dds : nfsu::ExportFormat::tga,
        result);
    for (std::size_t index : result.skipped) {
        std::cerr << "skipped texture " << index << ": "
                  << pack.textures[index].problem << '\n';
    }
    if (!exported) {
        std::cerr << "error: " << result.error << '\n';
        return 2;
    }
    for (const nfsu::ExportedTexture& file : result.files) {
        const nfsu::TextureRecord& texture = pack.textures[file.index];
        std::cout << "wrote " << file.path.filename().string() << ' '
                  << nfsu::pixel_format_name(texture.format) << ' '
                  << texture.width << 'x' << texture.height
                  << " jdlz_unused_bytes=" << file.jdlz_unused_bytes << '\n';
    }
    std::cout << "exported: " << result.files.size() << " of "
              << pack.textures.size() << '\n';
    return pack.ok() ? 0 : 3;
}

int run_texture_bindings(const fs::path& geometry_path, const fs::path& texture_path) {
    const nfsu::GeometryReport geometry = nfsu::load_geometry_file(geometry_path);
    std::vector<unsigned char> bytes;
    const nfsu::TexturePack pack = nfsu::load_texture_pack(texture_path, bytes);
    print_geometry_issues(geometry);
    print_texture_issues(pack);

    std::map<std::uint32_t, std::vector<const nfsu::TextureRecord*>> by_hash;
    for (const nfsu::TextureRecord& texture : pack.textures) {
        by_hash[texture.hash].push_back(&texture);
    }

    std::size_t records = 0;
    std::size_t resolved = 0;
    std::map<std::uint32_t, std::size_t> unresolved;
    std::map<std::uint32_t, std::size_t> flag_counts;
    std::map<std::uint32_t, std::size_t> material_key_slot_counts;
    std::map<std::uint32_t, std::size_t> material_key_counts;
    std::size_t material_key_bound = 0;
    std::size_t material_key_none = 0;
    for (const nfsu::Mesh& mesh : geometry.meshes) {
        if (!mesh.valid) {
            continue;
        }
        for (std::size_t i = 0; i < mesh.materials.size(); ++i) {
            const nfsu::MeshMaterial& material = mesh.materials[i];
            const std::uint32_t hash = mesh.texture_hashes[material.texture_slot];
            ++records;
            ++flag_counts[material.flags];
            ++material_key_slot_counts[material.material_key_slot];
            if (material.material_key_slot == 0xFFU) {
                ++material_key_none;
            } else {
                ++material_key_bound;
                ++material_key_counts[
                    mesh.material_key_hashes[material.material_key_slot]];
            }
            std::cout << std::setw(4) << std::setfill(' ') << mesh.index << ' '
                      << mesh.name << " material " << i << " @"
                      << material.offset << " indices " << material.first_index
                      << '+' << material.index_count << " slot "
                       << material.texture_slot << " hash " << hex32(hash)
                       << " @" << (mesh.texture_list_offset +
                                   material.texture_slot *
                                       nfsu::kTextureListEntrySize)
                       << " material_key_slot " << material.material_key_slot;
            if (material.material_key_slot != 0xFFU) {
                std::cout << " material_key "
                          << hex32(mesh.material_key_hashes[
                                 material.material_key_slot]);
            } else {
                std::cout << " none";
            }
            std::cout << " flags " << hex32(material.flags)
                       << " -> ";
            const auto found = by_hash.find(hash);
            if (found == by_hash.end()) {
                std::cout << "not in pack\n";
                ++unresolved[hash];
            } else if (found->second.size() != 1) {
                std::cout << "ambiguous (" << found->second.size()
                          << " pack entries)\n";
            } else {
                const nfsu::TextureRecord& texture = *found->second.front();
                std::cout << "texture " << texture.index << " @"
                          << texture.info_offset << ' ' << texture.name << '\n';
                ++resolved;
            }
        }
    }
    std::cout << "material records: " << records << '\n';
    std::cout << "resolved in pack: " << resolved << '\n';
    std::cout << "unresolved hashes: " << unresolved.size() << '\n';
    for (const auto& [hash, count] : unresolved) {
        std::cout << "  " << hex32(hash) << " x" << count << '\n';
    }
    std::cout << "material flag values: " << flag_counts.size() << '\n';
    for (const auto& [flags, count] : flag_counts) {
        std::cout << "  " << hex32(flags) << " x" << count << '\n';
    }
    std::cout << "material-key bindings: bound " << material_key_bound
              << " none " << material_key_none << '\n';
    std::cout << "material-key slot values: "
              << material_key_slot_counts.size() << '\n';
    for (const auto& [slot, count] : material_key_slot_counts) {
        std::cout << "  " << slot << " x" << count << '\n';
    }
    std::cout << "material-key hashes: " << material_key_counts.size() << '\n';
    for (const auto& [hash, count] : material_key_counts) {
        std::cout << "  " << hex32(hash) << " x" << count << '\n';
    }
    return geometry.ok() && pack.ok() ? 0 : 3;
}

void print_car_part_issues(const nfsu::CarPartDatabase& database) {
    for (const nfsu::CarPartIssue& issue : database.issues) {
        std::cerr << "car part issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }
}

// Display names are raw bytes. Bytes outside printable ASCII, the quote and
// the backslash are written as \xNN.
std::string escaped_name(const std::string& name) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    for (const char ch : name) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20U && byte <= 0x7EU && ch != '\'' && ch != '\\') {
            text.push_back(ch);
        } else {
            text += "\\x";
            text.push_back(digits[byte >> 4U]);
            text.push_back(digits[byte & 0x0FU]);
        }
    }
    return text;
}

std::string lod_hashes(const nfsu::CarPartRecord& record) {
    std::string text;
    for (std::size_t lod = 0; lod < nfsu::kCarPartLodCount; ++lod) {
        text += ' ';
        text += static_cast<char>('A' + lod);
        text += '=';
        text += record.lod_mesh_hashes[lod] == 0 ? std::string("-")
                                                 : hex32(record.lod_mesh_hashes[lod]);
    }
    return text;
}

void print_part_record(const nfsu::CarPartRecord& record) {
    const char* slot_name = nfsu::car_slot_name(record.slot);
    std::cout << std::setw(5) << std::setfill(' ') << record.index << " @"
              << record.offset << " car=" << hex32(record.car_type_hash)
              << " slot=" << record.slot << ' '
              << (slot_name == nullptr ? "?" : slot_name)
              << " level=" << static_cast<unsigned int>(record.upgrade_level)
              << " b12=" << static_cast<unsigned int>(record.byte_12)
              << " part=" << hex32(record.part_name_hash)
              << " attributes=" << record.attribute_count << '+'
              << record.second_attribute_count << lod_hashes(record) << " name='"
              << escaped_name(record.name) << "'\n";
}

int run_car_parts(const fs::path& path, const std::string& car_type_name) {
    const nfsu::CarPartDatabase database = nfsu::load_car_part_database(path);
    std::cout << "file: " << path.string() << '\n';
    std::cout << "bytes: " << database.file_size << '\n';
    std::cout << "database: offset " << database.chunk_offset << " version "
              << database.version << " valid " << (database.valid ? 1 : 0) << '\n';
    std::cout << "records: " << database.records.size() << '\n';
    std::cout << "attributes: " << database.attributes.size() << '\n';
    std::map<std::uint32_t, std::size_t> car_types;
    for (const nfsu::CarPartRecord& record : database.records) {
        ++car_types[record.car_type_hash];
    }
    std::cout << "car type hashes: " << car_types.size() << '\n';
    const bool filter = !car_type_name.empty();
    const std::uint32_t wanted = nfsu::bin_hash(car_type_name);
    if (filter) {
        std::cout << "car type: " << car_type_name << ' ' << hex32(wanted) << '\n';
    }
    std::size_t listed = 0;
    for (const nfsu::CarPartRecord& record : database.records) {
        if (!filter || record.car_type_hash == wanted) {
            print_part_record(record);
            ++listed;
        }
    }
    std::cout << "listed records: " << listed << '\n';
    print_car_part_issues(database);
    if (filter && listed == 0) {
        std::cerr << "error: no record has car type '" << car_type_name << "'\n";
        return 2;
    }
    return database.ok() ? 0 : 3;
}

// One mesh of a loaded geometry file, found by verified directory hash.
struct MeshLocation {
    std::size_t file = 0;
    std::size_t mesh = 0;
};

int run_stock_parts(const fs::path& database_path,
                    const std::string& car_type_name,
                    const std::vector<fs::path>& geometry_paths) {
    const nfsu::CarPartDatabase database = nfsu::load_car_part_database(database_path);
    print_car_part_issues(database);
    nfsu::StockParts parts;
    std::string error;
    if (!nfsu::select_stock_parts(database, car_type_name, parts, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    bool geometry_ok = true;
    std::vector<nfsu::GeometryReport> geometry;
    std::map<std::uint32_t, std::vector<MeshLocation>> by_hash;
    for (std::size_t f = 0; f < geometry_paths.size(); ++f) {
        geometry.push_back(nfsu::load_geometry_file(geometry_paths[f]));
        const nfsu::GeometryReport& report = geometry.back();
        print_geometry_issues(report);
        std::cout << "geometry " << f << ": " << geometry_paths[f].string() << " meshes "
                  << report.meshes.size() << " directory "
                  << (report.mesh_directory_valid ? "verified" : "unavailable") << '\n';
        if (!report.mesh_directory_valid) {
            // Without it a missing hash would prove nothing.
            std::cerr << "error: " << geometry_paths[f].string()
                      << " has no verified mesh directory\n";
            return 2;
        }
        geometry_ok = geometry_ok && report.ok();
        for (std::size_t m = 0; m < report.meshes.size(); ++m) {
            by_hash[report.meshes[m].directory_hash].push_back(MeshLocation{f, m});
        }
    }

    std::cout << "car type: " << parts.car_type_name << ' ' << hex32(parts.car_type_hash)
              << '\n';
    // Ascending and unique, as the game's solid lookup needs.
    std::vector<std::uint32_t> loaded;
    loaded.reserve(by_hash.size());
    for (const auto& [hash, locations] : by_hash) {
        loaded.push_back(hash);
    }
    std::array<std::size_t, nfsu::kCarPartLodCount> named{};
    std::array<std::size_t, nfsu::kCarPartLodCount> drawn{};
    std::array<std::size_t, nfsu::kCarPartLodCount> fell_back{};
    std::array<std::size_t, nfsu::kCarPartLodCount> invalid{};
    std::array<std::size_t, nfsu::kCarPartLodCount> empty{};
    std::size_t installed = 0;
    std::size_t enabled = 0;
    std::size_t without_model = 0;
    for (std::size_t s = 0; s < nfsu::kCarSlotCount; ++s) {
        const nfsu::StockPartSlot& slot = parts.slots[s];
        if (slot.record == nfsu::kNoCarPart) {
            continue;
        }
        ++installed;
        const bool has_model = s < nfsu::kCarModelSlotCount;
        if (!has_model) {
            ++without_model;
        }
        const nfsu::CarPartRecord& record = database.records[slot.record];
        std::cout << "slot " << std::setw(3) << std::setfill(' ') << s << ' '
                  << nfsu::car_slot_name(static_cast<int>(s)) << ' '
                  << nfsu::stock_part_rule_name(slot.rule) << ' '
                  << (slot.enabled ? "enabled" : "disabled") << " record "
                  << record.index << " level "
                  << static_cast<unsigned int>(record.upgrade_level) << " part "
                  << hex32(record.part_name_hash) << " name='"
                  << escaped_name(record.name) << "'"
                  << (has_model ? "" : " (no model: slot is outside 0..29)") << '\n';
        if (slot.enabled) {
            ++enabled;
        }
        std::array<nfsu::PartModel, nfsu::kCarPartLodCount> models{};
        if (!geometry.empty() &&
            !nfsu::resolve_part_models(record, loaded, models, error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        for (std::size_t lod = 0; lod < nfsu::kCarPartLodCount; ++lod) {
            const nfsu::PartModel& model = models[lod];
            const std::uint32_t requested = record.lod_mesh_hashes[lod];
            const bool counted = slot.enabled && has_model;
            std::cout << "  " << static_cast<char>('A' + lod) << ' ';
            if (requested == 0) {
                std::cout << "none\n";
                if (counted) {
                    ++empty[lod];
                }
                continue;
            }
            std::cout << hex32(requested);
            if (counted) {
                ++named[lod];
            }
            if (geometry.empty()) {
                std::cout << " not checked\n";
                continue;
            }
            if (model.source == nfsu::PartModelSource::fallback) {
                std::cout << " fallback " << hex32(model.hash);
            }
            if (!model.resolved) {
                std::cout << (model.hash == 0 ? " absent, no fallback\n" : " absent\n");
                if (counted) {
                    ++empty[lod];
                }
                continue;
            }
            bool all_valid = true;
            for (const MeshLocation& location : by_hash.find(model.hash)->second) {
                const nfsu::Mesh& mesh = geometry[location.file].meshes[location.mesh];
                std::cout << " geometry " << location.file << " mesh " << location.mesh
                          << ' ' << mesh.name << (mesh.valid ? "" : " invalid");
                all_valid = all_valid && mesh.valid;
            }
            if (by_hash.find(model.hash)->second.size() > 1) {
                std::cout << " (ambiguous)";
            }
            std::cout << '\n';
            if (counted) {
                ++(all_valid ? drawn : invalid)[lod];
                if (model.source == nfsu::PartModelSource::fallback) {
                    ++fell_back[lod];
                }
            }
        }
    }
    std::cout << "installed slots: " << installed << " enabled: " << enabled
              << " without a model: " << without_model << '\n';
    for (std::size_t lod = 0; lod < nfsu::kCarPartLodCount; ++lod) {
        std::cout << "enabled slots with a model at LOD " << static_cast<char>('A' + lod)
                  << ": named " << named[lod];
        if (!geometry.empty()) {
            std::cout << " drawn " << drawn[lod] << " (of them by fallback "
                      << fell_back[lod] << ") invalid " << invalid[lod] << " empty "
                      << empty[lod];
        }
        std::cout << '\n';
    }
    return database.ok() && geometry_ok ? 0 : 3;
}

// Decodes one explicitly selected, bounded region of a track STREAM
// container. The offset and the size come from the caller; nothing is
// scanned and no other byte of the file is read. Texture sources are
// searched in the order given, as everywhere else in this tool.
int run_track_region(
    const fs::path& path,
    std::uint64_t offset,
    std::uint64_t size,
    const std::vector<fs::path>& texture_paths) {
    std::vector<unsigned char> region_bytes;
    nfsu::TrackRegionSelector selector;
    selector.offset = offset;
    selector.size = size;
    const nfsu::TrackRegionReport report =
        nfsu::load_track_region(path, selector, region_bytes);

    std::cout << "file: " << path.string() << '\n';
    std::cout << "file size: " << report.file_size << '\n';
    std::cout << "region: offset " << report.region.offset << " size "
              << report.region.size << " bytes read " << region_bytes.size()
              << '\n';
    std::cout << "chunks: " << report.cost.chunks
              << " geometry roots: " << report.cost.geometry_roots << '\n';
    std::cout << "meshes: " << report.meshes.size() << " valid "
              << report.valid_mesh_count() << " empty "
              << report.cost.empty_meshes << " hash table "
              << (report.mesh_table_valid ? "verified" : "unverified") << '\n';

    std::size_t listed = 0;
    for (const nfsu::TrackMesh& mesh : report.meshes) {
        if (listed >= kTrackMeshListLimit) {
            std::cout << "  ... " << (report.meshes.size() - listed)
                      << " more meshes not listed\n";
            break;
        }
        ++listed;
        const char* state =
            mesh.valid ? "ok     " : (mesh.empty ? "empty  " : "invalid");
        std::cout << "  " << std::setw(4) << std::setfill(' ') << mesh.index
                  << ' ' << state << ' ' << hex32(mesh.mesh_hash) << ' '
                  << (mesh.name.empty() ? std::string("<unnamed>") : mesh.name)
                  << '\n';
        std::cout << "       container " << mesh.offset << " format "
                  << hex32(mesh.vertex_format_flags) << " stride "
                  << mesh.vertex_stride << " vertices "
                  << mesh.declared_vertex_count << " triangles "
                  << mesh.declared_face_count << " materials "
                  << mesh.declared_material_count << " textures "
                  << mesh.texture_hashes.size() << " keys "
                  << mesh.material_key_hashes.size() << '\n';
        std::cout << "       vertex data " << mesh.vertex_data_offset
                  << " prefix " << mesh.vertex_prefix_bytes << " index data "
                  << mesh.face_data_offset << " prefix "
                  << mesh.face_prefix_bytes << " tail " << mesh.face_tail_bytes
                  << '\n';
    }

    for (const nfsu::TrackIssue& issue : report.issues) {
        std::cerr << "region issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }

    const nfsu::TrackRegionCost& cost = report.cost;
    std::cout << "cost: region bytes " << cost.region_bytes << " chunks "
              << cost.chunks << " vertices " << cost.vertices << " triangles "
              << cost.triangles << " materials " << cost.materials << '\n';
    std::cout << "cost: source vertex bytes " << cost.source_vertex_bytes
              << " source index bytes " << cost.source_index_bytes
              << " decoded vertex bytes " << cost.decoded_vertex_bytes
              << " decoded index bytes " << cost.decoded_index_bytes << '\n';
    std::cout << "cost: distinct texture hashes "
              << cost.distinct_texture_hashes << " largest mesh vertices "
              << cost.largest_mesh_vertices << " triangles "
              << cost.largest_mesh_triangles << '\n';
    if (cost.has_bounds) {
        std::cout << "bounds: px " << cost.bounds_min[0] << " .. "
                  << cost.bounds_max[0] << " py " << cost.bounds_min[1] << " .. "
                  << cost.bounds_max[1] << " pz " << cost.bounds_min[2] << " .. "
                  << cost.bounds_max[2] << '\n';
    }

    if (texture_paths.empty()) {
        std::cout << "textures: no source given; materials are not resolved\n";
        return report.ok() ? 0 : 3;
    }
    if (report.valid_mesh_count() == 0) {
        std::cerr << "error: the region has no valid mesh to resolve\n";
        return report.ok() ? 2 : 3;
    }

    // Only the hashes this region names are decoded, so an unrelated
    // texture in a source never costs anything here.
    const std::vector<std::uint32_t> wanted =
        nfsu::track_region_texture_hashes(report);
    // Both vectors are sized once, so the pointers taken below stay valid.
    std::vector<std::vector<unsigned char>> source_bytes(texture_paths.size());
    std::vector<nfsu::TexturePackSet> sets(texture_paths.size());
    std::uint64_t read_bytes = 0;
    for (std::size_t i = 0; i < texture_paths.size(); ++i) {
        nfsu::TextureLimits source_limits;
        const std::uint64_t remaining = kMaxTrackTextureInputBytes - read_bytes;
        source_limits.max_file_size =
            std::min(source_limits.max_file_size, remaining);
        sets[i] = nfsu::load_texture_pack_set(texture_paths[i], source_bytes[i],
                                              source_limits);
        read_bytes += source_bytes[i].size();
        // "size" is what the file holds; "read" is what this run took,
        // which is 0 when the source was refused before it was read.
        std::cout << "texture source " << i << ": " << texture_paths[i].string()
                  << " size " << sets[i].file_size << " read "
                  << source_bytes[i].size() << " packs " << sets[i].packs.size()
                  << " complete " << (sets[i].complete ? 1 : 0) << '\n';
        for (const nfsu::TextureIssue& issue : sets[i].issues) {
            std::cerr << "texture source " << i << " issue at " << issue.offset
                      << ": " << issue.message << '\n';
        }
        for (std::size_t p = 0; p < sets[i].packs.size(); ++p) {
            for (const nfsu::TextureIssue& issue : sets[i].packs[p].issues) {
                std::cerr << "texture source " << i << " pack " << p
                          << " issue at " << issue.offset << ": "
                          << issue.message << '\n';
            }
        }
    }

    std::vector<nfsu::RenderTextureSource> sources(texture_paths.size());
    for (std::size_t i = 0; i < texture_paths.size(); ++i) {
        sources[i].bytes = &source_bytes[i];
        for (const nfsu::TexturePack& pack : sets[i].packs) {
            sources[i].packs.push_back(&pack);
        }
        sources[i].complete = sets[i].complete;
        sources[i].wanted = &wanted;
    }

    nfsu::RenderTextureSet textures;
    std::string error;
    if (!nfsu::decode_render_texture_sources(
            sources, nfsu::RenderTextureLimits{}, textures, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    for (const nfsu::RenderTexture& texture : textures.textures) {
        if (!texture.valid && !texture.problem.empty()) {
            std::cerr << "texture " << hex32(texture.hash) << " (source "
                      << texture.source << " pack " << texture.pack
                      << " record " << texture.record
                      << ") unusable: " << texture.problem << '\n';
        }
    }
    std::cout << "textures: wanted " << wanted.size() << " decoded "
              << textures.textures.size() << " bytes " << textures.decoded_bytes
              << " shadowed " << textures.shadowed.size() << " unusable claims "
              << textures.unusable_claims.size() << " skipped "
              << textures.not_wanted << '\n';
    for (const std::size_t blocked : textures.blocked_sources) {
        std::cerr << "texture source " << blocked
                  << " not searched: an earlier source is incomplete\n";
    }

    nfsu::PreparedTrackRegion prepared;
    if (!nfsu::prepare_track_region(report, textures, nfsu::TrackSceneLimits{},
                                    prepared, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    const nfsu::TrackDrawCost& draw_cost = prepared.cost;
    std::cout << "draws: " << draw_cost.draws << " drawable "
              << draw_cost.drawable_draws << " resolved "
              << draw_cost.resolved_draws << " unresolved "
              << draw_cost.unresolved_draws << " unusable "
              << draw_cost.unusable_draws << " invalid slot "
              << draw_cost.invalid_slot_draws << '\n';
    std::cout << "draw cost: vertex buffer bytes "
              << draw_cost.vertex_buffer_bytes << " index buffer bytes "
              << draw_cost.index_buffer_bytes << " drawn triangles "
              << draw_cost.drawn_triangles << " largest draw "
              << draw_cost.largest_draw_triangles << " texture binds "
              << draw_cost.texture_binds << '\n';
    std::cout << "draw cost: textures " << draw_cost.textures
              << " texture bytes " << draw_cost.texture_bytes
              << " unresolved hashes " << draw_cost.unresolved_hashes << '\n';
    for (const std::uint32_t hash : prepared.unresolved_hashes) {
        std::cout << "  unresolved " << hex32(hash) << '\n';
    }
    return report.ok() ? 0 : 3;
}

// ---------------------------------------------------------------------------
// track-objects: the scenery sections of one explicitly selected region.
// The region is read exactly once, read-only, and nothing else of the file
// is touched. A second selector, when given, names the region whose
// geometry the object mesh hashes are resolved against; it is read the
// same way. The two need not be the same bytes, because a section commonly
// names meshes that stream in with another one.
// ---------------------------------------------------------------------------
// track-collision: polygons printed before the listing is summarized.
constexpr std::size_t kCollisionPolygonListLimit = 16;

void print_collision_polygon(
    const nfsu::TrackCollisionPolygon& polygon,
    const char* prefix) {
    std::cout << prefix << std::setw(6) << std::setfill(' ') << polygon.index
              << " at " << polygon.offset << " corners";
    for (std::size_t c = 0; c < nfsu::kCollisionPolygonCorners; ++c) {
        std::cout << " (" << polygon.corner[c][0] << ", " << polygon.corner[c][1]
                  << ", " << polygon.corner[c][2] << ")";
    }
    std::cout << "\n";
}

int run_track_collision(
    const fs::path& path,
    bool has_probe,
    double probe_slot0,
    double probe_slot1) {
    std::vector<unsigned char> image;
    const nfsu::TrackCollisionReport report =
        nfsu::load_track_collision(path, image);

    std::cout << "file: " << path.string() << '\n';
    std::cout << "file size: " << report.file_size << " image bytes "
              << image.size() << " unused packed bytes "
              << report.cost.unused_packed_bytes << '\n';

    const nfsu::TrackCollisionCost& cost = report.cost;
    std::cout << "collision: " << (report.valid ? "valid" : "refused")
              << " chunks " << cost.chunks << " polygons " << cost.polygons
              << " cells " << cost.cells << " indices " << cost.indices
              << " nodes " << cost.tree_nodes << '\n';
    std::cout << "cost: source polygon bytes " << cost.source_polygon_bytes
              << " cell bytes " << cost.source_cell_bytes << " index bytes "
              << cost.source_index_bytes << " node bytes "
              << cost.source_tree_bytes << '\n';
    std::cout << "cost: cell polygon pairs " << cost.cell_polygon_pairs
              << " unreferenced polygons " << cost.unreferenced_polygons
              << " saturated vertical " << cost.saturated_vertical_polygons
              << " degenerate corners " << cost.degenerate_corner_polygons
              << '\n';
    if (cost.has_bounds) {
        std::cout << "stored bounds: px " << cost.stored_min[0] << " .. "
                  << cost.stored_max[0] << " pz " << cost.stored_min[1] << " .. "
                  << cost.stored_max[1] << " py " << cost.stored_min[2] << " .. "
                  << cost.stored_max[2] << '\n';
        std::cout << "corner bounds: px " << cost.corner_min[0] << " .. "
                  << cost.corner_max[0] << " pz " << cost.corner_min[1] << " .. "
                  << cost.corner_max[1] << " py " << cost.corner_min[2] << " .. "
                  << cost.corner_max[2] << '\n';
    }

    for (const nfsu::TrackIssue& issue : report.issues) {
        std::cerr << "collision issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    if (!report.valid) {
        return 3;
    }

    std::size_t listed = 0;
    for (const nfsu::TrackCollisionPolygon& polygon : report.polygons) {
        if (listed >= kCollisionPolygonListLimit) {
            std::cout << "  ... " << (report.polygons.size() - listed)
                      << " more polygons not listed\n";
            break;
        }
        ++listed;
        print_collision_polygon(polygon, "  ");
    }

    if (!has_probe) {
        std::cout << "probe: no column given; no query was run\n";
        return 0;
    }

    const float slot0 = static_cast<float>(probe_slot0);
    const float slot1 = static_cast<float>(probe_slot1);
    std::cout << "probe column: px " << slot0 << " pz " << slot1 << '\n';

    std::vector<std::size_t> cells;
    if (nfsu::query_track_collision_cells(report, slot0, slot1, slot0, slot1,
                                          cells)) {
        std::cout << "probe cells: " << cells.size() << '\n';
        for (std::size_t cell : cells) {
            std::cout << "  cell " << cell << " at ("
                      << report.cells[cell].center[0] << ", "
                      << report.cells[cell].center[1] << ") polygons "
                      << report.cells[cell].index_count << '\n';
        }
    } else {
        std::cout << "probe cells: the query was refused\n";
    }

    const float box_min[3] = {slot0 - 8.0F, slot1 - 8.0F, -1000.0F};
    const float box_max[3] = {slot0 + 8.0F, slot1 + 8.0F, 1000.0F};
    std::vector<std::size_t> found;
    if (nfsu::query_track_collision_box(report, box_min, box_max, found)) {
        std::cout << "probe box: 16 by 16 column, polygons " << found.size()
                  << '\n';
        listed = 0;
        for (std::size_t index : found) {
            if (listed >= kCollisionPolygonListLimit) {
                std::cout << "  ... " << (found.size() - listed)
                          << " more polygons not listed\n";
                break;
            }
            ++listed;
            print_collision_polygon(report.polygons[index], "  ");
        }
    } else {
        std::cout << "probe box: the query was refused\n";
    }

    // Straight down the column, from above every retail track and past the
    // bottom of every one of them.
    const float origin[3] = {slot0, slot1, 1000.0F};
    const float direction[3] = {0.0F, 0.0F, -1.0F};
    nfsu::TrackCollisionHit hit;
    if (nfsu::query_track_collision_ray(report, origin, direction, 2000.0F,
                                        hit)) {
        std::cout << "probe ray: polygon " << hit.polygon << " triangle "
                  << hit.triangle << " distance " << hit.distance << " point ("
                  << hit.point[0] << ", " << hit.point[1] << ", " << hit.point[2]
                  << ")\n";
    } else {
        std::cout << "probe ray: nothing under the column\n";
    }
    return 0;
}

// ---------------------------------------------------------------------------
// track-contact: the whole track's collision, loaded once, its memory against
// the budgets of issue #52, and one contact query. docs/TRACK_COLLISION.md,
// "The whole circuit". Read-only; writes nothing.
// ---------------------------------------------------------------------------

// A negative zero prints as "-0", which says nothing a zero does not.
float printable_zero(float value) {
    return value == 0.0F ? 0.0F : value;
}

std::uint64_t permille(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0 : part * 1000U / whole;
}

void print_track_contact(const char* label, nfsu::TrackContactStatus status,
                         const nfsu::TrackContact& contact) {
    std::cout << label << ": " << nfsu::track_contact_status_name(status);
    if (status == nfsu::TrackContactStatus::hit) {
        std::cout << ' ' << nfsu::track_contact_kind_name(contact.kind)
                  << " polygon " << contact.polygon << " triangle "
                  << contact.triangle << " distance "
                  << printable_zero(contact.distance)
                  << " point (" << contact.point[0] << ", " << contact.point[1]
                  << ", " << contact.point[2] << ") normal ("
                  << printable_zero(contact.normal[0]) << ", "
                  << printable_zero(contact.normal[1]) << ", "
                  << printable_zero(contact.normal[2]) << ") up " << contact.up;
    }
    std::cout << '\n';
}

// query: 0 none, 1 a column at probe[0..1], 2 a segment from probe[0..2] to
// probe[3..5].
int run_track_contact(const fs::path& path, int query, const float probe[6]) {
    const nfsu::TrackContactWorld world = nfsu::load_track_contact_world(path);
    const nfsu::TrackCollisionReport& report = world.collision;
    const nfsu::TrackContactMemory& memory = world.memory;

    std::cout << "file: " << path.string() << '\n';
    std::cout << "file size: " << report.file_size << " image bytes "
              << memory.image_bytes << " unused packed bytes "
              << report.cost.unused_packed_bytes << '\n';
    std::cout << "collision: " << (world.valid ? "valid" : "refused")
              << " polygons " << report.cost.polygons << " cells "
              << report.cost.cells << " indices " << report.cost.indices
              << " nodes " << report.cost.tree_nodes << '\n';
    for (const nfsu::TrackIssue& issue : report.issues) {
        std::cerr << "collision issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    if (!world.valid) {
        return 3;
    }
    const nfsu::TrackCollisionCost& cost = report.cost;
    std::cout << "bounds: px " << cost.stored_min[0] << " .. " << cost.stored_max[0]
              << " pz " << cost.stored_min[1] << " .. " << cost.stored_max[1]
              << " py " << cost.stored_min[2] << " .. " << cost.stored_max[2]
              << '\n';
    std::cout << "triangles: road " << world.counts.road_triangles << " wall "
              << world.counts.wall_triangles << " degenerate "
              << world.counts.degenerate_triangles << " (road when |normal py| >= "
              << nfsu::kTrackContactRoadMinUp << ")\n";
    std::cout << "frame: raw slot order (px, pz, py), py up, world units; "
                 "render x = px, y = py, z = -pz\n";
    std::cout << "memory resident: " << memory.resident_bytes << " bytes in "
              << memory.resident_allocations << " allocations: polygons "
              << memory.polygon_bytes << " cells " << memory.cell_bytes
              << " indices " << memory.index_bytes << " nodes "
              << memory.node_bytes << " triangles " << memory.triangle_bytes
              << '\n';
    std::cout << "memory stored: " << memory.stored_bytes
              << " bytes as the file holds the same arrays\n";
    std::cout << "memory load: packed " << memory.packed_bytes << " image "
              << memory.image_bytes << " peak " << memory.load_peak_bytes << '\n';
    std::cout << "memory ceiling: " << nfsu::track_contact_resident_ceiling(
                                           nfsu::TrackCollisionLimits{})
              << " resident bytes at the default collision limits\n";

    nfsu::MemoryPlan plan;
    std::string error;
    if (!plan.create(nfsu::switch_memory_budget(), error)) {
        std::cerr << "switch plan: " << error << '\n';
        return 4;
    }
    nfsu::TrackContactPlacement placement;
    if (!nfsu::place_track_contact_memory(memory, plan, placement)) {
        std::cout << "switch plan: refused at " << placement.refused_part << ": "
                  << nfsu::arena_refusal_name(placement.refusal) << '\n';
        return 4;
    }
    std::cout << "switch plan: persistent " << placement.persistent_used_bytes
              << " of " << placement.persistent_capacity << " bytes ("
              << permille(placement.persistent_used_bytes,
                          placement.persistent_capacity)
              << " per mille) extent " << placement.persistent_extent
              << "; streaming load peak " << placement.streaming_peak_bytes
              << " of " << placement.streaming_capacity << " bytes ("
              << permille(placement.streaming_peak_bytes,
                          placement.streaming_capacity)
              << " per mille), released\n";

    if (query == 0) {
        std::cout << "probe: no point given; no query was run\n";
        return 0;
    }
    nfsu::TrackContact contact;
    nfsu::TrackContactStatus status = nfsu::TrackContactStatus::refused;
    if (query == 1) {
        // The column issue #39's track-collision probe casts: from py 1000,
        // above every retail track, down 2000, past the bottom of every one.
        const float point[3] = {probe[0], probe[1], 1000.0F};
        std::cout << "probe column: px " << probe[0] << " pz " << probe[1]
                  << " from py 1000 down 2000\n";
        status = nfsu::query_track_contact_point(world, point, 0.0F, 2000.0F,
                                                 contact);
        print_track_contact("probe column", status, contact);
    } else {
        std::cout << "probe segment: (" << probe[0] << ", " << probe[1] << ", "
                  << probe[2] << ") to (" << probe[3] << ", " << probe[4] << ", "
                  << probe[5] << ")\n";
        status = nfsu::query_track_contact_segment(world, probe, probe + 3,
                                                   contact);
        print_track_contact("probe segment", status, contact);
    }
    return status == nfsu::TrackContactStatus::refused ? 5 : 0;
}

int run_track_objects(
    const fs::path& path,
    std::uint64_t offset,
    std::uint64_t size,
    bool has_geometry,
    std::uint64_t geometry_offset,
    std::uint64_t geometry_size) {
    std::vector<unsigned char> region_bytes;
    nfsu::TrackRegionSelector selector;
    selector.offset = offset;
    selector.size = size;
    const nfsu::TrackObjectReport report =
        nfsu::load_track_objects(path, selector, region_bytes);

    std::cout << "file: " << path.string() << '\n';
    std::cout << "file size: " << report.file_size << '\n';
    std::cout << "region: offset " << report.region.offset << " size "
              << report.region.size << " bytes read " << region_bytes.size()
              << '\n';
    std::cout << "sections: " << report.sections.size() << " valid "
              << report.valid_section_count() << " chunks "
              << report.cost.chunks << '\n';

    std::size_t listed = 0;
    for (const nfsu::TrackSection& section : report.sections) {
        if (listed >= kTrackSectionListLimit) {
            std::cout << "  ... " << (report.sections.size() - listed)
                      << " more sections not listed\n";
            break;
        }
        ++listed;
        std::cout << "  " << std::setw(4) << std::setfill(' ') << section.index
                  << ' ' << (section.valid ? "ok     " : "invalid")
                  << " number " << section.number << " letter "
                  << (section.has_letter ? section.letter : '?') << '\n';
        std::cout << "       container " << section.offset << " objects "
                  << section.objects.size() << " instances "
                  << section.instances.size() << " nodes "
                  << section.nodes.size() << " tree depth "
                  << section.tree_depth << '\n';
        std::cout << "       header " << section.header_record_offset
                  << " prefix " << section.header_prefix_bytes
                  << " object list " << section.object_list_offset
                  << " instance list " << section.instance_list_offset
                  << " prefix " << section.instance_prefix_bytes
                  << " bound tree " << section.bound_tree_offset << '\n';
    }

    for (const nfsu::TrackIssue& issue : report.issues) {
        std::cerr << "region issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }

    const nfsu::TrackObjectCost& cost = report.cost;
    std::cout << "cost: region bytes " << cost.region_bytes << " chunks "
              << cost.chunks << " sections " << cost.valid_sections
              << " objects " << cost.objects << " instances " << cost.instances
              << " nodes " << cost.bound_nodes << '\n';
    std::cout << "cost: source object bytes " << cost.source_object_bytes
              << " instance bytes " << cost.source_instance_bytes
              << " node bytes " << cost.source_bound_node_bytes << '\n';
    std::cout << "cost: mesh slots used " << cost.mesh_slots_used
              << " distinct mesh hashes " << cost.distinct_mesh_hashes
              << " largest section instances "
              << cost.largest_section_instances << " deepest tree "
              << cost.deepest_tree << '\n';
    if (cost.has_bounds) {
        std::cout << "bounds: px " << cost.bounds_min[0] << " .. "
                  << cost.bounds_max[0] << " pz " << cost.bounds_min[1]
                  << " .. " << cost.bounds_max[1] << " py "
                  << cost.bounds_min[2] << " .. " << cost.bounds_max[2] << '\n';
    }

    if (!has_geometry) {
        std::cout << "geometry: no region given; mesh hashes are not resolved\n";
        return report.ok() ? 0 : 3;
    }

    std::vector<unsigned char> geometry_bytes;
    nfsu::TrackRegionSelector geometry_selector;
    geometry_selector.offset = geometry_offset;
    geometry_selector.size = geometry_size;
    const nfsu::TrackRegionReport geometry =
        nfsu::load_track_region(path, geometry_selector, geometry_bytes);
    std::cout << "geometry region: offset " << geometry.region.offset
              << " size " << geometry.region.size << " bytes read "
              << geometry_bytes.size() << " meshes " << geometry.meshes.size()
              << " valid " << geometry.valid_mesh_count() << '\n';
    for (const nfsu::TrackIssue& issue : geometry.issues) {
        std::cerr << "geometry issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }

    const nfsu::TrackObjectResolution resolution =
        nfsu::resolve_track_objects(report, geometry);
    std::cout << "slots: total " << resolution.slots_total << " empty "
              << resolution.slots_empty << " resolved "
              << resolution.slots_resolved << " unresolved "
              << resolution.slots_unresolved << '\n';
    std::cout << "instances: checked " << resolution.instances_checked
              << " bounds contain the mesh "
              << resolution.instances_bounds_contained << '\n';
    if (resolution.has_bounds) {
        std::cout << "world: px " << resolution.bounds_min[0] << " .. "
                  << resolution.bounds_max[0] << " pz "
                  << resolution.bounds_min[1] << " .. "
                  << resolution.bounds_max[1] << " py "
                  << resolution.bounds_min[2] << " .. "
                  << resolution.bounds_max[2] << '\n';
    }
    std::size_t shown = 0;
    for (const std::uint32_t hash : resolution.unresolved_hashes) {
        if (shown >= kTrackUnresolvedListLimit) {
            std::cout << "  ... "
                      << (resolution.unresolved_hashes.size() - shown)
                      << " more unresolved hashes not listed\n";
            break;
        }
        ++shown;
        std::cout << "  unresolved " << hex32(hash) << '\n';
    }
    return (report.ok() && geometry.ok()) ? 0 : 3;
}

// ---------------------------------------------------------------------------
// track-visibility: which scenery instances one camera can see in one
// explicitly selected region, under the rule recovered in
// docs/TRACK_VISIBILITY.md or under one of its explicit fallbacks.
// ---------------------------------------------------------------------------

constexpr std::size_t kTrackVisibleListLimit = 32;

// A finite float, with the whole argument consumed.
bool parse_f32(const std::string& text, float& out) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t used = 0;
        const float value = std::stof(text, &used);
        if (used != text.size() || !std::isfinite(value)) {
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// The render camera is stated in double, because OrbitCamera is: parsing a
// view angle as float and widening it would round the value the report then
// prints back at full precision.
bool parse_f64(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(value)) {
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

struct TrackVisibilityArgs {
    nfsu::TrackCamera camera;
    nfsu::TrackCullOptions options;
    std::size_t list_limit = kTrackVisibleListLimit;
    // --zones names a Tracks/TRACKB####.lzc and --zone-point a world point
    // in the two horizontal raw slots. Together they replace the residency
    // with the zone the recovered polygon test picks for that point, which
    // is what turns --residency zone from a caller-supplied list into the
    // game's own rule. Neither prints anything when both are absent, so the
    // commands docs/TRACK_VISIBILITY.md pins keep their exact output.
    bool has_zone_file = false;
    fs::path zone_file;
    bool has_zone_point = false;
    float zone_point[2] = {0.0F, 0.0F};
};

int run_track_visibility(
    const fs::path& path,
    std::uint64_t offset,
    std::uint64_t size,
    const TrackVisibilityArgs& args) {
    std::vector<unsigned char> region_bytes;
    nfsu::TrackRegionSelector selector;
    selector.offset = offset;
    selector.size = size;
    const nfsu::TrackObjectReport objects =
        nfsu::load_track_objects(path, selector, region_bytes);

    std::cout << "file: " << path.string() << '\n';
    std::cout << "region: offset " << objects.region.offset << " size "
              << objects.region.size << " bytes read " << region_bytes.size()
              << '\n';
    std::cout << "sections: " << objects.sections.size() << " valid "
              << objects.valid_section_count() << '\n';
    for (const nfsu::TrackIssue& issue : objects.issues) {
        std::cerr << "region issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }

    nfsu::TrackCullOptions options = args.options;
    if (args.has_zone_file != args.has_zone_point) {
        std::cerr << "error: --zones and --zone-point are used together\n";
        return 1;
    }
    // --zones replaces the residency outright, so a hand-written --zone
    // list beside it would be read by nothing. Refusing is the only
    // answer that cannot be mistaken for the list having been used.
    if (args.has_zone_file && !args.options.residency.zone_sections.empty()) {
        std::cerr << "error: --zones replaces the residency, so it cannot be "
                     "combined with --zone\n";
        return 1;
    }
    if (args.has_zone_file) {
        std::vector<unsigned char> zone_bytes;
        const nfsu::TrackZoneReport zones =
            nfsu::load_track_zones(args.zone_file, zone_bytes);
        for (const nfsu::TrackIssue& issue : zones.issues) {
            std::cerr << "zone issue at " << issue.offset << ": "
                      << issue.message << '\n';
        }
        if (!zones.valid) {
            std::cerr << "error: the zone container was refused\n";
            return 3;
        }
        const std::size_t picked = nfsu::find_track_zone(zones, args.zone_point);
        std::cout << "zones: file " << args.zone_file.string() << " polygons "
                  << zones.cost.polygons << " zones " << zones.cost.zones
                  << '\n';
        std::cout << "zone point: slot0 " << args.zone_point[0] << " slot1 "
                  << args.zone_point[1] << '\n';
        if (picked == nfsu::kTrackZoneNone) {
            // 0x00446424 `test ebp, ebp` / `jne`: no zone is the other arm
            // of 0x00446400, not a failure.
            options.residency = nfsu::TrackResidency{};
            options.residency.rule = nfsu::TrackResidencyRule::no_zone;
            std::cout << "zone: none; residency falls back to no-zone\n";
        } else if (!nfsu::track_zone_residency(zones, picked,
                                               options.residency)) {
            std::cerr << "error: the selected zone yields no residency\n";
            return 3;
        } else {
            const nfsu::TrackZone& zone = zones.zones[picked];
            std::cout << "zone: index " << picked << " key " << zone.key
                      << " polygon " << zone.polygon_index << " sections "
                      << zone.section_count << '\n';
            std::cout << "zone sections:";
            for (std::int32_t i = 0; i < zone.section_count; ++i) {
                std::cout << ' '
                          << zone.sections[static_cast<std::size_t>(i)];
            }
            std::cout << '\n';
        }
    }

    nfsu::TrackFrustum frustum;
    if (!nfsu::track_frustum_look_at(args.camera, frustum)) {
        std::cerr << "error: the camera is degenerate and builds no frustum\n";
        return 1;
    }

    const nfsu::TrackCamera& camera = args.camera;
    std::cout << "camera: world eye " << camera.eye[0] << ' ' << camera.eye[1]
              << ' ' << camera.eye[2] << " target " << camera.target[0] << ' '
              << camera.target[1] << ' ' << camera.target[2] << '\n';
    std::cout << "camera: fov " << camera.vertical_fov << " rad aspect "
              << camera.aspect << " near " << camera.near_plane << " far "
              << camera.far_plane << '\n';
    for (std::size_t i = 0; i < nfsu::kTrackFrustumPlanes; ++i) {
        const nfsu::TrackPlane& plane = frustum.planes[i];
        std::cout << "  plane " << i << " raw normal " << plane.normal[0] << ' '
                  << plane.normal[1] << ' ' << plane.normal[2] << " d "
                  << plane.d << '\n';
    }

    const nfsu::TrackCullReport report =
        nfsu::cull_track_region(objects, frustum, options);

    std::cout << "policy: requested "
              << nfsu::track_cull_policy_name(report.requested_policy)
              << " held " << (report.policy_held ? "yes" : "no") << '\n';
    std::cout << "sections: considered " << report.stats.sections_considered
              << " resident " << report.stats.sections_resident << " degraded "
              << report.stats.sections_degraded << '\n';
    for (const nfsu::TrackSectionCull& section : report.sections) {
        std::cout << "  section " << section.section_index << " number "
                  << section.number << " letter "
                  << (section.has_letter ? section.letter : '?')
                  << (section.resident ? " resident" : " not resident")
                  << " policy " << nfsu::track_cull_policy_name(section.policy_used)
                  << " containment "
                  << (!section.containment_checked
                          ? "unchecked"
                          : (section.containment_holds ? "holds" : "broken"))
                  << " instances " << section.instances << " visible "
                  << section.visible << '\n';
    }

    std::cout << "nodes: visited " << report.stats.nodes_visited << " tested "
              << report.stats.node_tests << " culled "
              << report.stats.nodes_culled << '\n';
    std::cout << "instances: visited " << report.stats.instances_visited
              << " pruned " << report.stats.instances_pruned << " box tests "
              << report.stats.instance_tests << '\n';
    std::cout << "culled: by mask " << report.stats.instances_culled_by_mask
              << " by box " << report.stats.instances_culled_by_box
              << " by size " << report.stats.instances_culled_by_size << '\n';
    std::cout << "visible: " << report.stats.instances_visible << '\n';

    std::size_t listed = 0;
    for (const nfsu::TrackVisibleInstance& item : report.visible) {
        if (listed >= args.list_limit) {
            std::cout << "  ... " << (report.visible.size() - listed)
                      << " more visible instances not listed\n";
            break;
        }
        ++listed;
        std::cout << "  section " << item.section_index << " instance "
                  << item.instance_index << " node " << item.node_index
                  << " node " << nfsu::track_box_class_name(item.node_class)
                  << " box " << nfsu::track_box_class_name(item.instance_class)
                  << " level " << item.screen_level << '\n';
    }

    for (const nfsu::TrackIssue& issue : report.issues) {
        std::cerr << "visibility issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    return (objects.ok() && report.ok()) ? 0 : 3;
}

// ---------------------------------------------------------------------------
// track-zones: the 0x80034150 visibility zones of one track, and which one
// holds a given world point. See docs/TRACK_ZONES.md.
// ---------------------------------------------------------------------------

constexpr std::size_t kTrackZoneListLimit = 16;

void print_track_zone(
    const nfsu::TrackZoneReport& report,
    const nfsu::TrackZone& zone,
    const char* indent) {
    std::cout << indent << "zone " << zone.index << " key " << zone.key
              << " polygon " << zone.polygon_index << " sections "
              << zone.section_count << " stored link "
              << zone.stored_link[0] << '/' << zone.stored_link[1]
              << " unknown " << zone.unknown << " polygon link "
              << zone.stored_polygon_link << '\n';
    std::cout << indent << "  sections:";
    for (std::int32_t i = 0; i < zone.section_count; ++i) {
        std::cout << ' ' << zone.sections[static_cast<std::size_t>(i)];
    }
    std::cout << '\n';
    if (zone.polygon_index >= report.polygons.size()) {
        return;
    }
    const nfsu::TrackZonePolygon& polygon = report.polygons[zone.polygon_index];
    std::cout << indent << "  polygon " << polygon.index << " key "
              << polygon.key << " vertices " << polygon.vertex_count << ':';
    for (std::int16_t v = 0; v < polygon.vertex_count; ++v) {
        std::cout << " (" << polygon.vertices[static_cast<std::size_t>(v)][0]
                  << ", " << polygon.vertices[static_cast<std::size_t>(v)][1]
                  << ')';
    }
    std::cout << '\n';
}

int run_track_zones(
    const fs::path& path,
    bool has_point,
    double point_slot0,
    double point_slot1,
    std::size_t list_limit) {
    std::vector<unsigned char> image;
    const nfsu::TrackZoneReport report = nfsu::load_track_zones(path, image);

    std::cout << "file: " << path.string() << '\n';
    std::cout << "file size: " << report.file_size << " image bytes "
              << image.size() << " unused packed bytes "
              << report.cost.unused_packed_bytes << '\n';
    const nfsu::TrackZoneCost& cost = report.cost;
    std::cout << "zones: " << (report.valid ? "valid" : "refused") << " chunks "
              << cost.chunks << " container children " << cost.container_children
              << " polygons " << cost.polygons << " zones " << cost.zones
              << " section lists " << report.section_lists.size() << '\n';
    std::cout << "cost: polygon vertices " << cost.polygon_vertices
              << " zone sections " << cost.zone_sections << '\n';
    if (cost.polygon_vertices != 0) {
        std::cout << "vertex bounds: slot0 " << cost.vertex_min[0] << " .. "
                  << cost.vertex_max[0] << " slot1 " << cost.vertex_min[1]
                  << " .. " << cost.vertex_max[1] << '\n';
    }

    for (const nfsu::TrackIssue& issue : report.issues) {
        std::cerr << "zone issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }
    if (!report.valid) {
        return 3;
    }

    for (const nfsu::TrackZoneSectionList& list : report.section_lists) {
        std::cout << "section list " << nfsu::format_chunk_id(list.chunk_id)
                  << " record " << list.index << " key " << list.key
                  << " count " << list.count << ":";
        for (std::int32_t i = 0; i < list.count; ++i) {
            std::cout << ' ' << list.sections[static_cast<std::size_t>(i)];
        }
        std::cout << '\n';
    }

    std::size_t listed = 0;
    for (const nfsu::TrackZone& zone : report.zones) {
        if (listed >= list_limit) {
            std::cout << "  ... " << (report.zones.size() - listed)
                      << " more zones not listed\n";
            break;
        }
        ++listed;
        print_track_zone(report, zone, "  ");
    }

    if (!has_point) {
        std::cout << "point: none given; no lookup was run\n";
        return 0;
    }

    const float point[2] = {static_cast<float>(point_slot0),
                            static_cast<float>(point_slot1)};
    std::cout << "point: slot0 " << point[0] << " slot1 " << point[1] << '\n';
    const std::size_t picked = nfsu::find_track_zone(report, point);
    if (picked == nfsu::kTrackZoneNone) {
        std::cout << "picked: none; retail falls back to the no-zone rule\n";
        return 0;
    }
    std::cout << "picked: zone " << picked << '\n';
    print_track_zone(report, report.zones[picked], "  ");
    return 0;
}

// ---------------------------------------------------------------------------
// track-scene: one bounded geometry region placed by the scenery region
// beside it, and the draw list one camera selects out of the two. See
// docs/TRACK_DRAW_LIST.md.
// ---------------------------------------------------------------------------

constexpr std::size_t kTrackDrawListLimit = 32;

struct TrackSceneArgs {
    std::vector<fs::path> textures;
    nfsu::TrackSceneOptions scene;
    nfsu::TrackDrawListOptions list;
    nfsu::TrackCullOptions cull;
    nfsu::TrackCamera camera;
    // Without a camera the run uses the fallback policy, which consults no
    // frustum at all, so the frustum built from the default camera below
    // cannot change its answer.
    bool has_camera = false;
    std::size_t list_limit = kTrackDrawListLimit;
};

// Decodes only the hashes `wanted` names out of the given sources. Both
// vectors are sized once, so the pointers taken here stay valid.
//
// `quiet` suppresses the per-source lines and nothing else. It exists for
// `track-budget`, which TIMES this call: anything printed inside a timed
// region is being timed, and a dozen formatted lines are neither free nor
// part of what texture decoding costs. The alternative was a second copy of
// this loop in that command, and a second place for the input-size budget
// below to be got wrong.
//
// IT HAS NO DEFAULT VALUE, deliberately. A default is the one setting of a
// parameter that no call site states, and flipping it here silences the
// per-source lines of `track-scene` and `track-render` - which nothing in
// this repository asserts, so an independent mutation pass flipped it and
// survived the whole suite. With two call sites, both in this file, and no
// default, that mutation does not exist to be caught rather than being
// caught by a test someone has to remember to write.
bool decode_track_scene_textures(
    const std::vector<fs::path>& paths,
    const std::vector<std::uint32_t>& wanted,
    std::vector<std::vector<unsigned char>>& source_bytes,
    std::vector<nfsu::TexturePackSet>& sets,
    std::vector<nfsu::RenderTextureSource>& sources,
    nfsu::RenderTextureSet& textures,
    bool quiet) {
    source_bytes.resize(paths.size());
    sets.resize(paths.size());
    sources.resize(paths.size());
    std::uint64_t read_bytes = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        nfsu::TextureLimits limits;
        // Clamped rather than left to the loop. read_bytes cannot pass the
        // constant today, because every file is capped by `remaining` on the
        // same iteration that adds it - but that is an emergent property of
        // this loop rather than a stated one, and an unsigned subtraction
        // that underflows gives a HUGE remaining and lifts the cap it exists
        // to impose. Stating it costs one comparison.
        const std::uint64_t remaining =
            read_bytes >= kMaxTrackTextureInputBytes
                ? 0
                : kMaxTrackTextureInputBytes - read_bytes;
        limits.max_file_size = std::min(limits.max_file_size, remaining);
        sets[i] = nfsu::load_texture_pack_set(paths[i], source_bytes[i], limits);
        read_bytes += source_bytes[i].size();
        if (!quiet) {
            std::cout << "texture source " << i << ": " << paths[i].string()
                      << " size " << sets[i].file_size << " read "
                      << source_bytes[i].size() << " packs "
                      << sets[i].packs.size() << " complete "
                      << (sets[i].complete ? 1 : 0) << '\n';
        }
        for (const nfsu::TextureIssue& issue : sets[i].issues) {
            std::cerr << "texture source " << i << " issue at " << issue.offset
                      << ": " << issue.message << '\n';
        }
    }
    for (std::size_t i = 0; i < paths.size(); ++i) {
        sources[i].bytes = &source_bytes[i];
        for (const nfsu::TexturePack& pack : sets[i].packs) {
            sources[i].packs.push_back(&pack);
        }
        sources[i].complete = sets[i].complete;
        sources[i].wanted = &wanted;
    }
    std::string error;
    if (!nfsu::decode_render_texture_sources(
            sources, nfsu::RenderTextureLimits{}, textures, error)) {
        std::cerr << "error: " << error << '\n';
        return false;
    }
    return true;
}

// Everything track-scene and track-render both do, and every line they both
// print. It lives here rather than in either command so that the two cannot
// drift: the scene, cull and draw-list diagnostics of a track-render run are
// the same bytes a track-scene run over the same region prints, and
// docs/TRACK_DRAW_LIST.md's acceptance numbers stay readable from either.
//
// Every buffer a later stage points into is a member, because
// nfsu::RenderTextureSource holds raw pointers into `source_bytes`, `sets`
// and `wanted`: returning the set out of a function that owned them on its
// stack is exactly the dangling this struct exists to prevent.
struct TrackScenePipeline {
    std::vector<unsigned char> geometry_bytes;
    std::vector<unsigned char> object_bytes;
    // The two decoded reports. They are members rather than locals
    // because track-probe reads their provenance - mesh container and
    // record offsets, section, object and instance records - after the
    // pipeline has run.
    nfsu::TrackRegionReport geometry;
    nfsu::TrackObjectReport objects;
    std::vector<std::uint32_t> wanted;
    std::vector<std::vector<unsigned char>> source_bytes;
    std::vector<nfsu::TexturePackSet> sets;
    std::vector<nfsu::RenderTextureSource> sources;
    nfsu::RenderTextureSet textures;
    nfsu::PreparedTrackScene scene;
    nfsu::TrackCullReport cull;
    // The frustum that cull was run with, kept for the same reason: a
    // probe that built a second one could describe a camera the report
    // never named.
    nfsu::TrackFrustum frustum;
    nfsu::TrackDrawList list;
    // geometry.ok() && objects.ok() && cull.ok(), which is what decides
    // between exit 0 and exit 3 for both commands.
    bool reports_ok = false;
};

// Returns 0 when the pipeline is built, or the exit status the command must
// return. A non-zero return has already printed its own diagnostic.
int build_track_scene_stage(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackSceneArgs& args,
    TrackScenePipeline& out) {
    out.geometry =
        nfsu::load_track_region(path, geometry_region, out.geometry_bytes);
    out.objects =
        nfsu::load_track_objects(path, object_region, out.object_bytes);
    const nfsu::TrackRegionReport& geometry = out.geometry;
    const nfsu::TrackObjectReport& objects = out.objects;

    std::cout << "file: " << path.string() << '\n';
    std::cout << "geometry region: offset " << geometry.region.offset
              << " size " << geometry.region.size << " meshes "
              << geometry.meshes.size() << " valid "
              << geometry.valid_mesh_count() << '\n';
    std::cout << "object region: offset " << objects.region.offset << " size "
              << objects.region.size << " sections " << objects.sections.size()
              << " valid " << objects.valid_section_count() << " instances "
              << objects.cost.instances << '\n';
    for (const nfsu::TrackIssue& issue : geometry.issues) {
        std::cerr << "geometry issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    for (const nfsu::TrackIssue& issue : objects.issues) {
        std::cerr << "object issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }

    out.wanted = nfsu::track_region_texture_hashes(geometry);
    const std::vector<std::uint32_t>& wanted = out.wanted;
    // Not quiet: track-scene and track-render both report their sources.
    if (!decode_track_scene_textures(args.textures, wanted, out.source_bytes,
                                     out.sets, out.sources, out.textures,
                                     false)) {
        return 2;
    }
    const nfsu::RenderTextureSet& textures = out.textures;
    std::cout << "textures: wanted " << wanted.size() << " decoded "
              << textures.textures.size() << " bytes " << textures.decoded_bytes
              << '\n';

    nfsu::PreparedTrackScene& scene = out.scene;
    std::string error;
    if (!nfsu::build_track_scene(geometry, objects, textures, args.scene, scene,
                                 error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    const nfsu::TrackDrawCost& draws = scene.region.cost;
    std::cout << "region: meshes " << draws.meshes << " draws " << draws.draws
              << " drawable " << draws.drawable_draws << " resolved "
              << draws.resolved_draws << " unresolved "
              << draws.unresolved_draws << " textures " << draws.textures
              << '\n';
    std::cout << "region: vertex buffer bytes " << draws.vertex_buffer_bytes
              << " index buffer bytes " << draws.index_buffer_bytes
              << " texture bytes " << draws.texture_bytes << '\n';

    const nfsu::TrackScenePlacementCost& cost = scene.cost;
    std::cout << "placement: slot " << scene.lod_slot << " sections "
              << cost.valid_sections << " of " << cost.source_sections
              << " instances " << cost.source_instances << " placed "
              << cost.placed_instances << " fallbacks " << cost.slot_fallbacks
              << '\n';
    std::cout << "unplaced: empty slot " << cost.unplaced_empty_slot
              << " skipped hash " << cost.unplaced_skipped_hash
              << " unresolved " << cost.unplaced_unresolved << " distinct "
              << cost.unplaced_hashes << '\n';
    std::cout << "meshes: drawn " << cost.drawn_meshes << " undrawn "
              << cost.undrawn_meshes << " resident entries "
              << cost.resident_entries << " resident triangles "
              << cost.resident_triangles << '\n';
    if (cost.has_bounds) {
        std::cout << "world: px " << cost.bounds_min[0] << " .. "
                  << cost.bounds_max[0] << " pz " << cost.bounds_min[1]
                  << " .. " << cost.bounds_max[1] << " py "
                  << cost.bounds_min[2] << " .. " << cost.bounds_max[2] << '\n';
    }
    std::size_t shown = 0;
    for (const std::uint32_t hash : scene.unplaced_hashes) {
        if (shown >= kTrackUnresolvedListLimit) {
            std::cout << "  ... " << (scene.unplaced_hashes.size() - shown)
                      << " more unplaced hashes not listed\n";
            break;
        }
        ++shown;
        std::cout << "  unplaced " << hex32(hash) << '\n';
    }

    return 0;
}

// The second half: the frustum one camera builds, the visible set it
// selects and the per-frame draw list. `camera` is a parameter rather
// than args.camera for one reason - track-probe frames its camera on the
// scene the stage above produced, so the camera cannot be known until
// that stage has run. track-scene and track-render pass args.camera and
// args.has_camera, so every byte they print is the byte they printed
// before this function was split in two.
int build_track_cull_stage(
    const nfsu::TrackCamera& camera,
    bool has_camera,
    const TrackSceneArgs& args,
    TrackScenePipeline& out) {
    const nfsu::TrackObjectReport& objects = out.objects;
    const nfsu::PreparedTrackScene& scene = out.scene;
    std::string error;
    if (!nfsu::track_frustum_look_at(camera, out.frustum)) {
        std::cerr << "error: the camera is degenerate and builds no frustum\n";
        return 1;
    }
    if (has_camera) {
        std::cout << "camera: world eye " << camera.eye[0] << ' '
                  << camera.eye[1] << ' ' << camera.eye[2]
                  << " target " << camera.target[0] << ' '
                  << camera.target[1] << ' ' << camera.target[2]
                  << " far " << camera.far_plane << '\n';
    } else {
        std::cout << "camera: none given; the fallback policy consults no"
                     " frustum\n";
    }

    out.cull = nfsu::cull_track_region(objects, out.frustum, args.cull);
    const nfsu::TrackCullReport& cull = out.cull;
    std::cout << "cull: policy "
              << nfsu::track_cull_policy_name(cull.requested_policy) << " held "
              << (cull.policy_held ? "yes" : "no") << " resident "
              << cull.stats.sections_resident << " degraded "
              << cull.stats.sections_degraded << " visible "
              << cull.stats.instances_visible << '\n';
    for (const nfsu::TrackIssue& issue : cull.issues) {
        std::cerr << "visibility issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }

    nfsu::TrackDrawList& list = out.list;
    if (!nfsu::build_track_draw_list(scene, cull, args.list, list, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    const nfsu::TrackDrawListCost& list_cost = list.cost;
    std::cout << "draw list: order " << nfsu::track_draw_order_name(list.order)
              << " visible " << list_cost.visible << " placed "
              << list_cost.visible_placed << " unplaced "
              << list_cost.visible_unplaced << " without groups "
              << list_cost.instances_without_entries << '\n';
    std::cout << "draw list: entries " << list_cost.entries << " triangles "
              << list_cost.triangles << " groups " << list_cost.groups
              << " meshes " << list_cost.meshes << " texture binds "
              << list_cost.texture_binds << '\n';

    out.reports_ok = out.geometry.ok() && objects.ok() && cull.ok();
    return 0;
}

// Both halves, in order.
int build_track_scene_pipeline(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackSceneArgs& args,
    TrackScenePipeline& out) {
    const int status = build_track_scene_stage(path, geometry_region,
                                              object_region, args, out);
    if (status != 0) {
        return status;
    }
    return build_track_cull_stage(args.camera, args.has_camera, args, out);
}

// The draw-list listing both commands end with. track-scene shows
// args.list_limit entries by default; track-render shows none unless asked,
// because its own report is what a reader of a rendered frame wants first.
void print_track_draw_list_entries(
    const TrackScenePipeline& pipeline, std::size_t limit) {
    std::size_t listed = 0;
    for (const nfsu::TrackDrawListEntry& entry : pipeline.list.entries) {
        if (listed >= limit) {
            std::cout << "  ... " << (pipeline.list.entries.size() - listed)
                      << " more entries not listed\n";
            break;
        }
        ++listed;
        const nfsu::PreparedTrackInstance& placed =
            pipeline.scene.instances[entry.instance];
        const nfsu::TrackDraw& draw = pipeline.scene.region.draws[entry.draw];
        std::cout << "  instance " << entry.instance << " section "
                  << placed.section_index << " source " << placed.instance_index
                  << " mesh " << placed.mesh << " draw " << entry.draw
                  << " indices " << draw.index_count << " texture "
                  << hex32(draw.texture_hash) << '\n';
    }
}

int run_track_scene(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackSceneArgs& args) {
    TrackScenePipeline pipeline;
    const int status = build_track_scene_pipeline(path, geometry_region,
                                                  object_region, args, pipeline);
    if (status != 0) {
        return status;
    }
    print_track_draw_list_entries(pipeline, args.list_limit);
    return pipeline.reports_ok ? 0 : 3;
}

// ---------------------------------------------------------------------------
// track-render: the same pipeline, drawn with the CPU reference renderer of
// docs/RENDERER.md and written as one TGA. It is the only probe command that
// renders; `obj` and `export-textures` already write derived files, so it is
// not the first that writes one.
//
// The output is never replaced: an existing target is refused before any
// input is opened, and nfsu::write_frame_tga refuses again and publishes
// through an exclusively created "<target>.partial".
// ---------------------------------------------------------------------------

struct TrackRenderArgs {
    TrackSceneArgs scene;
    fs::path output;
    nfsu::RenderOptions render;
    nfsu::TrackFramingOptions framing;
    // Each replaces the framed value for that field only. The rest of the
    // camera stays what frame_track_bounds computed, so one override does
    // not silently discard the framing.
    bool has_distance = false;
    double distance = 0.0;
    bool has_target = false;
    nfsu::Vec3 target;
    bool has_near = false;
    double near_plane = 0.0;
    bool has_far = false;
    double far_plane = 0.0;
    // Entries to list after the render report; none by default.
    std::size_t list_limit = 0;
};

void print_track_world_bounds(
    const char* label, const nfsu::TrackWorldBounds& bounds) {
    if (!bounds.valid) {
        std::cout << "render: world " << label << " none\n";
        return;
    }
    std::cout << "render: world " << label << " x " << format_double(bounds.min[0])
              << " .. " << format_double(bounds.max[0]) << " y "
              << format_double(bounds.min[1]) << " .. "
              << format_double(bounds.max[1]) << " z "
              << format_double(bounds.min[2]) << " .. "
              << format_double(bounds.max[2]) << " points " << bounds.points
              << '\n';
}

int run_track_render(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackRenderArgs& args) {
    // Before a byte is read. write_frame_tga repeats both refusals, and the
    // publication is atomic, so this is an early diagnostic rather than the
    // guarantee; the guarantee is there.
    {
        fs::path partial = args.output;
        partial += ".partial";
        std::vector<fs::path> inputs;
        inputs.push_back(path);
        inputs.insert(inputs.end(), args.scene.textures.begin(),
                      args.scene.textures.end());
        for (const fs::path& source : inputs) {
            if (nfsu::same_file_path(source, args.output) ||
                nfsu::same_file_path(source, partial)) {
                std::cerr << "error: --out must differ from every input file\n";
                return 1;
            }
        }
        std::error_code code;
        if (fs::exists(args.output, code)) {
            std::cerr << "error: --out " << args.output.string()
                      << " already exists; this command never replaces a file\n";
            return 1;
        }
    }

    TrackScenePipeline pipeline;
    const int status = build_track_scene_pipeline(path, geometry_region,
                                                  object_region, args.scene,
                                                  pipeline);
    if (status != 0) {
        return status;
    }

    std::string error;
    nfsu::TrackDrawListBounds bounds;
    if (!nfsu::track_draw_list_bounds(pipeline.scene, pipeline.list, bounds,
                                      error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    nfsu::RenderOptions options = args.render;
    bool framed = false;
    if (nfsu::frame_track_bounds(bounds.drawn, options.width, options.height,
                                 args.framing, options.camera, error)) {
        framed = true;
    } else if (!args.has_distance || !args.has_target) {
        std::cerr << "error: the region could not be framed automatically ("
                  << error
                  << "); give --view-distance and --view-target explicitly\n";
        return 2;
    } else {
        // The renderer's own defaults, with the framing angles applied, are
        // what the explicit distance and target are then measured against.
        // Report it: otherwise the reason the framing failed reaches
        // nobody, and the frame is not the one the options describe.
        std::cout << "render: framing failed (" << error
                  << "); drawing from the explicit distance and target\n";
        options.camera = nfsu::OrbitCamera{};
        options.camera.yaw_deg = args.framing.yaw_deg;
        options.camera.pitch_deg = args.framing.pitch_deg;
        options.camera.fov_y_deg = args.framing.fov_y_deg;
    }
    if (args.has_distance) {
        options.camera.distance = args.distance;
    }
    if (args.has_target) {
        options.camera.target = args.target;
    }
    if (args.has_near) {
        options.camera.near_plane = args.near_plane;
    }
    if (args.has_far) {
        options.camera.far_plane = args.far_plane;
    }
    if (!nfsu::validate_camera(options.camera, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track_stats;
    const nfsu::RenderLimits limits;
    if (!nfsu::render_track_draw_list(pipeline.scene, pipeline.list, options,
                                      limits, frame, stats, track_stats,
                                      error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    std::vector<fs::path> inputs;
    inputs.push_back(path);
    inputs.insert(inputs.end(), args.scene.textures.begin(),
                  args.scene.textures.end());
    nfsu::FrameWriteResult write;
    if (!nfsu::write_frame_tga(frame, args.output, inputs, write)) {
        std::cerr << "error: " << write.error << '\n';
        for (const fs::path& left : write.not_removed) {
            std::cerr << "error: left behind " << left.string() << '\n';
        }
        return 2;
    }

    const nfsu::Vec3 eye = nfsu::orbit_eye(options.camera);
    std::cout << "render: size " << frame.width() << 'x' << frame.height()
              << " camera " << (framed ? "framed" : "explicit") << '\n';
    std::cout << "render: camera yaw " << format_double(options.camera.yaw_deg)
              << " pitch " << format_double(options.camera.pitch_deg)
              << " distance " << format_double(options.camera.distance)
              << " target " << format_double(options.camera.target.x) << ','
              << format_double(options.camera.target.y) << ','
              << format_double(options.camera.target.z) << " fov_y "
              << format_double(options.camera.fov_y_deg) << " near "
              << format_double(options.camera.near_plane) << " far "
              << format_double(options.camera.far_plane) << '\n';
    std::cout << "render: eye " << format_double(eye.x) << ','
              << format_double(eye.y) << ',' << format_double(eye.z) << '\n';
    print_track_world_bounds("drawn", track_stats.bounds.drawn);
    print_track_world_bounds("stored", track_stats.bounds.stored);
    std::cout << "render: instances " << track_stats.bounds.instances
              << " without vertices "
              << track_stats.bounds.instances_without_vertices << '\n';
    std::cout << "render: entries " << track_stats.entries << " drawn "
              << track_stats.entries_drawn << " resolved "
              << track_stats.entries_resolved << " unresolved "
              << track_stats.entries_unresolved << " unusable "
              << track_stats.entries_unusable << " binds "
              << track_stats.texture_binds << '\n';
    std::cout << "render: drawn instances " << track_stats.instances_drawn
              << " meshes " << track_stats.meshes_drawn << " groups "
              << track_stats.groups << '\n';
    std::cout << "render: meshes " << stats.meshes << " rendered "
              << stats.meshes_rendered << " skipped invalid "
              << stats.meshes_skipped_invalid << " unresolved hashes "
              << stats.unresolved_hashes << '\n';
    std::cout << "render: triangles submitted " << stats.triangles_submitted
              << " textured " << stats.triangles_resolved << " fallback "
              << stats.triangles_fallback << " bad_index "
              << stats.triangles_bad_index << " degenerate_source "
              << stats.triangles_degenerate_source << '\n';
    std::cout << "render: triangles outside " << stats.triangles_outside
              << " clipped " << stats.triangles_clipped << " degenerate_screen "
              << stats.triangles_degenerate_screen << " culled "
              << stats.triangles_culled << " front " << stats.triangles_front
              << " back " << stats.triangles_back << " rasterized "
              << stats.triangles_rasterized << " raster_pieces "
              << stats.raster_triangles << '\n';
    std::cout << "render: coverage pixel_tests " << stats.pixel_tests
              << " fragments " << stats.fragments << " alpha_discarded "
              << stats.fragments_alpha_discarded << " far_discarded "
              << stats.fragments_far_discarded << " depth_failed "
              << stats.fragments_depth_failed << " written "
              << stats.fragments_written << '\n';
    std::cout << "render: pixels drawn " << stats.pixels_drawn << " fallback "
              << stats.pixels_fallback << '\n';
    if (stats.pixels_drawn > 0) {
        std::cout << "render: drawn bounds x " << stats.min_x << ".."
                  << stats.max_x << " y " << stats.min_y << ".." << stats.max_y
                  << '\n';
    } else {
        std::cout << "render: drawn bounds none\n";
    }
    std::cout << "wrote: " << args.output.string() << '\n';

    // Only when entries were actually asked for; see above.
    if (args.list_limit > 0) {
        print_track_draw_list_entries(pipeline, args.list_limit);
    }
    return pipeline.reports_ok ? 0 : 3;
}

// ---------------------------------------------------------------------------
// track-budget: what one bounded region costs, separated by the stage that
// pays it. See docs/TRACK_BUDGET.md.
//
// It runs the same four stages track-render runs, over the same inputs, and
// writes NO FILE AT ALL: the frame is rendered into memory and dropped, so
// this command cannot put a derived game asset on disk even by accident.
// That is why it has no --out and why it is not subject to the output
// guards track-render carries.
//
// TWO HALVES WITH DIFFERENT STABILITY, AND THEY ARE PRINTED APART.
//
//   * The `cost:` lines are counts. They are a function of the inputs, they
//     are byte-identical between runs, and they are what an automated limit
//     is written against. --no-time prints these and nothing else, so a
//     differential check over this command has something exact to compare.
//   * The `time:` lines are wall-clock milliseconds on whatever machine ran
//     them, and they are NOT reproducible: several sessions build
//     concurrently on this project's machine, so a stage timed against a
//     running link is slower for a reason that is not this code. They are
//     reported as a spread over --repeat repetitions - min, median, mean,
//     standard deviation - rather than as one number, because one number
//     from a shared machine reads like a fact and is not one.
//
// No --limit is ever a time, and check_track_budget cannot express one:
// nfsu::TrackBudgetCost holds no clock. docs/TRACK_BUDGET.md carries the
// reasoning and the measurements behind it.
//
// EVERY REPETITION'S COUNTS ARE COMPARED, not just timed. A repetition whose
// counter table differs from the first is reported and exits 5. That is a
// self-check on the claim this whole command rests on - that the counts are
// deterministic - and it is cheap: the repetitions are being run anyway.
// ---------------------------------------------------------------------------

// The five phases this command times. They are the four TrackBudgetStage
// stages plus texture decoding, which is called out on its own because it
// is large - 8 MB read and 3 MB decoded for the reference region - and
// would otherwise hide inside a neighbour. Its bytes are retained by the
// prepared region, so its COST is reported under prepare.texture_bytes;
// only its TIME is separate.
enum class TrackBudgetPhase {
    parse,
    texture,
    prepare,
    frame,
    render,
    count_,
};

const char* track_budget_phase_name(TrackBudgetPhase phase) {
    switch (phase) {
        case TrackBudgetPhase::parse:
            return "parse";
        case TrackBudgetPhase::texture:
            return "texture";
        case TrackBudgetPhase::prepare:
            return "prepare";
        case TrackBudgetPhase::frame:
            return "frame";
        case TrackBudgetPhase::render:
            return "render";
        case TrackBudgetPhase::count_:
            break;
    }
    return "unknown";
}

constexpr std::size_t kTrackBudgetPhaseCount =
    static_cast<std::size_t>(TrackBudgetPhase::count_);

constexpr std::uint64_t kMaxTrackBudgetRepeat = 1000;

struct TrackBudgetArgs {
    TrackSceneArgs scene;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    nfsu::TrackFramingOptions framing;
    std::uint64_t repeat = 5;
    bool render = true;
    bool show_time = true;
    std::vector<nfsu::TrackBudgetLimit> limits;
};

// The sample statistics and the repetition comparison are
// nfsu::summarize_track_samples and nfsu::track_budget_costs_equal, in
// nfsu/track_budget.h, rather than local copies here. That is not tidiness.
// A mutation sweep over an earlier revision of this file, recorded in
// docs/TRACK_BUDGET.md, found that EVERY fault in the local copies survived
// every gate, and for two reasons no case in
// tests/track_budget_guards.cmake can get around:
//
//   * a summary of wall-clock times cannot be asserted against a value, so
//     a swapped min and max, an unsorted sample set and a standard deviation
//     divided by n instead of n-1 all print numbers that look exactly as
//     plausible as the right ones;
//   * a comparison that only ever runs over costs that ARE equal never takes
//     its failing branch, so deleting the branch changes no output.
//
// As library functions both are driven directly by
// tests/track_budget_tests.cpp, with sample sets whose statistics are known
// exactly and with two costs built to differ.

double elapsed_ms(
    const std::chrono::steady_clock::time_point& from,
    const std::chrono::steady_clock::time_point& to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// One silent repetition of the whole pipeline. It prints nothing, because
// anything printed inside a timed region is being timed: the reference
// region's report is about forty lines of formatted output, which is not
// free and is not part of what any stage costs.
//
// Returns 0 on success, or the exit status the command must return; a
// non-zero return has set `error` unless it is 3, which means the reports
// carried issues and the measurement is over a region that did not fully
// parse.
int measure_track_budget_once(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackBudgetArgs& args,
    double phase_ms[kTrackBudgetPhaseCount],
    nfsu::TrackBudgetCost& cost,
    bool& reports_ok,
    bool& framed,
    std::string& error) {
    TrackScenePipeline pipeline;

    const auto t_parse_begin = std::chrono::steady_clock::now();
    const nfsu::TrackRegionReport geometry =
        nfsu::load_track_region(path, geometry_region, pipeline.geometry_bytes);
    const nfsu::TrackObjectReport objects =
        nfsu::load_track_objects(path, object_region, pipeline.object_bytes);
    const auto t_parse_end = std::chrono::steady_clock::now();
    phase_ms[static_cast<std::size_t>(TrackBudgetPhase::parse)] =
        elapsed_ms(t_parse_begin, t_parse_end);
    nfsu::track_budget_parse_cost(geometry, objects, cost);

    // The SAME function track-scene and track-render decode through, with
    // its printing suppressed. A second copy here would be a second place
    // for the texture budget to be got wrong, and would mean this command
    // timed a code path neither of the others runs.
    const auto t_texture_begin = std::chrono::steady_clock::now();
    pipeline.wanted = nfsu::track_region_texture_hashes(geometry);
    if (!decode_track_scene_textures(args.scene.textures, pipeline.wanted,
                                     pipeline.source_bytes, pipeline.sets,
                                     pipeline.sources, pipeline.textures,
                                     true)) {
        error = "the texture sources could not be decoded";
        return 2;
    }
    const auto t_texture_end = std::chrono::steady_clock::now();
    phase_ms[static_cast<std::size_t>(TrackBudgetPhase::texture)] =
        elapsed_ms(t_texture_begin, t_texture_end);

    const auto t_prepare_begin = std::chrono::steady_clock::now();
    if (!nfsu::build_track_scene(geometry, objects, pipeline.textures,
                                 args.scene.scene, pipeline.scene, error)) {
        return 2;
    }
    const auto t_prepare_end = std::chrono::steady_clock::now();
    phase_ms[static_cast<std::size_t>(TrackBudgetPhase::prepare)] =
        elapsed_ms(t_prepare_begin, t_prepare_end);
    nfsu::track_budget_prepare_cost(pipeline.scene, cost);

    nfsu::TrackFrustum frustum;
    if (!nfsu::track_frustum_look_at(args.scene.camera, frustum)) {
        error = "the camera is degenerate and builds no frustum";
        return 1;
    }
    const auto t_frame_begin = std::chrono::steady_clock::now();
    pipeline.cull =
        nfsu::cull_track_region(objects, frustum, args.scene.cull);
    if (!nfsu::build_track_draw_list(pipeline.scene, pipeline.cull,
                                     args.scene.list, pipeline.list, error)) {
        return 2;
    }
    const auto t_frame_end = std::chrono::steady_clock::now();
    phase_ms[static_cast<std::size_t>(TrackBudgetPhase::frame)] =
        elapsed_ms(t_frame_begin, t_frame_end);
    nfsu::track_budget_frame_cost(pipeline.list, cost);

    reports_ok = geometry.ok() && objects.ok() && pipeline.cull.ok();

    if (!args.render) {
        return 0;
    }

    // The camera is framed from the drawn bounds, exactly as track-render
    // frames it, and the framing is NOT timed: it walks every vertex the
    // list names, which is work a backend does once to choose a view and not
    // work the renderer pays per frame.
    nfsu::TrackDrawListBounds bounds;
    if (!nfsu::track_draw_list_bounds(pipeline.scene, pipeline.list, bounds,
                                      error)) {
        return 2;
    }
    nfsu::RenderOptions options;
    options.width = args.width;
    options.height = args.height;
    framed = bounds.drawn.valid;
    if (bounds.drawn.valid) {
        if (!nfsu::frame_track_bounds(bounds.drawn, options.width,
                                      options.height, args.framing,
                                      options.camera, error)) {
            // There is no explicit-camera fallback here, unlike
            // track-render: this command offers no explicit camera at all,
            // because a budget quoted from a view nobody named is not
            // reproducible. A box this far out or this large is the only way
            // to reach this, and the diagnostic says which.
            error = "the drawn region could not be framed (" + error +
                    "); this command has no explicit camera to fall back to";
            return 2;
        }
    } else {
        // NOTHING IS DRAWN, and that is a measurable case rather than an
        // error. There is no box to fit, so the framing rule has no input;
        // the documented empty-scene camera below replaces it. It is a
        // fixed camera with the framing angles applied, so the run is still
        // reproducible, and the run still measures the one render cost that
        // does not depend on geometry at all - render.frame_bytes, which for
        // the default 1280x720 is 15667200 bytes and is paid whether or not
        // a single triangle is submitted. That figure is larger than the
        // whole prepared region for the reference region, so a budget that
        // could not measure it would be missing its largest single item.
        options.camera = nfsu::OrbitCamera{};
        options.camera.yaw_deg = args.framing.yaw_deg;
        options.camera.pitch_deg = args.framing.pitch_deg;
        options.camera.fov_y_deg = args.framing.fov_y_deg;
        if (!nfsu::validate_camera(options.camera, error)) {
            return 2;
        }
    }

    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track_stats;
    const nfsu::RenderLimits limits;
    const auto t_render_begin = std::chrono::steady_clock::now();
    if (!nfsu::render_track_draw_list(pipeline.scene, pipeline.list, options,
                                      limits, frame, stats, track_stats,
                                      error)) {
        return 2;
    }
    const auto t_render_end = std::chrono::steady_clock::now();
    phase_ms[static_cast<std::size_t>(TrackBudgetPhase::render)] =
        elapsed_ms(t_render_begin, t_render_end);
    nfsu::track_budget_render_cost(stats, track_stats, frame, cost);
    return 0;
}

int run_track_budget(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackBudgetArgs& args) {
    std::cout << "budget: file " << path.string() << '\n';
    std::cout << "budget: geometry offset " << geometry_region.offset
              << " size " << geometry_region.size << " objects offset "
              << object_region.offset << " size " << object_region.size << '\n';
    std::cout << "budget: lod slot " << args.scene.scene.lod_slot << " order "
              << nfsu::track_draw_order_name(args.scene.list.order)
              << " camera " << (args.scene.has_camera ? "given" : "none")
              << " render " << (args.render ? "yes" : "no") << '\n';
    if (args.render) {
        std::cout << "budget: frame " << args.width << 'x' << args.height
                  << " yaw " << format_double(args.framing.yaw_deg) << " pitch "
                  << format_double(args.framing.pitch_deg) << " fov "
                  << format_double(args.framing.fov_y_deg) << " margin "
                  << format_double(args.framing.margin) << '\n';
    }

    // One discarded warm-up, always, and it is reported. The first parse of
    // a region reads it off the disk and every later one reads it out of the
    // operating system's file cache, so a first sample is a different
    // measurement from the rest and averaging it in produces a number that
    // describes neither.
    std::vector<double> samples[kTrackBudgetPhaseCount];
    nfsu::TrackBudgetCost first_cost;
    std::vector<nfsu::TrackBudgetEntry> first_entries;
    bool reports_ok = true;
    bool framed = true;
    std::string error;
    const std::uint64_t runs = args.repeat + 1;
    for (std::uint64_t run = 0; run < runs; ++run) {
        double phase_ms[kTrackBudgetPhaseCount] = {};
        nfsu::TrackBudgetCost cost;
        bool run_reports_ok = true;
        bool run_framed = true;
        const int status =
            measure_track_budget_once(path, geometry_region, object_region,
                                      args, phase_ms, cost, run_reports_ok,
                                      run_framed, error);
        if (status != 0) {
            std::cerr << "error: " << error << '\n';
            return status;
        }
        if (run == 0) {
            // The warm-up. Its counts are the reference the rest are
            // compared against; its times are dropped.
            first_cost = cost;
            nfsu::track_budget_entries(first_cost, first_entries);
            reports_ok = run_reports_ok;
            framed = run_framed;
            if (args.render && !framed) {
                std::cout << "budget: nothing is drawn, so there is no box to"
                             " frame; rendering with the documented"
                             " empty-scene camera\n";
            }
            continue;
        }
        std::string difference;
        if (!nfsu::track_budget_costs_equal(first_cost, cost, difference)) {
            std::cerr << "error: repetition " << run
                      << " disagreed with the first run about " << difference
                      << "; these counts are supposed to be a function of the"
                         " inputs, so this is a defect and not a budget"
                         " result\n";
            return 5;
        }
        if (run_reports_ok != reports_ok || run_framed != framed) {
            std::cerr << "error: repetition " << run
                      << " disagreed with the first run about whether the"
                         " reports were clean or the region was framed\n";
            return 5;
        }
        for (std::size_t p = 0; p < kTrackBudgetPhaseCount; ++p) {
            samples[p].push_back(phase_ms[p]);
        }
    }

    std::cout << "budget: repetitions " << args.repeat
              << " after 1 discarded warm-up, counts identical in all "
              << runs << " runs\n";

    for (const nfsu::TrackBudgetEntry& entry : first_entries) {
        std::cout << "cost: " << entry.name << ' ' << entry.value << '\n';
    }

    if (args.show_time) {
        for (std::size_t p = 0; p < kTrackBudgetPhaseCount; ++p) {
            const TrackBudgetPhase phase = static_cast<TrackBudgetPhase>(p);
            if (phase == TrackBudgetPhase::render && !args.render) {
                continue;
            }
            const nfsu::TrackSampleSummary timing =
                nfsu::summarize_track_samples(samples[p]);
            std::cout << "time: " << track_budget_phase_name(phase)
                      << " samples " << timing.samples << " min "
                      << format_double(timing.min) << " median "
                      << format_double(timing.median) << " mean "
                      << format_double(timing.mean) << " stddev "
                      << format_double(timing.stddev) << " max "
                      << format_double(timing.max) << " ms\n";
        }
    }

    std::vector<nfsu::TrackBudgetExceedance> exceedances;
    if (!nfsu::check_track_budget(first_cost, args.limits, exceedances,
                                  error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    for (const nfsu::TrackBudgetExceedance& over : exceedances) {
        std::cout << "budget: OVER " << over.name << ' ' << over.value
                  << " exceeds " << over.ceiling << '\n';
    }
    std::cout << "budget: limits " << args.limits.size() << " exceeded "
              << exceedances.size() << '\n';

    // WHICH OF THE TWO FAILURES THE SINGLE EXIT STATUS REPORTS, and why this
    // way round. Both lines are printed either way; only the status has to
    // choose, and the choice follows from which direction a damaged parse
    // can move a count.
    //
    // A region that did not fully parse yields counts that are too SMALL -
    // a mesh that failed to decode draws nothing, an unreadable section
    // places nothing. So a damaged parse can manufacture a budget that
    // PASSES and can never manufacture one that fails:
    //
    //   over budget, reports bad   the exceedance is real, and it happened
    //                              despite a parse that could only have
    //                              lowered the count. Exit 4.
    //   within budget, reports bad the pass is not trustworthy, because the
    //                              counts may be short. Exit 3.
    //
    // An earlier draft had these the other way round, on the reasoning that
    // a budget over a partial parse is not a budget. That is true of the
    // PASS and false of the exceedance, which is the asymmetry above.
    if (!exceedances.empty()) {
        return 4;
    }
    return reports_ok ? 0 : 3;
}

// ---------------------------------------------------------------------------
// track-probe: where every placed instance of one bounded region came from,
// what it draws, and why it is or is not on screen, from one free camera.
// It is the same pipeline track-scene and track-render build, and it writes
// nothing at all. See docs/TRACK_PROBE.md.
// ---------------------------------------------------------------------------

// Instance records printed by default, and sections printed at all. Both
// listings print a "listed N of M" line whether or not anything was listed,
// so neither has a trailer that only a non-empty listing could reach.
constexpr std::size_t kTrackProbeInstanceListLimit = 4;
constexpr std::size_t kTrackProbeSectionListLimit = 32;

struct TrackProbeArgs {
    TrackSceneArgs scene;
    nfsu::TrackProbeSelection select;
    nfsu::TrackFramingOptions framing;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    // The explicit camera. Both halves are given together or neither is.
    bool has_eye = false;
    nfsu::Vec3 eye;
    bool has_look_at = false;
    nfsu::Vec3 look_at;
    nfsu::Vec3 up{0.0, 1.0, 0.0};
    bool has_near = false;
    double near_plane = 0.0;
    bool has_far = false;
    double far_plane = 0.0;
    nfsu::TrackCameraMoves moves;
    std::size_t list_limit = kTrackProbeInstanceListLimit;
    bool use_screen_size = false;
    double screen_scale = 0.0;
    // --zones names a Tracks/TRACKB####.lzc and --zone-point a world point in
    // the two horizontal raw slots, exactly as track-visibility spells them.
    bool has_zone_file = false;
    fs::path zone_file;
    bool has_zone_point = false;
    float zone_point[2] = {0.0F, 0.0F};
};

std::string format_vec3(const nfsu::Vec3& value) {
    return format_double(value.x) + ',' + format_double(value.y) + ',' +
           format_double(value.z);
}

void print_track_probe_bounds(
    const char* indent,
    const char* label,
    const nfsu::TrackWorldBounds& bounds) {
    if (!bounds.valid) {
        std::cout << indent << label << ": none\n";
        return;
    }
    std::cout << indent << label << ": x " << format_double(bounds.min[0])
              << " .. " << format_double(bounds.max[0]) << " y "
              << format_double(bounds.min[1]) << " .. "
              << format_double(bounds.max[1]) << " z "
              << format_double(bounds.min[2]) << " .. "
              << format_double(bounds.max[2]) << " points " << bounds.points
              << '\n';
}

void print_track_probe_index(std::size_t value) {
    if (value == nfsu::kTrackProbeNone) {
        std::cout << "none";
    } else {
        std::cout << value;
    }
}

void print_track_instance_probe(
    const nfsu::TrackInstanceProbe& probe,
    const nfsu::PreparedTrackScene& scene) {
    std::cout << "  instance " << probe.section_index << ':'
              << probe.instance_index << " section number "
              << probe.section_number << " letter "
              << (probe.has_section_letter ? probe.section_letter : '?')
              << " object " << probe.object_index << '\n';
    std::cout << "    offsets: section " << probe.section_offset << " instance "
              << probe.instance_offset << " object " << probe.object_offset;
    if (probe.placed) {
        std::cout << " mesh container " << probe.mesh_container_offset
                  << " mesh record " << probe.mesh_record_offset
                  << " hash record ";
        if (probe.has_mesh_hash_record) {
            std::cout << probe.mesh_hash_record_offset;
        } else {
            std::cout << "none";
        }
    }
    std::cout << '\n';
    std::cout << "    object: radius " << format_float(probe.object_radius)
              << " flags " << hex32(probe.object_flags) << " flags2 "
              << hex32(probe.object_flags2) << " slots";
    for (std::size_t slot = 0; slot < nfsu::kSceneryObjectMeshSlots; ++slot) {
        std::cout << ' ' << hex32(probe.object_mesh_hash[slot]);
    }
    std::cout << '\n';
    std::cout << "    instance: flags " << hex32(probe.instance_flags)
              << " stored raw px " << probe.stored_min[0] << " .. "
              << probe.stored_max[0] << " pz " << probe.stored_min[1] << " .. "
              << probe.stored_max[1] << " py " << probe.stored_min[2] << " .. "
              << probe.stored_max[2] << '\n';
    std::cout << "    transform: linear";
    for (std::size_t k = 0; k < nfsu::kSceneryInstanceMatrixWords; ++k) {
        std::cout << ' ' << format_float(probe.transform.linear[k]);
    }
    std::cout << " translation " << format_float(probe.transform.translation[0])
              << ',' << format_float(probe.transform.translation[1]) << ','
              << format_float(probe.transform.translation[2]) << '\n';
    print_track_probe_bounds("    ", "stored world", probe.stored_world);
    print_track_probe_bounds("    ", "drawn world", probe.drawn_world);
    std::cout << "    residency: "
              << (probe.section_resident ? "resident" : "not resident")
              << " reason "
              << nfsu::track_residency_reason_name(probe.residency_reason)
              << " zone position ";
    print_track_probe_index(probe.zone_position);
    std::cout << '\n';
    std::cout << "    tree: policy "
              << nfsu::track_cull_policy_name(probe.policy_used) << " node ";
    print_track_probe_index(probe.node_index);
    std::cout << " depth " << probe.node_depth << " carried "
              << nfsu::track_box_class_name(probe.node_class) << " pruned by ";
    print_track_probe_index(probe.pruned_by_node);
    std::cout << '\n';
    std::cout << "    gates: mask "
              << nfsu::track_gate_verdict_name(probe.mask_gate) << " bits "
              << hex32(probe.mask_bits) << " box "
              << nfsu::track_gate_verdict_name(probe.box_gate) << " tested "
              << nfsu::track_box_class_name(probe.instance_class) << " own box "
              << nfsu::track_box_class_name(probe.own_box_class) << " size "
              << nfsu::track_gate_verdict_name(probe.size_gate) << " level ";
    if (probe.screen_level_measured) {
        std::cout << probe.screen_level;
    } else {
        std::cout << "unmeasured";
    }
    std::cout << '\n';
    std::cout << "    visible: " << (probe.visible ? "yes" : "no") << " culler "
              << (probe.culler_visible ? "yes" : "no") << '\n';
    if (!probe.placed) {
        std::cout << "    placement: none reason ";
        if (probe.has_unplaced_reason) {
            std::cout << nfsu::track_unplaced_reason_name(probe.unplaced_reason);
        } else {
            std::cout << "unrecorded";
        }
        std::cout << '\n';
        return;
    }
    std::cout << "    placement: mesh " << probe.mesh << " '" << probe.mesh_name
              << "' hash " << hex32(probe.mesh_hash) << " source mesh "
              << probe.source_mesh_index << " slot " << probe.slot
              << " requested " << probe.requested_slot << " fallback "
              << (probe.slot_fallback_used ? "yes" : "no") << " scene instance "
              << probe.scene_instance << '\n';
    std::cout << "    materials: groups " << probe.draw_count << " drawable "
              << probe.groups_drawable << " triangles " << probe.triangles
              << " resolved " << probe.groups_resolved << " unresolved "
              << probe.groups_unresolved << " unusable "
              << probe.groups_unusable << " invalid slot "
              << probe.groups_invalid_slot << '\n';
    for (std::size_t d = 0; d < probe.draw_count; ++d) {
        const std::size_t index = probe.first_draw + d;
        const nfsu::TrackDraw& draw = scene.region.draws[index];
        std::cout << "      group " << index << " material "
                  << draw.material_index << " record " << draw.record_offset
                  << " first " << draw.first_index << " count "
                  << draw.index_count << " slot " << draw.texture_slot
                  << " hash " << hex32(draw.texture_hash) << " binding "
                  << nfsu::binding_kind_name(draw.binding) << " drawable "
                  << (draw.drawable ? "yes" : "no") << '\n';
        std::cout << "        texture ";
        if (draw.binding == nfsu::BindingKind::resolved) {
            std::cout << draw.texture << " source " << draw.texture_source
                      << " pack " << draw.texture_pack << " record "
                      << draw.texture_record;
        } else {
            std::cout << "none";
        }
        std::cout << " key slot " << draw.material_key_slot << " flags "
                  << hex32(draw.flags) << " record bounds min "
                  << format_float(draw.bounds_min[0]) << ','
                  << format_float(draw.bounds_min[1]) << ','
                  << format_float(draw.bounds_min[2]) << " max "
                  << format_float(draw.bounds_max[0]) << ','
                  << format_float(draw.bounds_max[1]) << ','
                  << format_float(draw.bounds_max[2]) << '\n';
    }
}

// The region's own bounds: the drawn vertices of every instance the scene
// placed, selected by no camera at all.
//
// It has to be camera-independent, because the camera is what decides the
// visible set and the default camera is framed from these bounds. A default
// framed on what a camera sees would be a camera defined in terms of itself,
// and two runs at different angles could not be compared - which is the
// property docs/TRACK_RENDER.md chose the sphere fit for.
//
// The throwaway draw list below is the reuse that keeps this honest: the
// drawn box comes out of nfsu::track_draw_list_bounds, the same measurement
// track-render frames on, rather than out of a second vertex walk here.
bool track_probe_region_bounds(
    const nfsu::PreparedTrackScene& scene,
    nfsu::TrackWorldBounds& bounds,
    std::string& error) {
    std::vector<nfsu::TrackVisibleInstance> everything;
    for (const nfsu::TrackSceneSection& section : scene.sections) {
        for (std::size_t k = 0; k < section.source_instances; ++k) {
            nfsu::TrackVisibleInstance item;
            item.section_index = section.section_index;
            item.instance_index = k;
            everything.push_back(item);
        }
    }
    nfsu::TrackDrawList list;
    if (!nfsu::build_track_draw_list(scene, everything,
                                     nfsu::TrackDrawListOptions{}, list,
                                     error)) {
        return false;
    }
    nfsu::TrackDrawListBounds measured;
    if (!nfsu::track_draw_list_bounds(scene, list, measured, error)) {
        return false;
    }
    bounds = measured.drawn;
    return true;
}

int run_track_probe(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackProbeArgs& args) {
    TrackScenePipeline pipeline;
    TrackSceneArgs scene_args = args.scene;
    // A probe always has a camera: unlike track-scene there is no arm of this
    // command that consults no frustum.
    scene_args.has_camera = true;
    int status = build_track_scene_stage(path, geometry_region, object_region,
                                         scene_args, pipeline);
    if (status != 0) {
        return status;
    }

    // --zones replaces the residency with the zone the recovered polygon test
    // picks for the point, which is what turns "resident" from a caller's
    // assertion into the game's own rule. The parser has already refused it
    // beside --residency and --zone, so nothing here can be overwritten.
    if (args.has_zone_file) {
        std::vector<unsigned char> zone_bytes;
        const nfsu::TrackZoneReport zones =
            nfsu::load_track_zones(args.zone_file, zone_bytes);
        for (const nfsu::TrackIssue& issue : zones.issues) {
            std::cerr << "zone issue at " << issue.offset << ": "
                      << issue.message << '\n';
        }
        if (!zones.valid) {
            std::cerr << "error: the zone container was refused\n";
            return 3;
        }
        const std::size_t picked = nfsu::find_track_zone(zones, args.zone_point);
        std::cout << "zones: file " << args.zone_file.string() << " polygons "
                  << zones.cost.polygons << " zones " << zones.cost.zones
                  << '\n';
        std::cout << "zone point: slot0 " << format_float(args.zone_point[0])
                  << " slot1 " << format_float(args.zone_point[1]) << '\n';
        if (picked == nfsu::kTrackZoneNone) {
            scene_args.cull.residency = nfsu::TrackResidency{};
            scene_args.cull.residency.rule = nfsu::TrackResidencyRule::no_zone;
            std::cout << "zone: none; residency falls back to no-zone\n";
        } else if (!nfsu::track_zone_residency(zones, picked,
                                               scene_args.cull.residency)) {
            std::cerr << "error: the selected zone yields no residency\n";
            return 3;
        } else {
            const nfsu::TrackZone& zone = zones.zones[picked];
            std::cout << "zone: index " << picked << " key " << zone.key
                      << " polygon " << zone.polygon_index << " sections "
                      << zone.section_count << '\n';
        }
    }

    nfsu::TrackWorldBounds region_bounds;
    std::string error;
    if (!track_probe_region_bounds(pipeline.scene, region_bounds, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    print_track_probe_bounds("", "probe: region bounds", region_bounds);

    nfsu::TrackFreeCamera camera;
    bool framed = false;
    if (args.has_eye) {
        camera.eye = args.eye;
        camera.target = args.look_at;
        camera.fov_y_deg = args.framing.fov_y_deg;
        camera.width = args.width;
        camera.height = args.height;
    } else if (nfsu::track_frame_free_camera(region_bounds, args.framing,
                                             args.width, args.height, camera,
                                             error)) {
        framed = true;
    } else {
        std::cerr << "error: the region could not be framed automatically ("
                  << error << "); give --eye and --look-at explicitly\n";
        return 2;
    }
    camera.up = args.up;
    if (args.has_near) {
        camera.near_plane = args.near_plane;
    }
    if (args.has_far) {
        camera.far_plane = args.far_plane;
    }
    if (!nfsu::validate_track_free_camera(camera, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    nfsu::TrackFreeCamera moved;
    if (!nfsu::track_move_free_camera(camera, args.moves, moved, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    nfsu::TrackCameraBasis basis;
    if (!nfsu::track_camera_basis(moved, basis, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    nfsu::TrackCamera cull_camera;
    if (!nfsu::track_cull_camera_of(moved, cull_camera, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    // The screen-size gate reads the SAME camera. track-visibility asks for
    // its eye and its forward axis separately, which lets a caller evaluate
    // the gate from somewhere the frustum is not; there is one camera here
    // and it supplies both, in the raw slot order the gate wants.
    if (args.use_screen_size) {
        scene_args.cull.use_screen_size = true;
        scene_args.cull.screen_scale = static_cast<float>(args.screen_scale);
        const float eye_world[3] = {static_cast<float>(moved.eye.x),
                                    static_cast<float>(moved.eye.y),
                                    static_cast<float>(moved.eye.z)};
        nfsu::track_raw_from_world(eye_world, scene_args.cull.screen_eye);
        const float forward_world[3] = {static_cast<float>(basis.forward.x),
                                        static_cast<float>(basis.forward.y),
                                        static_cast<float>(basis.forward.z)};
        nfsu::track_raw_from_world(forward_world,
                                   scene_args.cull.screen_forward);
    }

    std::cout << "probe: camera " << (framed ? "framed" : "explicit")
              << " moves turn ";
    if (args.moves.has_turn) {
        std::cout << format_double(args.moves.turn_yaw_deg) << ','
                  << format_double(args.moves.turn_pitch_deg);
    } else {
        std::cout << "none";
    }
    std::cout << " fly ";
    if (args.moves.has_fly) {
        std::cout << format_double(args.moves.fly_forward) << ','
                  << format_double(args.moves.fly_right) << ','
                  << format_double(args.moves.fly_up);
    } else {
        std::cout << "none";
    }
    std::cout << '\n';
    std::cout << "probe: camera eye " << format_vec3(moved.eye) << " target "
              << format_vec3(moved.target) << " up " << format_vec3(moved.up)
              << '\n';
    std::cout << "probe: camera forward " << format_vec3(basis.forward)
              << " right " << format_vec3(basis.right) << " up-axis "
              << format_vec3(basis.up_axis) << " distance "
              << format_double(basis.distance) << '\n';
    std::cout << "probe: camera fov_y " << format_double(moved.fov_y_deg)
              << " frame " << moved.width << 'x' << moved.height << " aspect "
              << format_float(cull_camera.aspect) << " near "
              << format_double(moved.near_plane) << " far "
              << format_double(moved.far_plane) << '\n';

    status = build_track_cull_stage(cull_camera, true, scene_args, pipeline);
    if (status != 0) {
        return status;
    }

    for (std::size_t i = 0; i < nfsu::kTrackFrustumPlanes; ++i) {
        const nfsu::TrackPlane& plane = pipeline.frustum.planes[i];
        std::cout << "probe: plane " << i << " raw normal "
                  << format_float(plane.normal[0]) << ','
                  << format_float(plane.normal[1]) << ','
                  << format_float(plane.normal[2]) << " d "
                  << format_float(plane.d) << '\n';
    }

    nfsu::TrackProbeOptions options;
    options.cull = scene_args.cull;
    options.select = args.select;
    nfsu::TrackProbeReport report;
    if (!nfsu::probe_track_scene(pipeline.geometry, pipeline.objects,
                                 pipeline.scene, pipeline.cull,
                                 pipeline.frustum, options, report, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    const nfsu::TrackProbeCost& cost = report.cost;
    std::cout << "probe: residency rule ";
    switch (scene_args.cull.residency.rule) {
        case nfsu::TrackResidencyRule::every_section:
            std::cout << "every";
            break;
        case nfsu::TrackResidencyRule::no_zone:
            std::cout << "no-zone";
            break;
        case nfsu::TrackResidencyRule::zone_list:
            std::cout << "zone";
            break;
    }
    std::cout << " zone sections "
              << scene_args.cull.residency.zone_sections.size() << " mask "
              << hex32(scene_args.cull.instance_mask) << " screen scale ";
    if (scene_args.cull.use_screen_size) {
        std::cout << format_float(scene_args.cull.screen_scale);
    } else {
        std::cout << "off";
    }
    std::cout << '\n';
    std::cout << "probe: sections " << cost.sections << " valid "
              << cost.valid_sections << " resident " << cost.resident_sections
              << '\n';
    std::cout << "probe: instances " << cost.instances << " probed "
              << cost.probed << " visible " << cost.visible << " culler "
              << cost.culler_visible << " disagreements " << cost.disagreements
              << '\n';
    std::cout << "probe: rejected by mask " << cost.rejected_by_mask << " box "
              << cost.rejected_by_box << " size " << cost.rejected_by_size
              << " pruned " << cost.pruned << '\n';
    std::cout << "probe: placed " << cost.placed << " unplaced "
              << cost.unplaced << " groups " << cost.groups << " triangles "
              << cost.triangles << '\n';

    const std::size_t sections_listed =
        std::min(report.sections.size(), kTrackProbeSectionListLimit);
    std::cout << "probe: listed " << sections_listed << " of "
              << report.sections.size() << " sections\n";
    for (std::size_t i = 0; i < sections_listed; ++i) {
        const nfsu::TrackSectionProbe& section = report.sections[i];
        std::cout << "  section " << section.section_index << " number "
                  << section.number << " letter "
                  << (section.has_letter ? section.letter : '?') << " offset "
                  << section.offset << " valid "
                  << (section.valid ? "yes" : "no") << '\n';
        std::cout << "    residency: "
                  << (section.resident ? "resident" : "not resident")
                  << " reason "
                  << nfsu::track_residency_reason_name(section.residency_reason)
                  << " zone position ";
        print_track_probe_index(section.zone_position);
        std::cout << '\n';
        std::cout << "    policy " << nfsu::track_cull_policy_name(
                                          section.policy_used)
                  << " containment "
                  << (!section.containment_checked
                          ? "unchecked"
                          : (section.containment_holds ? "holds" : "broken"))
                  << " nodes " << section.nodes << " depth "
                  << section.tree_depth << " tree walked "
                  << (section.tree_walked ? "yes" : "no") << '\n';
        std::cout << "    instances " << section.instances << " probed "
                  << section.probed << " visible " << section.visible
                  << " culler " << section.culler_visible << " placed "
                  << section.placed << '\n';
        std::cout << "    rejected: mask " << section.rejected_by_mask
                  << " box " << section.rejected_by_box << " size "
                  << section.rejected_by_size << " pruned " << section.pruned
                  << '\n';
    }

    const std::size_t listed =
        std::min(report.instances.size(), args.list_limit);
    std::cout << "probe: listed " << listed << " of "
              << report.instances.size() << " instances\n";
    for (std::size_t i = 0; i < listed; ++i) {
        print_track_instance_probe(report.instances[i], pipeline.scene);
    }

    for (const nfsu::TrackIssue& issue : report.issues) {
        std::cerr << "probe issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }
    return (pipeline.reports_ok && report.ok()) ? 0 : 3;
}

// ---------------------------------------------------------------------------
// body-skin-inputs: read-only diagnostics for the assembled layer list.
// It opens only the files named on the command line, and it writes nothing.
// ---------------------------------------------------------------------------

struct LoadedSkinSource {
    std::vector<unsigned char> bytes;
    nfsu::TexturePack pack;
    std::string path;
    nfsu::SkinSourceKind kind = nfsu::SkinSourceKind::other;
};

bool parse_u32_arg(const std::string& text, std::uint32_t limit, std::uint32_t& out) {
    if (text.empty() || text.size() > 10) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10U + static_cast<std::uint64_t>(c - '0');
        if (value > limit) {
            return false;
        }
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parse_size_arg(const std::string& text, std::uint32_t& width, std::uint32_t& height) {
    const std::size_t x = text.find('x');
    if (x == std::string::npos) {
        return false;
    }
    return parse_u32_arg(text.substr(0, x), 65535U, width) &&
           parse_u32_arg(text.substr(x + 1), 65535U, height);
}

bool parse_paint_args(char** argv, std::uint32_t& out) {
    std::uint32_t channel[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        if (!parse_u32_arg(argv[i], 255U, channel[i])) {
            return false;
        }
    }
    out = nfsu::skin_pack_paint(static_cast<std::uint8_t>(channel[0]),
                                static_cast<std::uint8_t>(channel[1]),
                                static_cast<std::uint8_t>(channel[2]),
                                static_cast<std::uint8_t>(channel[3]));
    return true;
}

bool parse_palette_arg(const std::string& text, std::optional<std::uint32_t>& out) {
    if (text == "default") {
        out.reset();
        return true;
    }
    std::uint32_t value = 0;
    if (!parse_u32_arg(text, 256U, value)) {
        return false;
    }
    out = value;
    return true;
}

void print_skin_provenance(const char* label, const nfsu::SkinTextureProvenance& found) {
    std::cout << "    " << label << ": ";
    if (!found.resolved) {
        std::cout << "unresolved\n";
        return;
    }
    std::cout << "source " << found.source_index << " pack " << found.pack_index << " ("
              << nfsu::skin_source_kind_name(found.source_kind) << ") " << found.source_path
              << '\n';
    std::cout << "      pack " << found.pack_name << " root " << found.pack_root_offset
              << " hash " << hex32(found.pack_hash) << '\n';
    std::cout << "      record " << found.record_index << ' '
              << (found.record_valid ? "ok" : "invalid") << " info " << hex32(found.record_hash)
              << " table " << hex32(found.table_hash) << " blob " << hex32(found.blob_hash)
              << '\n';
    std::cout << "      name \"" << found.stored_name << "\" field_full "
              << (found.stored_name_full ? 1 : 0) << " name_hash "
              << (found.stored_name_matches ? "matches" : "differs") << '\n';
    std::cout << "      format " << nfsu::pixel_format_name(found.format) << " code "
              << static_cast<unsigned int>(found.format_code) << " d3d " << found.d3d_format
              << ' ' << found.width << 'x' << found.height << " mips " << found.mip_count
              << " data " << found.data_size << " palette_bytes " << found.record_palette_bytes
              << " top " << found.top_level_size << '\n';
    std::cout << "      storage " << nfsu::texture_storage_name(found.storage) << " info_offset "
              << found.info_offset << " blob " << found.blob_offset << '+' << found.blob_size
              << '\n';
    if (!found.record_valid && !found.record_problem.empty()) {
        std::cout << "      problem: " << found.record_problem << '\n';
    }
    for (const nfsu::SkinHashClaim& claim : found.also_claimed_by) {
        std::cout << "      also claimed by source " << claim.source << " pack " << claim.pack
                  << " record " << claim.record << '\n';
    }
}

void print_skin_input(const nfsu::SkinInputLayerInfo& input) {
    std::cout << "  " << nfsu::skin_source_role_name(input.role) << ": \"" << input.source_name
              << "\" " << hex32(input.source_hash) << " mask \"" << input.mask_name << "\" "
              << hex32(input.mask_hash) << '\n';
    std::cout << "    numcolours " << input.palette_entries
              << (input.palette_entries_defaulted ? " (defaulted)" : "") << " remap "
              << (input.remap ? 1 : 0);
    if (input.remap) {
        std::cout << " colors";
        for (std::size_t k = 0; k < input.remap_colors.size(); ++k) {
            std::cout << ' ' << hex32(input.remap_colors[k])
                      << (input.remap_color_defaulted[k] ? "*" : "");
        }
    }
    std::cout << '\n';
    print_skin_provenance("source", input.source);
    print_skin_provenance("mask", input.mask);
    if (!input.usable) {
        std::cout << "    skipped (" << nfsu::skin_skip_reason_name(input.skip_reason)
                  << "): " << input.skip_detail << '\n';
        return;
    }
    std::cout << "    layer " << input.layer_index;
    if (input.assembly) {
        const nfsu::SkinAssembledLayerInfo& made = *input.assembly;
        std::cout << " source_expanded " << (made.source_expanded ? 1 : 0) << " mask_expanded "
                  << (made.mask_expanded ? 1 : 0) << " owned " << made.owned_bytes;
        if (made.source_indices_used != 0) {
            std::cout << " source_entries " << made.source_indices_used
                      << (made.source_indices_exceed_palette ? " (over numcolours)" : "");
        }
        if (made.mask_indices_used != 0) {
            std::cout << " mask_entries " << made.mask_indices_used;
        }
    }
    std::cout << '\n';
}

void print_skin_plan(const nfsu::BodySkinInputPlan& plan) {
    std::cout << "target: " << nfsu::skin_target_type_name(plan.target) << ' ' << plan.width
              << 'x' << plan.height << '\n';
    if (plan.target_hash) {
        if (plan.target_texture.resolved) {
            print_skin_provenance("target texture", plan.target_texture);
        } else {
            std::cout << "    target texture: " << hex32(*plan.target_hash)
                      << " is in no source\n";
        }
    }
    std::cout << "base color: " << hex32(plan.base_color) << '\n';
    std::cout << "special colors:";
    for (std::size_t k = 0; k < plan.special_colors.size(); ++k) {
        std::cout << ' ' << hex32(plan.special_colors[k])
                  << (plan.special_color_defaulted[k] ? "*" : "");
    }
    std::cout << "   (* = fell back to the base color)\n";
    std::cout << "inputs: " << plan.inputs.size() << " selected, " << plan.usable_inputs
              << " usable, " << plan.skipped_inputs << " skipped\n";
    for (const nfsu::SkinInputLayerInfo& input : plan.inputs) {
        print_skin_input(input);
    }
    if (plan.target == nfsu::SkinTargetType::type8) {
        std::cout << "merged palette entries: " << plan.merged_palette_entries << " fits "
                  << (plan.merged_palette_fits ? 1 : 0) << '\n';
    }
}

int run_body_skin_inputs(int argc, char** argv) {
    // argv[2] is the car name; every other input is an explicit option.
    nfsu::BodySkinSelection selection;
    selection.car_name = argv[2];
    std::vector<std::unique_ptr<LoadedSkinSource>> loaded;
    bool assemble = false;

    for (int i = 3; i < argc;) {
        const std::string option = argv[i];
        const int left = argc - i - 1;
        if (option == "--assemble") {
            assemble = true;
            i += 1;
        } else if (option == "--size" && left >= 1) {
            if (!parse_size_arg(argv[i + 1], selection.width, selection.height)) {
                std::cerr << "bad --size, expected WIDTHxHEIGHT\n";
                return 1;
            }
            i += 2;
        } else if (option == "--target-texture" && left >= 1) {
            selection.target_hash = nfsu::bin_hash(argv[i + 1]);
            i += 2;
        } else if (option == "--target" && left >= 1) {
            const std::string value = argv[i + 1];
            if (value == "type32") {
                selection.target = nfsu::SkinTargetType::type32;
            } else if (value == "type8") {
                selection.target = nfsu::SkinTargetType::type8;
            } else {
                std::cerr << "bad --target, expected type32 or type8\n";
                return 1;
            }
            i += 2;
        } else if (option == "--source" && left >= 2) {
            const std::string kind = argv[i + 1];
            auto entry = std::unique_ptr<LoadedSkinSource>(new LoadedSkinSource());
            if (kind == "car-textures") {
                entry->kind = nfsu::SkinSourceKind::car_textures;
            } else if (kind == "car-vinyls") {
                entry->kind = nfsu::SkinSourceKind::car_vinyls;
            } else if (kind == "other") {
                entry->kind = nfsu::SkinSourceKind::other;
            } else {
                std::cerr << "bad --source kind, expected car-textures, car-vinyls or other\n";
                return 1;
            }
            entry->path = argv[i + 2];
            entry->pack = nfsu::load_texture_pack(
                nfsu::argument_path(entry->path.c_str()), entry->bytes);
            loaded.push_back(std::move(entry));
            i += 3;
        } else if (option == "--hood" && left >= 1) {
            nfsu::SkinHoodSpoilerSelection hood;
            const std::string variant = argv[i + 1];
            if (variant == "hood-spoiler") {
                hood.variant = nfsu::SkinHoodSpoilerVariant::hood_and_spoiler;
            } else if (variant == "hood") {
                hood.variant = nfsu::SkinHoodSpoilerVariant::hood;
            } else if (variant == "spoiler") {
                hood.variant = nfsu::SkinHoodSpoilerVariant::spoiler;
            } else {
                std::cerr << "bad --hood variant, expected hood-spoiler, hood or spoiler\n";
                return 1;
            }
            selection.hood_spoiler_vinyl = hood;
            i += 2;
        } else if (option == "--vinyl" && left >= 4) {
            std::uint32_t index = 0;
            if (!parse_u32_arg(argv[i + 1], 3U, index)) {
                std::cerr << "bad --vinyl layer, expected 0..3\n";
                return 1;
            }
            if (selection.vinyl_layers[index]) {
                // Replacing it would silently drop any --vinyl-color
                // already given for that layer.
                std::cerr << "--vinyl names a layer that was already selected\n";
                return 1;
            }
            nfsu::SkinVinylLayerSelection layer;
            layer.texture = argv[i + 2];
            if (!parse_palette_arg(argv[i + 3], layer.palette_size)) {
                std::cerr << "bad --vinyl NUMCOLOURS, expected 0..256 or default\n";
                return 1;
            }
            const std::string remap = argv[i + 4];
            if (remap == "remap") {
                layer.remap = true;
            } else if (remap != "no-remap") {
                std::cerr << "bad --vinyl remap flag, expected remap or no-remap\n";
                return 1;
            }
            selection.vinyl_layers[index] = layer;
            i += 5;
        } else if (option == "--vinyl-color" && left >= 6) {
            std::uint32_t index = 0;
            std::uint32_t slot = 0;
            if (!parse_u32_arg(argv[i + 1], 3U, index) || !parse_u32_arg(argv[i + 2], 3U, slot)) {
                std::cerr << "bad --vinyl-color layer or slot, expected 0..3\n";
                return 1;
            }
            if (!selection.vinyl_layers[index]) {
                std::cerr << "--vinyl-color names a layer that was not selected\n";
                return 1;
            }
            std::uint32_t color = 0;
            if (!parse_paint_args(argv + i + 3, color)) {
                std::cerr << "bad --vinyl-color channels, expected four 0..255 values\n";
                return 1;
            }
            selection.vinyl_layers[index]->remap_colors[slot] = color;
            i += 7;
        } else if (option == "--doorline" && left >= 1) {
            nfsu::SkinDoorlineSelection doorline;
            const std::string variant = argv[i + 1];
            if (variant == "plain") {
                doorline.variant = nfsu::SkinDoorlineVariant::plain;
            } else if (variant == "kit") {
                doorline.variant = nfsu::SkinDoorlineVariant::body_kit;
            } else if (variant == "widebody") {
                doorline.variant = nfsu::SkinDoorlineVariant::wide_body;
            } else {
                std::cerr << "bad --doorline variant, expected plain, kit or widebody\n";
                return 1;
            }
            selection.doorline = doorline;
            i += 2;
        } else if (option == "--base" && left >= 4) {
            if (!parse_paint_args(argv + i + 1, selection.base_color)) {
                std::cerr << "bad --base channels, expected four 0..255 values\n";
                return 1;
            }
            i += 5;
        } else if (option == "--special" && left >= 5) {
            std::uint32_t slot = 0;
            if (!parse_u32_arg(argv[i + 1], 3U, slot)) {
                std::cerr << "bad --special slot, expected 0..3\n";
                return 1;
            }
            std::uint32_t color = 0;
            if (!parse_paint_args(argv + i + 2, color)) {
                std::cerr << "bad --special channels, expected four 0..255 values\n";
                return 1;
            }
            selection.special_colors[slot] = color;
            i += 6;
        } else {
            std::cerr << "unknown or incomplete option: " << option << '\n';
            return 1;
        }
    }

    if (loaded.empty()) {
        std::cerr << "at least one --source is required\n";
        return 1;
    }

    std::vector<nfsu::SkinInputSource> sources;
    bool sources_ok = true;
    std::cout << "car: " << selection.car_name << '\n';
    for (std::size_t s = 0; s < loaded.size(); ++s) {
        const LoadedSkinSource& entry = *loaded[s];
        std::cout << "source " << s << " (" << nfsu::skin_source_kind_name(entry.kind) << "): "
                  << entry.path << " bytes " << entry.pack.file_size << " records "
                  << entry.pack.textures.size() << " valid " << entry.pack.valid_texture_count()
                  << " issues " << entry.pack.issues.size() << '\n';
        if (!entry.pack.ok()) {
            sources_ok = false;
            print_texture_issues(entry.pack);
        }
        nfsu::SkinInputSource source;
        source.bytes = &entry.bytes;
        source.packs.push_back(&entry.pack);
        source.kind = entry.kind;
        source.path = entry.path;
        sources.push_back(source);
    }

    const nfsu::SkinInputLimits limits;
    std::string error;
    if (!assemble) {
        nfsu::BodySkinInputPlan plan;
        if (!nfsu::plan_body_skin_inputs(selection, sources, limits, plan, error)) {
            std::cerr << "plan failed: " << error << '\n';
            return 3;
        }
        print_skin_plan(plan);
        return (sources_ok && plan.skipped_inputs == 0) ? 0 : 3;
    }

    nfsu::AssembledSkinInputs assembled;
    if (!nfsu::assemble_body_skin_inputs(selection, sources, limits, assembled, error)) {
        std::cerr << "assembly failed: " << error << '\n';
        return 3;
    }
    print_skin_plan(assembled.plan());
    std::cout << "assembled: " << assembled.layer_count() << " layers, "
              << assembled.owned_bytes() << " owned bytes\n";
    for (std::size_t layer = 0; layer < assembled.layer_count(); ++layer) {
        std::cout << "  layer " << layer << " from input " << assembled.input_of_layer(layer)
                  << '\n';
    }
    if (!nfsu::validate_assembled_skin_inputs(assembled, error)) {
        std::cerr << "assembled inputs failed validation: " << error << '\n';
        return 3;
    }
    std::cout << "ownership validated\n";
    return (sources_ok && assembled.plan().skipped_inputs == 0) ? 0 : 3;
}

void print_car_type_issues(const nfsu::CarTypeTable& table) {
    for (const nfsu::CarTypeIssue& issue : table.issues) {
        std::cerr << "issue at offset " << issue.offset << ": " << issue.message
                  << '\n';
    }
}

// Prints the wheel table of one car type and the 14 placements the
// CarRenderInfo constructor derives from it. Read-only; writes nothing.
int run_car_placement(const fs::path& path, const std::string& car_type_name) {
    const nfsu::CarTypeTable table = nfsu::load_car_type_table(path);
    std::cout << "file: " << path.string() << '\n';
    std::cout << "bytes: " << table.file_size << '\n';
    std::cout << "car type table: chunk offset " << table.chunk_offset
              << " records at " << table.record_offset << " count "
              << table.records.size() << " valid " << (table.valid ? 1 : 0) << '\n';
    print_car_type_issues(table);
    if (!table.ok()) {
        std::cerr << "error: the car type table is not valid\n";
        return 3;
    }

    std::vector<std::size_t> wanted;
    if (car_type_name.empty()) {
        for (const nfsu::CarTypeRecord& record : table.records) {
            wanted.push_back(record.index);
        }
    } else {
        const std::size_t found = nfsu::find_car_type(table, car_type_name);
        if (found == nfsu::kNoCarType) {
            std::cerr << "error: no car type is named '" << car_type_name << "'\n";
            return 2;
        }
        wanted.push_back(found);
    }

    std::cout << std::fixed << std::setprecision(5);
    for (const std::size_t index : wanted) {
        const nfsu::CarTypeRecord& record = table.records[index];
        std::cout << "car type " << record.index << ' ' << record.name
                  << " class " << record.class_id << " offset " << record.offset
                  << '\n';
        std::cout << "  geometry '" << escaped_name(record.geometry_path)
                  << "' compressed '" << escaped_name(record.compressed_path)
                  << "' brand '" << escaped_name(record.brand_name) << "'\n";
        for (std::size_t w = 0; w < nfsu::kCarWheelCount; ++w) {
            const nfsu::CarWheelPlacement& wheel = record.wheels[w];
            std::cout << "  wheel " << w << " offset " << wheel.offset
                      << " pos " << wheel.longitudinal << ' ' << wheel.lateral
                      << ' ' << wheel.vertical << " rolling-radius " << wheel.rolling_radius
                      << " +0x14 " << wheel.value_14 << " +0x1C " << wheel.value_1c
                      << " +0x20 " << wheel.value_20 << '\n';
        }
        std::array<nfsu::PartPlacement, nfsu::kCullableCarPartCount> placements;
        std::string error;
        if (!nfsu::derive_cullable_part_placements(record.wheels, placements, error)) {
            std::cerr << "error: " << record.name << ": " << error << '\n';
            return 3;
        }
        for (std::size_t p = 0; p < placements.size(); ++p) {
            const nfsu::PartPlacement& placement = placements[p];
            std::cout << "  part " << std::setw(2) << std::setfill(' ') << p << ' '
                      << nfsu::cullable_car_part_name(
                             static_cast<nfsu::CullableCarPart>(p))
                      << " pos " << placement.position[0] << ' '
                      << placement.position[1] << ' ' << placement.position[2]
                      << " from " << nfsu::placement_source_name(placement.source);
            // The source indices are printed with the table they index, so a
            // reader cannot mistake the underneath entry's placement indices
            // for wheel indices.
            if (placement.source_count != 0) {
                std::cout << ' '
                          << nfsu::placement_index_space_name(placement.index_space)
                          << " [";
                for (std::size_t k = 0; k < placement.source_count; ++k) {
                    std::cout << (k == 0 ? "" : " ") << placement.sources[k];
                }
                std::cout << ']';
            }
            if (placement.degenerate_plane) {
                std::cout << " degenerate-plane";
            }
            std::cout << '\n';
        }
    }
    std::cout << "listed car types: " << wanted.size() << '\n';
    return 0;
}

// Lists the 0x0013401A attachment markers of every mesh of a geometry file
// that validated, with each marker's resolved name, role, stored matrix and
// the local placement the exhaust and plate draw paths build from it. A mesh
// the parser rejected is skipped whole and only counted. Read-only; writes
// nothing.
int run_part_markers(const fs::path& path, const std::string& role_filter) {
    const nfsu::GeometryReport report = nfsu::load_geometry_file(path);
    print_geometry_issues(report);
    std::cout << "file: " << path.string() << '\n';
    std::cout << "bytes: " << report.file_size << '\n';
    std::cout << "meshes: " << report.meshes.size() << '\n';

    std::map<std::uint32_t, std::size_t> by_hash;
    std::size_t chunks = 0;
    std::size_t markers = 0;
    std::size_t unnamed = 0;
    std::size_t skipped = 0;
    std::cout << std::fixed << std::setprecision(5);
    for (const nfsu::Mesh& mesh : report.meshes) {
        // A mesh the parser rejected is skipped whole, markers included. The
        // parser already withdraws its marker state, so this only makes the
        // rule visible here and counts what was left out; the diagnostic for
        // each such mesh is in the issue list printed above.
        if (!mesh.valid) {
            ++skipped;
            continue;
        }
        if (!mesh.has_marker_chunk) {
            continue;
        }
        ++chunks;
        bool printed_header = false;
        for (const nfsu::MeshMarker& marker : mesh.markers) {
            ++markers;
            ++by_hash[marker.name_hash];
            const char* name = nfsu::car_marker_name(marker.name_hash);
            const nfsu::CarMarkerRole role = nfsu::car_marker_role(marker.name_hash);
            if (name == nullptr) {
                ++unnamed;
            }
            if (!role_filter.empty() &&
                role_filter != nfsu::car_marker_role_name(role)) {
                continue;
            }
            if (!printed_header) {
                std::cout << "mesh " << mesh.index << " '" << escaped_name(mesh.name)
                          << "' markers " << mesh.markers.size() << " at "
                          << mesh.marker_data_offset << " (prefix "
                          << mesh.marker_prefix_bytes << ")\n";
                printed_header = true;
            }
            std::cout << "  marker " << hex32(marker.name_hash) << ' '
                      << (name == nullptr ? "<name not recovered>" : name) << ' '
                      << nfsu::car_marker_role_name(role) << " offset "
                      << marker.offset << '\n';
            std::cout << "    translation " << marker.translation(0) << ' '
                      << marker.translation(1) << ' ' << marker.translation(2)
                      << '\n';
            float placement[16];
            if (!nfsu::car_marker_placement(marker, placement)) {
                std::cout << "    placement unavailable: the matrix is not finite\n";
                continue;
            }
            for (std::size_t row = 0; row < 4; ++row) {
                std::cout << "    placement row " << row;
                for (std::size_t column = 0; column < 4; ++column) {
                    std::cout << ' ' << placement[row * 4 + column];
                }
                std::cout << '\n';
            }
        }
    }
    std::cout << "meshes skipped as invalid: " << skipped << '\n';
    std::cout << "marker chunks: " << chunks << '\n';
    std::cout << "markers: " << markers << '\n';
    std::cout << "distinct hashes: " << by_hash.size() << '\n';
    std::cout << "markers whose name is not recovered: " << unnamed << '\n';
    for (const auto& [hash, count] : by_hash) {
        const char* name = nfsu::car_marker_name(hash);
        std::cout << "  " << hex32(hash) << ' '
                  << (name == nullptr ? "<name not recovered>" : name) << ' '
                  << nfsu::car_marker_role_name(nfsu::car_marker_role(hash))
                  << ' ' << count << '\n';
    }
    return report.ok() ? 0 : 3;
}

// Prepares the stock-car profile of PreparedCarScene and reports every
// slot. No texture source is read: the profile is a mesh, instance and
// placement selection, and every material group is reported unresolved.
int run_stock_scene(const fs::path& database_path,
                    const std::string& car_type_name,
                    std::size_t lod,
                    bool front_end,
                    bool place_wheels,
                    const std::vector<fs::path>& geometry_paths) {
    const nfsu::CarPartDatabase database = nfsu::load_car_part_database(database_path);
    print_car_part_issues(database);
    const nfsu::CarTypeTable table = nfsu::load_car_type_table(database_path);
    print_car_type_issues(table);

    std::vector<nfsu::GeometryReport> reports;
    reports.reserve(geometry_paths.size());
    for (std::size_t f = 0; f < geometry_paths.size(); ++f) {
        reports.push_back(nfsu::load_geometry_file(geometry_paths[f]));
        print_geometry_issues(reports.back());
        std::cout << "geometry " << f << ": " << geometry_paths[f].string() << " meshes "
                  << reports.back().meshes.size() << " directory "
                  << (reports.back().mesh_directory_valid ? "verified" : "unavailable")
                  << '\n';
    }
    std::vector<const nfsu::GeometryReport*> sources;
    sources.reserve(reports.size());
    for (const nfsu::GeometryReport& report : reports) {
        sources.push_back(&report);
    }

    nfsu::StockSceneRequest request;
    request.car_type_name = car_type_name;
    request.lod = lod;
    request.pass = front_end ? nfsu::CarDrawPass::front_end : nfsu::CarDrawPass::world;
    request.place_wheels_from_culling_table = place_wheels;

    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport report;
    std::string error;
    if (!nfsu::prepare_stock_car_scene(sources, nfsu::RenderTextureSet{}, database,
                                       table, request, nfsu::SceneLimits{}, scene,
                                       report, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    std::cout << "car type: " << report.car_type_name << ' '
              << hex32(report.car_type_hash) << " record " << report.car_type << '\n';
    std::cout << "lod: " << static_cast<char>('A' + report.lod) << " pass: "
              << nfsu::car_draw_pass_name(report.pass) << " wheels: "
              << (place_wheels ? "placed from the culling table" : "not placed")
              << '\n';
    for (const nfsu::StockSlotReport& slot : report.slots) {
        if (slot.record == nfsu::kNoCarPart) {
            continue;
        }
        std::cout << "slot " << std::setw(3) << std::setfill(' ') << slot.slot << ' '
                  << nfsu::car_slot_name(slot.slot) << ' '
                  << nfsu::stock_part_rule_name(slot.rule) << ' '
                  << (slot.enabled ? "enabled" : "disabled") << " record "
                  << slot.record << ' ' << nfsu::stock_slot_outcome_name(slot.outcome);
        if (slot.outcome != nfsu::StockSlotOutcome::no_model &&
            slot.outcome != nfsu::StockSlotOutcome::not_installed &&
            slot.outcome != nfsu::StockSlotOutcome::skipped) {
            std::cout << " lod " << static_cast<char>('A' + slot.lod) << " requested "
                      << hex32(slot.requested_hash);
            if (slot.model == nfsu::PartModelSource::fallback) {
                std::cout << " fallback " << hex32(slot.hash);
            }
        }
        if (slot.outcome == nfsu::StockSlotOutcome::drawn ||
            slot.outcome == nfsu::StockSlotOutcome::mesh_invalid) {
            std::cout << " geometry " << slot.geometry_source << " mesh "
                      << slot.source_mesh << ' '
                      << reports[slot.geometry_source].meshes[slot.source_mesh].name;
        }
        if (slot.outcome == nfsu::StockSlotOutcome::drawn) {
            std::cout << ' ' << nfsu::stock_placement_name(slot.placement)
                      << " instances " << slot.instance_count;
        }
        std::cout << '\n';
    }
    std::size_t exhaust = 0;
    std::size_t plate = 0;
    for (const nfsu::StockMarker& marker : report.markers) {
        exhaust += marker.role == nfsu::CarMarkerRole::exhaust ? 1U : 0U;
        plate += marker.role == nfsu::CarMarkerRole::license_plate ? 1U : 0U;
    }
    std::cout << "markers: " << report.markers.size() << " exhaust " << exhaust
              << " license plate " << plate << '\n';
    std::cout << "installed slots: " << report.installed << " with a model: "
              << report.with_model << " drawn: " << report.drawn << " by fallback: "
              << report.fell_back << '\n';
    std::cout << "prepared: " << scene.meshes.size() << " meshes, "
              << scene.draws.size() << " material groups, " << report.instances
              << " instances\n";
    if (report.degenerate_wheel_plane) {
        std::cout << "wheel plane: degenerate\n";
    }
    for (const std::uint32_t hash : report.absent_hashes) {
        std::cout << "absent hash: " << hex32(hash) << '\n';
    }
    for (const std::uint32_t hash : report.ambiguous_hashes) {
        std::cout << "ambiguous hash: " << hex32(hash) << '\n';
    }
    bool geometry_ok = true;
    for (const nfsu::GeometryReport& source : reports) {
        geometry_ok = geometry_ok && source.ok();
    }
    return database.ok() && table.ok() && geometry_ok ? 0 : 3;
}

// race-events: the stream, pack and texture names of one track, as the
// loading game flow formats them.
std::string race_track_files(const nfsu::TrackInfoRecord& track) {
    return "stream " + nfsu::track_stream_file_name(track) + " pack " +
           nfsu::track_stream_pack_file_name(track) + " textures " +
           nfsu::track_texture_file_name(track.number);
}

void print_race_table_issues(const char* label,
                             const std::vector<nfsu::RaceTableIssue>& issues) {
    for (const nfsu::RaceTableIssue& issue : issues) {
        std::cerr << label << " issue at offset " << issue.offset << ": "
                  << issue.message << '\n';
    }
}

// Lists the track table of GLOBALB and the career event table of FRONTB, each
// value with the absolute image offset it was read from, then the Underground
// order: the event a new career enters, the events winning it opens, race 1
// and the first circuit from race 1. Only the file names are printed, not
// the directories, so the listing of one installation hashes the same
// wherever it is installed. Read-only; writes nothing. docs/RACE_EVENTS.md.
int run_race_events(const fs::path& globalb_path, const fs::path& frontb_path) {
    std::vector<unsigned char> globalb_image;
    const nfsu::TrackInfoTable tracks =
        nfsu::load_track_info_table(globalb_path, globalb_image);
    std::cout << "globalb: " << globalb_path.filename().string() << '\n';
    std::cout << "globalb bytes: stored " << tracks.stored_size << " image "
              << tracks.image_size << " jdlz " << (tracks.jdlz ? 1 : 0) << '\n';
    print_race_table_issues("track table", tracks.issues);
    if (tracks.ok()) {
        std::cout << "track table: chunk offset " << tracks.chunk_offset
                  << " records at " << tracks.record_offset << " count "
                  << tracks.records.size() << '\n';
        for (const nfsu::TrackInfoRecord& track : tracks.records) {
            std::cout << "track " << track.number << " @" << track.offset
                      << " number @" << track.offset + nfsu::kTrackInfoNumberOffset
                      << " region " << track.region << " @"
                      << track.offset + nfsu::kTrackInfoRegionOffset << " lap-skip "
                      << static_cast<int>(track.lap_skip) << " @"
                      << track.offset + nfsu::kTrackInfoLapSkipOffset
                      << " number-copy " << track.number_copy << " @"
                      << track.offset + nfsu::kTrackInfoNumberCopyOffset
                      << " mode-mask " << hex32(track.mode_mask) << " @"
                      << track.offset + nfsu::kTrackInfoModeMaskOffset << ' '
                      << race_track_files(track) << " bundle "
                      << nfsu::track_bundle_file_name(track.number) << '\n';
        }
    }

    std::vector<unsigned char> frontb_image;
    const nfsu::RaceEventTable events =
        nfsu::load_race_event_table(frontb_path, frontb_image);
    std::cout << "frontb: " << frontb_path.filename().string() << '\n';
    std::cout << "frontb bytes: stored " << events.stored_size << " image "
              << events.image_size << " jdlz " << (events.jdlz ? 1 : 0) << '\n';
    print_race_table_issues("event table", events.issues);
    if (!events.ok()) {
        return 3;
    }
    std::cout << "event table: container offset " << events.container_offset
              << " chunk offset " << events.chunk_offset << " records at "
              << events.record_offset << " count " << events.records.size() << '\n';

    const std::vector<nfsu::RaceTableIssue> missing =
        nfsu::check_race_tracks(events, tracks);
    print_race_table_issues("event table", missing);

    std::size_t tournaments = 0;
    std::size_t slots = 0;
    std::array<std::size_t, 7> type_counts{};
    for (const nfsu::RaceEventRecord& event : events.records) {
        tournaments += event.kind == nfsu::kRaceEventKindTournament ? 1U : 0U;
        slots += event.races.size();
        ++type_counts[std::min<std::size_t>(event.type, type_counts.size() - 1)];
        std::cout << "event " << event.number << " @" << event.offset << " kind "
                  << event.kind << " @" << event.offset + nfsu::kRaceEventKindOffset
                  << (event.kind == nfsu::kRaceEventKindTournament ? " tournament" : "")
                  << " type " << nfsu::race_type_name(event.type) << " ("
                  << event.type << ") @" << event.offset + nfsu::kRaceEventTypeOffset
                  << " races " << event.race_count << " @"
                  << event.offset + nfsu::kRaceEventRaceCountOffset
                  << " prerequisites "
                  << (event.prerequisite_mode == nfsu::kPrerequisitesAllOf ? "all-of"
                                                                           : "any-of")
                  << " @" << event.offset + nfsu::kRaceEventPrerequisiteModeOffset
                  << " count " << event.prerequisite_count << " @"
                  << event.offset + nfsu::kRaceEventPrerequisiteCountOffset << " [";
        for (std::size_t p = 0; p < event.prerequisites.size(); ++p) {
            std::cout << (p == 0 ? "" : " ") << event.prerequisites[p];
        }
        std::cout << "] @" << event.offset + nfsu::kRaceEventPrerequisiteOffset
                  << '\n';
        for (std::size_t r = 0; r < event.races.size(); ++r) {
            const nfsu::RaceSlot& race = event.races[r];
            std::cout << "  race " << r << " @" << race.offset << " track "
                      << race.track << " laps " << static_cast<int>(race.laps)
                      << " @" << race.offset + nfsu::kRaceSlotLapsOffset
                      << " direction " << nfsu::race_direction_name(race.direction)
                      << " (" << static_cast<int>(race.direction) << ") @"
                      << race.offset + nfsu::kRaceSlotDirectionOffset;
            const nfsu::TrackInfoRecord* track =
                nfsu::find_track_info(tracks, race.track);
            if (track != nullptr) {
                std::cout << " region " << track->region << ' '
                          << race_track_files(*track);
            } else {
                std::cout << " track-not-listed";
            }
            std::cout << '\n';
        }
    }
    std::cout << "events: " << events.records.size() << " tournaments "
              << tournaments << " race slots " << slots << '\n';
    std::cout << "types:";
    for (std::uint32_t t = 0; t < 6; ++t) {
        std::cout << ' ' << nfsu::race_type_name(t) << ' ' << type_counts[t];
    }
    std::cout << " unknown " << type_counts[6] << '\n';

    if (!tracks.ok() || !missing.empty()) {
        return 3;
    }

    // The Underground order is the event number: the brief screen prints it
    // with "Event %d" (0x004C5A40..0x004C5A47), and the availability test
    // decides which numbers are offered.
    std::cout << "underground order: ascending event number (0x004C5A40)\n";
    std::cout << "new career enters: event " << nfsu::kNewCareerEventNumber
              << " (0x004B383E)\n";
    std::vector<std::uint8_t> status(nfsu::kRaceEventNumberLimit, 0);
    status[nfsu::kNewCareerEventNumber] = nfsu::kEventStatusWon;
    std::vector<std::uint32_t> opened;
    for (const nfsu::RaceEventRecord& event : events.records) {
        if (nfsu::race_event_available(events, event, status)) {
            opened.push_back(event.number);
        }
    }
    std::sort(opened.begin(), opened.end());
    std::cout << "winning event " << nfsu::kNewCareerEventNumber << " opens:";
    for (const std::uint32_t number : opened) {
        std::cout << ' ' << number;
    }
    std::cout << '\n';

    // Every used slot names a listed track here: check_race_tracks found
    // none missing, and both tables are valid.
    const auto describe = [&](const nfsu::RaceEventRecord& event) {
        const nfsu::RaceSlot& race = event.races.front();
        const nfsu::TrackInfoRecord* track = nfsu::find_track_info(tracks, race.track);
        std::cout << "event " << event.number << " type "
                  << nfsu::race_type_name(event.type) << " races "
                  << event.race_count << " track " << race.track << " laps "
                  << static_cast<int>(race.laps) << " direction "
                  << nfsu::race_direction_name(race.direction) << " region "
                  << track->region << ' ' << race_track_files(*track) << '\n';
    };
    std::cout << "race 1: ";
    if (const nfsu::RaceEventRecord* race1 = nfsu::find_race_event(events, 1)) {
        describe(*race1);
    } else {
        std::cout << "none\n";
    }
    const nfsu::RaceEventRecord* first_circuit = nullptr;
    for (const nfsu::RaceEventRecord& event : events.records) {
        if (event.number >= 1 &&
            event.type == static_cast<std::uint32_t>(nfsu::RaceType::circuit) &&
            event.kind != nfsu::kRaceEventKindNeverAvailable &&
            (first_circuit == nullptr || event.number < first_circuit->number)) {
            first_circuit = &event;
        }
    }
    std::cout << "first circuit from race 1: ";
    if (first_circuit != nullptr) {
        describe(*first_circuit);
    } else {
        std::cout << "none\n";
    }

    std::map<std::string, std::size_t> circuit_streams;
    for (const nfsu::RaceEventRecord& event : events.records) {
        if (event.type != static_cast<std::uint32_t>(nfsu::RaceType::circuit)) {
            continue;
        }
        for (const nfsu::RaceSlot& race : event.races) {
            const nfsu::TrackInfoRecord* track =
                nfsu::find_track_info(tracks, race.track);
            ++circuit_streams[nfsu::track_stream_file_name(*track)];
        }
    }
    std::cout << "circuit race slots by stream:";
    for (const auto& [stream, count] : circuit_streams) {
        std::cout << ' ' << stream << ' ' << count;
    }
    std::cout << '\n';
    return 0;
}

// vehicle-sim: the vehicle model of docs/VEHICLE.md over one scripted input
// sequence on flat ground. Prints the gearbox thresholds, one telemetry line
// every `every` steps and the last step, and the SHA-256 of the telemetry
// records, which docs/VEHICLE.md pins. With a GLOBALB.BUN, the car's
// handling is read out of its car type table first and compared with the
// built-in table. Read-only; writes nothing.
struct VehicleSimArgs {
    std::string script_name;
    bool has_script_file = false;
    fs::path script_file;
    bool has_steps = false;
    std::uint64_t steps = 0;
    std::uint64_t every = 120;
    bool has_handling = false;
    fs::path handling_file;
    bool has_car = false;
    std::string car = "240SX";
};

std::string vehicle_fixed(double value, int decimals) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(decimals) << value;
    return text.str();
}

std::string vehicle_gear_name(int gear) {
    if (gear == nfsu::kVehicleGearReverse) {
        return std::string(1, 'R');
    }
    if (gear == nfsu::kVehicleGearNeutral) {
        return "N";
    }
    return std::to_string(gear - nfsu::kVehicleGearFirst + 1);
}

void print_vehicle_line(const char* label, const nfsu::VehicleTelemetry& t) {
    std::cout << label << " step " << t.step << " t "
              << vehicle_fixed(static_cast<double>(t.step) * nfsu::kVehicleStepSeconds, 3)
              << " pos " << vehicle_fixed(t.position.x, 3) << ' '
              << vehicle_fixed(t.position.y, 3) << ' ' << vehicle_fixed(t.position.z, 4)
              << " kmh " << vehicle_fixed(t.forward_speed * 3.6, 2) << " lateral "
              << vehicle_fixed(t.lateral_speed, 3) << " yaw-rate "
              << vehicle_fixed(t.angular_velocity.z, 4) << " rpm "
              << vehicle_fixed(t.engine_rpm, 1) << " gear " << vehicle_gear_name(t.gear)
              << " in " << vehicle_fixed(t.throttle, 2) << ' ' << vehicle_fixed(t.brake, 2)
              << ' ' << vehicle_fixed(t.steer, 2) << (t.handbrake ? " hb" : "")
              << " steer-angle " << vehicle_fixed(t.steer_angle, 4) << " load";
    for (const nfsu::VehicleWheelTelemetry& w : t.wheels) {
        std::cout << ' ' << vehicle_fixed(w.load, 0);
    }
    std::cout << " sliding ";
    for (const nfsu::VehicleWheelTelemetry& w : t.wheels) {
        std::cout << (w.sliding ? '1' : '0');
    }
    std::cout << '\n';
}

// Reads the handling of one car type out of a GLOBALB.BUN: the car type
// table through nfsu_core, then the record's bytes at the offset it gives.
int load_vehicle_handling(const fs::path& path, const std::string& car,
                          nfsu::VehicleHandling& out) {
    const nfsu::CarTypeTable table = nfsu::load_car_type_table(path);
    std::cout << "handling file: " << path.filename().string() << " bytes " << table.file_size
              << '\n';
    print_car_type_issues(table);
    if (!table.ok()) {
        std::cerr << "error: the car type table is not valid\n";
        return 3;
    }
    const std::size_t found = nfsu::find_car_type(table, car);
    if (found == nfsu::kNoCarType) {
        std::cerr << "error: no car type is named '" << car << "'\n";
        return 3;
    }
    const nfsu::CarTypeRecord& record = table.records[found];
    std::vector<unsigned char> bytes(nfsu::kVehicleRecordSize);
    std::ifstream stream(path, std::ios::binary);
    stream.seekg(static_cast<std::streamoff>(record.offset));
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        std::cerr << "error: failed to read the car type record at offset " << record.offset
                  << '\n';
        return 3;
    }
    std::vector<nfsu::VehicleHandlingIssue> issues;
    if (!nfsu::decode_vehicle_handling(bytes.data(), bytes.size(), out, issues)) {
        for (const nfsu::VehicleHandlingIssue& issue : issues) {
            std::cerr << "handling issue: record " << record.index << " at offset "
                      << record.offset << ' ' << issue.message << '\n';
        }
        std::cerr << "error: the handling of '" << car << "' cannot drive the model\n";
        return 3;
    }
    const std::size_t differs =
        nfsu::compare_vehicle_handling(out, nfsu::stock_240sx_handling());
    std::cout << "handling: car type " << record.index << ' ' << record.name << " record @"
              << record.offset << ' ';
    if (differs == nfsu::kVehicleRecordSize) {
        std::cout << "equal to the built-in 240SX table\n";
    } else {
        std::cout << "differs from the built-in 240SX table first at record "
                  << hex32(static_cast<std::uint32_t>(differs)) << " @"
                  << record.offset + differs << '\n';
    }
    return 0;
}

int run_vehicle_sim(const VehicleSimArgs& args) {
    nfsu::VehicleScript script;
    if (args.has_script_file) {
        std::ifstream stream(args.script_file, std::ios::binary);
        if (!stream) {
            std::cerr << "error: cannot open the script " << args.script_file.string() << '\n';
            return 2;
        }
        const nfsu::VehicleScriptLimits limits;
        std::vector<char> buffer(limits.max_bytes + 1);
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::string text(buffer.data(), static_cast<std::size_t>(stream.gcount()));
        std::string error;
        if (!nfsu::parse_vehicle_script(text, script, error, limits)) {
            std::cerr << "error: script " << args.script_file.filename().string() << ": "
                      << error << '\n';
            return 2;
        }
        script.name = args.script_file.filename().string();
    } else if (!nfsu::builtin_vehicle_script(args.script_name, script)) {
        std::cerr << "error: no built-in script is named '" << args.script_name
                  << "'; the names are";
        for (const std::string& name : nfsu::builtin_vehicle_script_names()) {
            std::cerr << ' ' << name;
        }
        std::cerr << '\n';
        return 1;
    }

    nfsu::VehicleSpec spec = nfsu::stock_240sx_vehicle_spec();
    if (args.has_handling) {
        const int loaded = load_vehicle_handling(args.handling_file, args.car, spec.handling);
        if (loaded != 0) {
            return loaded;
        }
    } else {
        std::cout << "handling: built-in 240SX table (RECOVERED, docs/VEHICLE.md)\n";
    }
    const nfsu::VehicleModel model(spec);
    if (!model.valid()) {
        std::cerr << "error: the model refuses this car: " << model.error() << '\n';
        return 3;
    }

    const std::uint64_t steps = args.has_steps ? args.steps : script.total_steps();
    std::cout << "vehicle-sim: script " << script.name << " segments "
              << script.segments.size() << " steps " << steps << " of 1/"
              << nfsu::kVehicleStepsPerSecond << " s gearbox "
              << (script.gearbox == nfsu::VehicleGearbox::manual ? "manual" : "automatic")
              << " speed " << vehicle_fixed(script.speed, 6) << " m/s\n";
    const int top =
        nfsu::kVehicleGearFirst + static_cast<int>(spec.handling.forward_gears) - 1;
    std::cout << "shift up rpm:";
    for (int g = nfsu::kVehicleGearFirst; g <= top; ++g) {
        std::cout << ' ' << vehicle_gear_name(g) << ':'
                  << vehicle_fixed(model.upshift_rpm()[static_cast<std::size_t>(g)], 2);
    }
    std::cout << "\nshift down rpm:";
    for (int g = nfsu::kVehicleGearFirst + 1; g <= top; ++g) {
        std::cout << ' ' << vehicle_gear_name(g) << ':'
                  << vehicle_fixed(model.downshift_rpm()[static_cast<std::size_t>(g)], 2);
    }
    std::cout << '\n';

    nfsu::VehiclePose pose;
    pose.speed = script.speed;
    pose.gearbox = script.gearbox;
    nfsu::VehicleState state;
    if (!model.reset(state, pose)) {
        std::cerr << "error: the model refused the starting pose\n";
        return 4;
    }
    const nfsu::VehicleFlatGround ground(0.0);
    nfsu::VehicleTelemetryHash hash;
    nfsu::VehicleTelemetry telemetry;
    for (std::uint64_t i = 0; i < steps; ++i) {
        if (!model.step(state, script.input_at(i), ground, telemetry)) {
            std::cerr << "error: the model stopped at step " << i + 1
                      << ": the state is no longer finite\n";
            return 4;
        }
        hash.add(telemetry);
        if (args.every != 0 && (i + 1) % args.every == 0 && i + 1 != steps) {
            print_vehicle_line("at", telemetry);
        }
    }
    if (steps != 0) {
        print_vehicle_line("final", telemetry);
    }
    const std::uint64_t records = hash.records();
    const std::string digest = nfsu::sha256_hex(hash.finish());
    std::cout << "telemetry: records " << records << " bytes "
              << records * nfsu::kVehicleTelemetryRecordBytes << " sha256 " << digest << '\n';
    return 0;
}

// vehicle-tuning: the per-level tuning tables of docs/VEHICLE_TUNING.md, over
// every car type of a GLOBALB.BUN. For each record it prints the values a
// reviewer can check against that document and the SHA-256 of the bytes the
// tuning encoder writes, and for the 240SX it compares the decoded tables with
// the built-in stock_240sx_tuning() field by field. Read-only; writes nothing.
int run_vehicle_tuning(const fs::path& path) {
    const nfsu::CarTypeTable table = nfsu::load_car_type_table(path);
    std::cout << "tuning file: " << path.filename().string() << " bytes "
              << table.file_size << '\n';
    print_car_type_issues(table);
    if (!table.ok()) {
        std::cerr << "error: the car type table is not valid" << '\n';
        return 3;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "error: failed to reopen the file" << '\n';
        return 3;
    }
    std::size_t decoded = 0;
    std::size_t refused = 0;
    bool compared = false;
    nfsu::Sha256 all;
    for (const nfsu::CarTypeRecord& record : table.records) {
        std::vector<unsigned char> bytes(nfsu::kVehicleRecordSize);
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(record.offset));
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
            std::cerr << "error: failed to read the record of '" << record.name
                      << "' at offset " << record.offset << '\n';
            return 3;
        }
        nfsu::VehicleTuning tuning;
        std::vector<nfsu::VehicleTuningIssue> issues;
        if (!nfsu::decode_vehicle_tuning(bytes.data(), bytes.size(), tuning, issues)) {
            ++refused;
            std::cout << "car " << record.index << ' ' << record.name << " refused" << '\n';
            for (const nfsu::VehicleTuningIssue& issue : issues) {
                std::cout << "  +0x" << std::hex << issue.offset << std::dec << ": "
                          << issue.message << '\n';
            }
            continue;
        }
        ++decoded;
        std::vector<unsigned char> image(nfsu::kVehicleRecordSize, 0);
        nfsu::encode_vehicle_tuning(tuning, image.data(), image.size());
        const std::vector<unsigned char> written(
            image.begin() + static_cast<std::ptrdiff_t>(nfsu::kVehicleTuningWeightOffset),
            image.begin() + static_cast<std::ptrdiff_t>(nfsu::kVehicleTuningEnd));
        all.update(written.data(), written.size());
        std::cout << "car " << record.index << ' ' << record.name << " @"
                  << record.offset << " mass-scale";
        for (const nfsu::VehicleTuningWeight& weight : tuning.weight) {
            std::cout << ' ' << vehicle_fixed(static_cast<double>(weight.mass_scale), 4);
        }
        std::cout << " gears";
        for (const nfsu::VehicleTuningGearbox& gearbox : tuning.gearbox) {
            std::cout << ' ' << gearbox.stored_forward_gears;
        }
        std::cout << " tyre-peak";
        for (const nfsu::VehicleTuningTyres& tyres : tuning.tyres) {
            std::cout << ' '
                      << vehicle_fixed(static_cast<double>(tyres.axles[0].static_friction), 3)
                      << '/'
                      << vehicle_fixed(static_cast<double>(tyres.axles[1].static_friction), 3);
        }
        std::cout << " spring";
        for (const nfsu::VehicleTuningSuspension& suspension : tuning.suspension) {
            std::cout << ' '
                      << vehicle_fixed(static_cast<double>(suspension.axles[0].spring), 2)
                      << '/'
                      << vehicle_fixed(static_cast<double>(suspension.axles[1].spring), 2);
        }
        std::cout << " rear-override "
                  << vehicle_fixed(
                         static_cast<double>(tuning.item.rear_drive_fraction_override), 3)
                  << " sha256 "
                  << nfsu::sha256_hex(nfsu::sha256(written.data(), written.size())) << '\n';
        if (record.name == "240SX") {
            compared = true;
            const std::size_t differs =
                nfsu::compare_vehicle_tuning(tuning, nfsu::stock_240sx_tuning());
            if (differs != nfsu::kVehicleRecordSize) {
                std::cout << "tuning: car type " << record.index << " 240SX record @"
                          << record.offset
                          << " differs from the built-in 240SX tables at +0x" << std::hex
                          << differs << std::dec << '\n';
                return 3;
            }
            std::cout << "tuning: car type " << record.index << " 240SX record @"
                      << record.offset << " equal to the built-in 240SX tables" << '\n';
        }
    }
    std::cout << "tuning: records " << table.records.size() << " decoded " << decoded
              << " refused " << refused << " bytes "
              << table.records.size() *
                     (nfsu::kVehicleTuningEnd - nfsu::kVehicleTuningWeightOffset)
              << " sha256 " << nfsu::sha256_hex(all.finish()) << '\n';
    if (refused != 0) {
        return 3;
    }
    if (!compared) {
        std::cerr << "error: the file holds no car type named 240SX" << '\n';
        return 3;
    }
    return 0;
}

// Parses everything after "vehicle-sim". Both paths are converted here,
// before anything is read, so a path argument the platform cannot decode is
// refused before any work.
int parse_vehicle_sim(int argc, char** argv, VehicleSimArgs& args) {
    const auto number = [&](int& i, const std::string& name, std::uint64_t& out) {
        if (i + 1 >= argc) {
            std::cerr << "error: " << name << " needs a value\n";
            return false;
        }
        ++i;
        if (!parse_u64(std::string(argv[i]), out) || out > 12000000U) {
            std::cerr << "error: " << name
                      << " must be a decimal or 0x integer no larger than 12000000\n";
            return false;
        }
        return true;
    };
    bool has_name = false;
    bool has_every = false;
    for (int i = 2; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--script" || argument == "--handling") {
            const bool script = argument == "--script";
            if (script ? args.has_script_file : args.has_handling) {
                std::cerr << "error: " << argument << " is given twice\n";
                return 1;
            }
            if (i + 1 >= argc) {
                std::cerr << "error: " << argument << " needs a file\n";
                return 1;
            }
            ++i;
            if (script) {
                args.has_script_file = true;
                args.script_file = nfsu::argument_path(argv[i]);
            } else {
                args.has_handling = true;
                args.handling_file = nfsu::argument_path(argv[i]);
            }
        } else if (argument == "--steps" || argument == "--every") {
            const bool steps = argument == "--steps";
            if (steps ? args.has_steps : has_every) {
                std::cerr << "error: " << argument << " is given twice\n";
                return 1;
            }
            if (!number(i, argument, steps ? args.steps : args.every)) {
                return 1;
            }
            (steps ? args.has_steps : has_every) = true;
        } else if (argument == "--car") {
            if (args.has_car) {
                std::cerr << "error: --car is given twice\n";
                return 1;
            }
            if (i + 1 >= argc) {
                std::cerr << "error: --car needs a car type name\n";
                return 1;
            }
            ++i;
            args.has_car = true;
            args.car = argv[i];
        } else if (argument.size() > 1 && argument[0] == '-') {
            std::cerr << "error: unknown option " << argument << '\n';
            return 1;
        } else if (has_name) {
            std::cerr << "error: give one script name only\n";
            return 1;
        } else {
            has_name = true;
            args.script_name = argument;
        }
    }
    if (has_name == args.has_script_file) {
        std::cerr << "error: give either a built-in script name or --script <FILE>\n";
        return 1;
    }
    if (args.has_car && !args.has_handling) {
        std::cerr << "error: --car needs --handling <GLOBALB.BUN>\n";
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// drive-replay: a recorded controller stream through the driving controls,
// the vehicle model and the chase camera of issue #59, one 1/60 s tick per
// recorded frame, and the stock 240SX seen from that camera with the CPU
// reference renderer. See docs/CONTROLS_CAMERA.md.
//
// It reads the recording, the controls file and, with --root, the six files
// of the stock frame under a game root; it writes nothing unless --out
// names a frame prefix, and then never replaces a file.
// ---------------------------------------------------------------------------

struct DriveReplayArgs {
    bool demo = false;
    bool has_input = false;
    fs::path input;
    bool has_controls = false;
    fs::path controls;
    int pov = 0;
    bool has_frames = false;
    std::uint64_t frames = 0;
    std::uint64_t every = 60;
    bool has_root = false;
    fs::path root;
    bool has_render_every = false;
    std::uint64_t render_every = 120;
    bool has_output = false;
    fs::path output;
    bool has_size = false;
    std::uint32_t width = 640;
    std::uint32_t height = 360;
};

fs::path drive_replay_frame_name(const fs::path& prefix, std::uint64_t frame) {
    std::ostringstream name;
    name << '-' << std::setw(6) << std::setfill('0') << frame << ".tga";
    fs::path out = prefix;
    out += name.str();
    return out;
}

// Reads a whole text file of at most `limit` bytes. Returns 0, or 2 with the
// reason on standard error.
int read_drive_text(const fs::path& path, const char* what, std::size_t limit,
                    std::string& text) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        std::cerr << "error: cannot open the " << what << ' ' << path.string() << '\n';
        return 2;
    }
    std::vector<char> buffer(limit + 1);
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::size_t size = static_cast<std::size_t>(stream.gcount());
    if (size > limit) {
        std::cerr << "error: the " << what << ' ' << path.filename().string()
                  << " is larger than " << limit << " bytes\n";
        return 2;
    }
    text.assign(buffer.data(), size);
    return 0;
}

void print_drive_line(const char* label, const nfsu::DriveSession& session) {
    const nfsu::VehicleTelemetry& t = session.telemetry();
    const nfsu::VehicleInput& in = session.last_input();
    const nfsu::ChaseCameraView& v = session.view();
    std::cout << label << " frame " << session.frames() << " t "
              << vehicle_fixed(static_cast<double>(session.frames()) * nfsu::kDriveFrameSeconds, 3)
              << " pos " << vehicle_fixed(t.position.x, 3) << ' '
              << vehicle_fixed(t.position.y, 3) << " kmh "
              << vehicle_fixed(t.forward_speed * 3.6, 2) << " gear " << vehicle_gear_name(t.gear)
              << " in " << vehicle_fixed(in.throttle, 2) << ' ' << vehicle_fixed(in.brake, 2)
              << ' ' << vehicle_fixed(in.steer, 3) << (in.handbrake ? " hb" : "")
              << " camera pov " << v.pov << " heading " << v.heading << " lag " << v.lag
              << " eye " << vehicle_fixed(v.eye.x, 3) << ' ' << vehicle_fixed(v.eye.y, 3) << ' '
              << vehicle_fixed(v.eye.z, 3) << '\n';
}

// The 240SX record of the GLOBALB.BUN the stock frame read, compared with
// the built-in handling and camera tables the session drives with.
void compare_drive_tables(const nfsu::StockFrame& frame, const nfsu::StockFrameInputs& inputs) {
    const std::size_t found = nfsu::find_car_type(frame.car_types, "240SX");
    const std::vector<unsigned char>& bytes =
        inputs.file(nfsu::StockFrameFile::global_bun).bytes;
    if (found == nfsu::kNoCarType) {
        std::cout << "tables: the car type table names no 240SX; the built-in tables drive\n";
        return;
    }
    const nfsu::CarTypeRecord& record = frame.car_types.records[found];
    if (record.offset > bytes.size() || bytes.size() - record.offset < nfsu::kVehicleRecordSize) {
        std::cout << "tables: the 240SX record runs past the file; the built-in tables drive\n";
        return;
    }
    const unsigned char* data = bytes.data() + record.offset;
    nfsu::VehicleHandling handling;
    std::vector<nfsu::VehicleHandlingIssue> issues;
    std::cout << "handling: 240SX record @" << record.offset << ' ';
    if (!nfsu::decode_vehicle_handling(data, nfsu::kVehicleRecordSize, handling, issues)) {
        std::cout << "cannot drive the model ("
                  << (issues.empty() ? std::string("refused") : issues.front().message) << ")";
    } else if (nfsu::compare_vehicle_handling(handling, nfsu::stock_240sx_handling()) ==
               nfsu::kVehicleRecordSize) {
        std::cout << "equal to the built-in 240SX table";
    } else {
        std::cout << "differs from the built-in 240SX table";
    }
    std::cout << "; the built-in table drives\n";
    nfsu::ChaseCameraTable table;
    std::string error;
    std::cout << "camera anchors: 240SX record +0xB50 ";
    if (!nfsu::decode_chase_camera_table(data, nfsu::kVehicleRecordSize, table, error)) {
        std::cout << "refused (" << error << ")";
    } else {
        std::vector<unsigned char> mine(nfsu::kVehicleRecordSize, 0);
        nfsu::encode_chase_camera_table(nfsu::stock_240sx_chase_camera_table(), mine.data(),
                                        mine.size());
        const bool equal =
            std::equal(mine.begin() + static_cast<std::ptrdiff_t>(nfsu::kChaseCameraBlockOffset),
                       mine.begin() + static_cast<std::ptrdiff_t>(nfsu::kChaseCameraBlockOffset +
                                                                   nfsu::kChaseCameraBlockBytes),
                       data + nfsu::kChaseCameraBlockOffset);
        std::cout << (equal ? "equal to" : "differs from") << " the built-in 240SX table";
    }
    std::cout << "; the built-in table drives\n";
}

int run_drive_replay(const DriveReplayArgs& args) {
    std::vector<nfsu::GamepadFrame> recording;
    std::string source = "the built-in demonstration";
    if (args.has_input) {
        std::string text;
        const int read = read_drive_text(args.input, "recording", nfsu::kMaxRecordingBytes, text);
        if (read != 0) {
            return read;
        }
        std::string error;
        if (!nfsu::parse_input_recording(text, recording, error)) {
            std::cerr << "error: recording " << args.input.filename().string() << ": " << error
                      << '\n';
            return 2;
        }
        source = args.input.filename().string();
    } else {
        recording = nfsu::drive_demo_recording();
    }

    nfsu::DriveSessionSetup setup = nfsu::default_drive_session_setup();
    setup.pov = args.pov;
    std::string controls_name = "the default Switch layout";
    if (args.has_controls) {
        std::string text;
        const int read =
            read_drive_text(args.controls, "controls file", nfsu::kMaxDriveConfigBytes, text);
        if (read != 0) {
            return read;
        }
        std::string error;
        if (!nfsu::parse_drive_controls_config(text, setup.controls, error)) {
            std::cerr << "error: controls " << args.controls.filename().string() << ": " << error
                      << '\n';
            return 2;
        }
        controls_name = args.controls.filename().string();
    }

    // The stock frame, when there is a root to read it from.
    std::unique_ptr<nfsu::StockFrameInputs> inputs;
    std::unique_ptr<nfsu::StockFrame> stock;
    std::vector<fs::path> sources;
    if (args.has_root) {
        inputs = std::make_unique<nfsu::StockFrameInputs>();
        nfsu::read_stock_frame_inputs(args.root, *inputs);
        bool missing = false;
        for (std::size_t i = 0; i < nfsu::kStockFrameFileCount; ++i) {
            const nfsu::StockFrameInput& input = inputs->files[i];
            sources.push_back(args.root / fs::path(nfsu::stock_frame_file_path(i)));
            if (!input.read) {
                std::cerr << "error: " << input.path << ": " << input.error << '\n';
                missing = true;
            }
        }
        if (missing) {
            return 3;
        }
    }

    const std::uint64_t total = args.has_frames && args.frames < recording.size()
                                    ? args.frames
                                    : static_cast<std::uint64_t>(recording.size());
    std::vector<std::uint64_t> render_frames;
    if (args.has_root) {
        for (std::uint64_t f = args.render_every; f <= total; f += args.render_every) {
            render_frames.push_back(f);
        }
    }
    if (args.has_output) {
        std::vector<fs::path> named = sources;
        if (args.has_input) {
            named.push_back(args.input);
        }
        if (args.has_controls) {
            named.push_back(args.controls);
        }
        for (const std::uint64_t f : render_frames) {
            const fs::path frame = drive_replay_frame_name(args.output, f);
            fs::path partial = frame;
            partial += ".partial";
            for (const fs::path& input : named) {
                if (nfsu::same_file_path(input, frame) || nfsu::same_file_path(input, partial)) {
                    std::cerr << "error: frame " << frame.filename().string()
                              << " would name an input file\n";
                    return 1;
                }
            }
            std::error_code code;
            if (fs::exists(frame, code)) {
                std::cerr << "error: frame " << frame.string()
                          << " already exists; this command never replaces a file\n";
                return 1;
            }
        }
    }

    if (args.has_root) {
        stock = std::make_unique<nfsu::StockFrame>();
        std::string error;
        const bool prepared = nfsu::prepare_stock_frame(*inputs, nfsu::stock_240sx_request(),
                                                        nfsu::stock_240sx_skin(), *stock, error);
        for (const std::string& issue : stock->issues) {
            std::cout << "issue: " << issue << '\n';
        }
        if (!prepared) {
            std::cerr << "error: the stock frame cannot be prepared: " << error << '\n';
            return 3;
        }
        std::cout << "stock car: 240SX painted, " << stock->scene.instances.size()
                  << " instances of " << stock->scene.meshes.size() << " meshes\n";
        compare_drive_tables(*stock, *inputs);
    }

    nfsu::DriveSession session;
    std::string error;
    if (!session.start(setup, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    std::cout << "drive-replay: recording " << source << " frames " << recording.size()
              << " replayed " << total << " of 1/" << nfsu::kDriveFramesPerSecond << " s, "
              << nfsu::kDriveStepsPerFrame << " vehicle steps each\n";
    std::cout << "controls: " << controls_name << ", " << setup.controls.bindings.size()
              << " bindings\n";
    std::cout << "camera: chase POV " << session.view().pov << " fov " << session.view().fov
              << " units, " << vehicle_fixed(nfsu::chase_angle_degrees(session.view().fov), 3)
              << " degrees across "
              << (setup.camera_approx.fov_mode == nfsu::ChaseFovMode::horizontal
                      ? "the frame"
                      : "a 4:3 view, its height kept")
              << '\n';

    std::vector<nfsu::PreparedInstance> base_instances;
    if (stock) {
        base_instances = stock->scene.instances;
    }
    std::size_t next_render = 0;
    std::uint64_t written = 0;
    for (std::uint64_t f = 0; f < total; ++f) {
        if (!session.advance(recording[static_cast<std::size_t>(f)], error)) {
            std::cerr << "error: " << error << '\n';
            return 4;
        }
        if (args.every != 0 && session.frames() % args.every == 0 && session.frames() != total) {
            print_drive_line("at", session);
        }
        if (next_render >= render_frames.size() || render_frames[next_render] != session.frames()) {
            continue;
        }
        ++next_render;
        nfsu::RenderOptions options;
        options.width = args.width;
        options.height = args.height;
        if (!nfsu::drive_render_camera(session.view(), setup.camera_approx.fov_mode, options.width,
                                       options.height, options.camera, error)) {
            std::cerr << "error: frame " << session.frames() << ": " << error << '\n';
            return 4;
        }
        nfsu::drive_place_instances(base_instances, session.state(), session.spec(),
                                    stock->scene.instances);
        nfsu::RenderStats stats;
        nfsu::RenderStats ground_stats;
        const nfsu::VehicleVec3 centre =
            nfsu::chase_subject(session.state(), session.spec()).origin;
        if (!nfsu::render_scene(stock->scene, options, nfsu::RenderLimits{}, stock->frame, stats,
                                error) ||
            !nfsu::drive_draw_ground(options, centre, stock->frame, ground_stats, error)) {
            std::cerr << "error: frame " << session.frames() << ": " << error << '\n';
            return 4;
        }
        nfsu::StockFrameImage image;
        if (!nfsu::encode_stock_frame(stock->frame, image, error)) {
            std::cerr << "error: frame " << session.frames() << ": " << error << '\n';
            return 4;
        }
        std::cout << "frame " << session.frames() << ": " << options.width << 'x'
                  << options.height << " car pixels " << stats.pixels_drawn
                  << " ground triangles " << ground_stats.triangles_rasterized << " sha256 "
                  << nfsu::sha256_hex(image.sha256);
        if (args.has_output) {
            const fs::path path = drive_replay_frame_name(args.output, session.frames());
            nfsu::FrameWriteResult write;
            if (!nfsu::write_frame_tga(stock->frame, path, sources, write)) {
                std::cout << '\n';
                std::cerr << "error: " << write.error << '\n';
                for (const fs::path& left : write.not_removed) {
                    std::cerr << "error: left behind " << left.string() << '\n';
                }
                return 2;
            }
            ++written;
            std::cout << " wrote " << path.filename().string();
        }
        std::cout << '\n';
    }
    if (total != 0) {
        print_drive_line("final", session);
    }
    const std::uint64_t bytes = session.hashed_bytes();
    std::cout << "stream: frames " << session.frames() << " bytes " << bytes << " sha256 "
              << nfsu::sha256_hex(session.finish()) << '\n';
    if (args.has_root) {
        std::cout << "frames: rendered " << render_frames.size() << " written " << written
                  << '\n';
    }
    return 0;
}

// Parses everything after "drive-replay". Every path is converted here,
// before anything is read, so a path argument the platform cannot decode is
// refused before any work.
int parse_drive_replay(int argc, char** argv, DriveReplayArgs& args) {
    static const char* const kFlags[] = {"--demo",   "--input", "--controls", "--pov",
                                         "--frames", "--every", "--root",     "--render-every",
                                         "--out",    "--width", "--height"};
    std::vector<std::string> seen;
    for (int i = 2; i < argc; ++i) {
        const std::string flag(argv[i]);
        if (std::find(std::begin(kFlags), std::end(kFlags), flag) == std::end(kFlags)) {
            std::cerr << "error: unknown option " << flag << '\n';
            return 1;
        }
        if (std::find(seen.begin(), seen.end(), flag) != seen.end()) {
            std::cerr << "error: " << flag << " is given twice\n";
            return 1;
        }
        seen.push_back(flag);
        if (flag == "--demo") {
            args.demo = true;
            continue;
        }
        if (i + 1 >= argc) {
            std::cerr << "error: " << flag << " needs a value\n";
            return 1;
        }
        ++i;
        const std::string value(argv[i]);
        std::uint64_t number = 0;
        if (flag == "--input") {
            args.has_input = true;
            args.input = nfsu::argument_path(argv[i]);
        } else if (flag == "--controls") {
            args.has_controls = true;
            args.controls = nfsu::argument_path(argv[i]);
        } else if (flag == "--root") {
            args.has_root = true;
            args.root = nfsu::argument_path(argv[i]);
        } else if (flag == "--out") {
            args.has_output = true;
            args.output = nfsu::argument_path(argv[i]);
        } else if (flag == "--pov") {
            if (value != "0" && value != "1" && value != "4") {
                std::cerr << "error: --pov must be a chase view: 0, 1 or 4\n";
                return 1;
            }
            args.pov = value[0] - '0';
        } else if (flag == "--frames" || flag == "--every" || flag == "--render-every") {
            const bool zero_allowed = flag == "--every";
            if (!parse_u64(value, number) || number > 10000000U ||
                (!zero_allowed && number == 0)) {
                std::cerr << "error: " << flag << " must be a whole number from "
                          << (zero_allowed ? "0" : "1") << " to 10000000\n";
                return 1;
            }
            if (flag == "--frames") {
                args.has_frames = true;
                args.frames = number;
            } else if (flag == "--every") {
                args.every = number;
            } else {
                args.has_render_every = true;
                args.render_every = number;
            }
        } else {
            if (!parse_u64(value, number) || number == 0 || number > 8192) {
                std::cerr << "error: " << flag << " needs a size from 1 to 8192\n";
                return 1;
            }
            args.has_size = true;
            (flag == "--width" ? args.width : args.height) = static_cast<std::uint32_t>(number);
        }
    }
    if (args.demo == args.has_input) {
        std::cerr << "error: give either --demo or --input <FILE>\n";
        return 1;
    }
    if (!args.has_root && (args.has_render_every || args.has_output || args.has_size)) {
        std::cerr << "error: --render-every, --out, --width and --height describe the frames,"
                     " which need --root\n";
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// track-whole: every section of one track's STREAM file that speed.exe loads
// for a race on that track, loaded together and placed as one scene. See
// docs/TRACK_WHOLE.md.
//
// The track's TRACKB####.LZC carries the streaming table (0x00034107) and
// the two lists the loader reads beside it (0x00034154 and 0x00034153 of
// the zone container); the command applies the recovered rule to pick the
// sections, reads exactly their byte ranges and nothing else, and prints
// per-section counts, totals and the budgets of issue #52. With --out it
// also draws the whole track with the CPU reference renderer, exactly as
// track-render draws one region, and never replaces a file.
// ---------------------------------------------------------------------------

constexpr std::uint64_t kTrackWholeTextureBytes = 1ULL << 30;

struct TrackWholeArgs {
    nfsu::TrackStreamSet set = nfsu::TrackStreamSet::permanent;
    bool has_pack = false;
    fs::path pack;
    std::vector<fs::path> textures;
    std::size_t lod_slot = 0;
    // Sections to list; every section when not given.
    bool has_list = false;
    std::size_t list_limit = 0;
    bool has_output = false;
    fs::path output;
    nfsu::RenderOptions render;
    nfsu::TrackFramingOptions framing;
    bool has_distance = false;
    double distance = 0.0;
    bool has_target = false;
    nfsu::Vec3 target;
    bool has_near = false;
    double near_plane = 0.0;
    bool has_far = false;
    double far_plane = 0.0;
};

void print_track_whole_issues(const char* label,
                              const std::vector<nfsu::TrackIssue>& issues) {
    for (const nfsu::TrackIssue& issue : issues) {
        std::cerr << label << " issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
}

// The entries a section list's count names; the slots past it are never
// read by the loader and are not printed.
std::string track_whole_list(const nfsu::TrackZoneSectionList& list) {
    std::string text;
    for (std::size_t k = 0;
         list.count > 0 && k < static_cast<std::size_t>(list.count) &&
         k < list.sections.size();
         ++k) {
        text += ' ';
        text += std::to_string(list.sections[k]);
    }
    return text;
}

int run_track_whole(const fs::path& stream_path,
                    const fs::path& trackb_path,
                    std::uint32_t track,
                    const TrackWholeArgs& args) {
    std::vector<fs::path> inputs;
    inputs.push_back(stream_path);
    inputs.push_back(trackb_path);
    if (args.has_pack) {
        inputs.push_back(args.pack);
    }
    inputs.insert(inputs.end(), args.textures.begin(), args.textures.end());
    if (args.has_output) {
        // Before a byte is read. write_frame_tga repeats both refusals and
        // publishes atomically; this is the early diagnostic.
        fs::path partial = args.output;
        partial += ".partial";
        for (const fs::path& source : inputs) {
            if (nfsu::same_file_path(source, args.output) ||
                nfsu::same_file_path(source, partial)) {
                std::cerr << "error: --out must differ from every input file\n";
                return 1;
            }
        }
        std::error_code code;
        if (fs::exists(args.output, code)) {
            std::cerr << "error: --out " << args.output.string()
                      << " already exists; this command never replaces a file\n";
            return 1;
        }
    }

    std::vector<unsigned char> image;
    const nfsu::TrackZoneReport zones = nfsu::load_track_zones(trackb_path, image);
    std::cout << "stream: " << stream_path.filename().string() << '\n';
    std::cout << "trackb: " << trackb_path.filename().string() << " packed "
              << zones.cost.packed_bytes << " image " << image.size() << '\n';
    std::cout << "zones: valid " << (zones.valid ? "yes" : "no")
              << " polygons " << zones.polygons.size() << " zones "
              << zones.zones.size() << " section lists "
              << zones.section_lists.size() << '\n';
    print_track_whole_issues("zone", zones.issues);

    const nfsu::TrackStreamTable table =
        nfsu::parse_track_stream_table(image.data(), image.size());
    print_track_whole_issues("table", table.issues);
    if (!table.valid || !zones.valid) {
        std::cout << "table: " << (table.valid ? "valid" : "not valid")
                  << "; no section is selected\n";
        return 3;
    }
    std::uint64_t by_file[nfsu::kTrackStreamFileCount] = {0, 0, 0};
    std::uint64_t flagged = 0;
    for (const nfsu::TrackStreamRecord& record : table.records) {
        ++by_file[record.file_index];
        flagged += record.track_flag != 0 ? 1U : 0U;
    }
    std::cout << "table: offset " << table.table_offset << " records "
              << table.records.size() << " stream " << by_file[1] << " pack "
              << by_file[2] << " other " << by_file[0] << " track-flagged "
              << flagged << '\n';
    if (table.has_file_sizes) {
        std::cout << "table: file sizes " << table.file_sizes[0] << ' '
                  << table.file_sizes[1] << ' ' << table.file_sizes[2]
                  << " at " << table.file_sizes_offset << '\n';
    } else {
        std::cout << "table: file sizes none\n";
    }
    std::cout << "table: streaming pool " << nfsu::track_stream_pool_bytes(table)
              << " (0x00444AF0 plus " << nfsu::kTrackStreamPoolHeadroom
              << ")\n";

    nfsu::TrackStreamSelection selection;
    std::string error;
    if (!nfsu::select_track_stream_sections(table, zones, track, args.set,
                                            selection, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::cout << "track: " << track;
    if (selection.has_extra_list) {
        const nfsu::TrackZoneSectionList& list =
            zones.section_lists[selection.extra_list];
        std::cout << " extra list record " << list.index << ":"
                  << track_whole_list(list) << '\n';
    } else {
        std::cout << " extra list none\n";
    }
    std::cout << "set: " << nfsu::track_stream_set_name(selection.set)
              << " requests " << selection.requests.size() << " selected "
              << selection.records.size() << " without record "
              << selection.requests_without_record << " repeated "
              << selection.repeated_requests << " excluded "
              << selection.excluded_records << " stopped at limit "
              << (selection.stopped_at_limit ? "yes" : "no") << '\n';
    if (selection.has_route_list) {
        std::cout << "route: zones " << selection.route_zones << " missing "
                  << selection.route_zones_missing << " section numbers "
                  << selection.route_numbers << " with a record "
                  << selection.route_numbers_with_record
                  << " outside the set " << selection.route_numbers_outside
                  << '\n';
    } else {
        std::cout << "route: no 0x00034153 record is keyed on " << track
                  << '\n';
    }

    nfsu::TrackStreamFiles files;
    files.stream = stream_path;
    files.has_pack = args.has_pack;
    files.pack = args.pack;
    nfsu::TrackStreamLoadLimits load_limits;
    nfsu::TrackStreamLoad load;
    if (!nfsu::load_track_stream_sections(table, selection, files, load_limits,
                                          load, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    print_track_whole_issues("stream", load.issues);
    std::cout << "files: stream size " << load.stream_file_size << " pack "
              << (args.has_pack ? std::to_string(load.pack_file_size)
                                : std::string("none"))
              << " read " << load.bytes_read << '\n';
    for (const nfsu::TrackStreamSection& section : load.sections) {
        print_track_whole_issues("section", section.issues);
        print_track_whole_issues("geometry", section.geometry.issues);
        print_track_whole_issues("object", section.objects.issues);
        for (const nfsu::TextureIssue& issue : section.packs.issues) {
            std::cerr << "section " << section.name << " pack issue at "
                      << (section.offset + issue.offset) << ": "
                      << issue.message << '\n';
        }
        for (const nfsu::TexturePack& pack : section.packs.packs) {
            for (const nfsu::TextureIssue& issue : pack.issues) {
                std::cerr << "section " << section.name << " pack issue at "
                          << (section.offset + issue.offset) << ": "
                          << issue.message << '\n';
            }
        }
    }

    // Texture sources: the packs of the loaded sections, in selection order,
    // then the files named with --texture, in the order given. Earlier
    // sources win a shared hash, as they do for every track command.
    const std::vector<std::uint32_t> wanted =
        nfsu::track_region_texture_hashes(load.geometry);
    std::vector<nfsu::RenderTextureSource> sources;
    std::vector<std::size_t> owners;
    nfsu::track_stream_texture_sources(load, &wanted, sources, owners);
    const std::size_t section_sources = sources.size();
    std::vector<std::size_t> source_owner = owners;
    std::vector<std::vector<unsigned char>> file_bytes(args.textures.size());
    std::vector<nfsu::TexturePackSet> file_sets(args.textures.size());
    for (std::size_t i = 0; i < args.textures.size(); ++i) {
        nfsu::TextureLimits limits;
        limits.max_file_size =
            std::min<std::uint64_t>(limits.max_file_size,
                                    kMaxTrackTextureInputBytes);
        file_sets[i] =
            nfsu::load_texture_pack_set(args.textures[i], file_bytes[i], limits);
        std::cout << "texture source " << (section_sources + i) << ": "
                  << args.textures[i].filename().string() << " size "
                  << file_sets[i].file_size << " packs "
                  << file_sets[i].packs.size() << " complete "
                  << (file_sets[i].complete ? 1 : 0) << '\n';
        for (const nfsu::TextureIssue& issue : file_sets[i].issues) {
            std::cerr << "texture source " << (section_sources + i)
                      << " issue at " << issue.offset << ": " << issue.message
                      << '\n';
        }
    }
    for (std::size_t i = 0; i < args.textures.size(); ++i) {
        nfsu::RenderTextureSource source;
        source.bytes = &file_bytes[i];
        for (const nfsu::TexturePack& pack : file_sets[i].packs) {
            source.packs.push_back(&pack);
        }
        source.complete = file_sets[i].complete;
        source.wanted = &wanted;
        sources.push_back(std::move(source));
        source_owner.push_back(nfsu::kTrackStreamNoRecord);
    }
    nfsu::RenderTextureLimits texture_limits;
    texture_limits.max_total_bytes = kTrackWholeTextureBytes;
    nfsu::RenderTextureSet textures;
    if (!nfsu::decode_render_texture_sources(sources, texture_limits, textures,
                                             error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::cout << "textures: sources " << sources.size() << " from sections "
              << section_sources << " wanted " << wanted.size()
              << " decoded " << textures.textures.size() << " bytes "
              << textures.decoded_bytes << " shadowed "
              << textures.shadowed.size() << " unusable claims "
              << textures.unusable_claims.size() << " blocked "
              << textures.blocked_sources.size() << " not wanted "
              << textures.not_wanted << '\n';

    nfsu::TrackSceneOptions scene_options;
    scene_options.lod_slot = args.lod_slot;
    scene_options.region.max_texture_bytes = kTrackWholeTextureBytes;
    nfsu::PreparedTrackScene scene;
    if (!nfsu::build_track_scene(load.geometry, load.objects, textures,
                                 scene_options, scene, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::vector<nfsu::TrackStreamSectionCost> costs;
    if (!nfsu::track_stream_section_costs(load, scene, source_owner, costs,
                                          error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    // What each --texture file supplied to the scene; the sections' share
    // is on their own lines below.
    for (std::size_t i = 0; i < args.textures.size(); ++i) {
        const std::size_t source = section_sources + i;
        std::uint64_t retained = 0;
        std::uint64_t retained_bytes = 0;
        for (const nfsu::RenderTexture& texture : scene.region.textures) {
            if (texture.source == source) {
                ++retained;
                retained_bytes += texture.rgba.size();
            }
        }
        std::cout << "texture source " << source << ": retained " << retained
                  << " bytes " << retained_bytes << '\n';
    }

    const std::size_t listed =
        args.has_list ? std::min(args.list_limit, load.sections.size())
                      : load.sections.size();
    std::cout << "sections: listed " << listed << " of " << load.sections.size()
              << '\n';
    for (std::size_t s = 0; s < listed; ++s) {
        const nfsu::TrackStreamSection& section = load.sections[s];
        const nfsu::TrackStreamSectionCost& cost = costs[s];
        std::cout << "section " << s << " record " << section.record << " name "
                  << section.name << " number " << section.number << " reason "
                  << nfsu::track_stream_reason_name(section.reason) << " file "
                  << section.file_index << " offset " << section.offset
                  << " size " << section.size << " resident "
                  << section.resident_size << " loaded "
                  << (section.loaded ? "yes" : "no") << '\n';
        std::cout << "  chunks " << section.chunks << " roots "
                  << section.geometry_roots << " scenery "
                  << section.scenery_sections << " packs "
                  << section.texture_packs << " padding "
                  << section.padding_chunks << " other " << section.other_chunks
                  << '\n';
        std::cout << "  meshes " << cost.meshes << " valid " << cost.valid_meshes
                  << " empty " << cost.empty_meshes << " refused "
                  << cost.refused_meshes << " vertices " << cost.vertices
                  << " triangles " << cost.triangles << " scenery sections "
                  << cost.scenery_sections << " instances " << cost.instances
                  << " placed " << cost.placed_instances << " pack records "
                  << cost.pack_records << " textures " << cost.retained_textures
                  << " texture bytes " << cost.retained_texture_bytes << '\n';
    }

    std::uint64_t loaded = 0;
    std::uint64_t resident_declared = 0;
    std::uint64_t refused = 0;
    std::uint64_t pack_records = 0;
    for (std::size_t s = 0; s < load.sections.size(); ++s) {
        loaded += load.sections[s].loaded ? 1U : 0U;
        resident_declared += load.sections[s].resident_size;
        refused += costs[s].refused_meshes;
        pack_records += costs[s].pack_records;
    }
    const nfsu::TrackRegionCost& parsed = load.geometry.cost;
    std::cout << "totals: sections " << load.sections.size() << " loaded "
              << loaded << " bytes read " << load.bytes_read
              << " resident declared " << resident_declared << '\n';
    std::cout << "totals: meshes " << parsed.meshes << " valid "
              << parsed.valid_meshes << " empty " << parsed.empty_meshes
              << " refused " << refused << " vertices " << parsed.vertices
              << " triangles " << parsed.triangles << " materials "
              << parsed.materials << '\n';
    const nfsu::TrackScenePlacementCost& placement = scene.cost;
    std::cout << "totals: scenery sections " << load.objects.cost.sections
              << " valid " << load.objects.cost.valid_sections << " objects "
              << load.objects.cost.objects << " instances "
              << placement.source_instances << " placed "
              << placement.placed_instances << " unplaced empty slot "
              << placement.unplaced_empty_slot << " skipped hash "
              << placement.unplaced_skipped_hash << " unresolved "
              << placement.unplaced_unresolved << " distinct "
              << placement.unplaced_hashes << '\n';
    const nfsu::TrackDrawCost& draws = scene.region.cost;
    std::cout << "totals: pack records " << pack_records << " textures retained "
              << draws.textures << " texture bytes " << draws.texture_bytes
              << " unresolved hashes " << draws.unresolved_hashes << '\n';
    std::cout << "totals: draws " << draws.draws << " drawable "
              << draws.drawable_draws << " resolved " << draws.resolved_draws
              << " unresolved " << draws.unresolved_draws << " unusable "
              << draws.unusable_draws << '\n';
    std::cout << "totals: meshes drawn " << placement.drawn_meshes
              << " undrawn " << placement.undrawn_meshes
              << " resident entries " << placement.resident_entries
              << " resident triangles " << placement.resident_triangles
              << '\n';
    nfsu::TrackBudgetCost prepared;
    nfsu::track_budget_prepare_cost(scene, prepared);
    std::cout << "totals: vertex buffer bytes " << draws.vertex_buffer_bytes
              << " index buffer bytes " << draws.index_buffer_bytes
              << " texture bytes " << draws.texture_bytes << " instance bytes "
              << prepared.prepare_instance_bytes << " resident bytes "
              << prepared.prepare_resident_bytes << '\n';

    // Every placed instance, in material-group order: the draw list of a
    // frame that sees the whole track, which is also what --out draws.
    std::vector<nfsu::TrackVisibleInstance> everything;
    for (const nfsu::TrackSceneSection& section : scene.sections) {
        for (std::size_t k = 0; k < section.source_instances; ++k) {
            nfsu::TrackVisibleInstance item;
            item.section_index = section.section_index;
            item.instance_index = k;
            everything.push_back(item);
        }
    }
    nfsu::TrackDrawListOptions list_options;
    list_options.order = nfsu::TrackDrawOrder::material_group;
    nfsu::TrackDrawList list;
    if (!nfsu::build_track_draw_list(scene, everything, list_options, list,
                                     error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::cout << "draw list: order material group entries "
              << list.cost.entries << " triangles " << list.cost.triangles
              << " groups " << list.cost.groups << " meshes "
              << list.cost.meshes << " texture binds "
              << list.cost.texture_binds << '\n';

    // The hashes nothing loaded resolves, each with the number of entries of
    // the draw list above that need it, so a hash no placed instance draws
    // reads as 0. Each listing prints its count whether or not it is empty.
    std::vector<std::uint64_t> unplaced_instances(scene.unplaced_hashes.size(), 0);
    for (const nfsu::TrackUnplacedInstance& unplaced : scene.unplaced) {
        const auto found = std::lower_bound(scene.unplaced_hashes.begin(),
                                            scene.unplaced_hashes.end(),
                                            unplaced.mesh_hash);
        if (found != scene.unplaced_hashes.end() && *found == unplaced.mesh_hash) {
            ++unplaced_instances[static_cast<std::size_t>(
                found - scene.unplaced_hashes.begin())];
        }
    }
    const std::size_t unplaced_listed =
        std::min(scene.unplaced_hashes.size(), kTrackUnresolvedListLimit);
    std::cout << "unplaced: listed " << unplaced_listed << " of "
              << scene.unplaced_hashes.size() << " mesh hashes\n";
    for (std::size_t k = 0; k < unplaced_listed; ++k) {
        std::cout << "  unplaced " << hex32(scene.unplaced_hashes[k])
                  << " instances " << unplaced_instances[k] << '\n';
    }
    const std::vector<std::uint32_t>& missing = scene.region.unresolved_hashes;
    std::vector<std::uint64_t> missing_entries(missing.size(), 0);
    for (const nfsu::TrackDrawListEntry& entry : list.entries) {
        const nfsu::TrackDraw& draw = scene.region.draws[entry.draw];
        if (draw.binding != nfsu::BindingKind::unresolved) {
            continue;
        }
        const auto found =
            std::lower_bound(missing.begin(), missing.end(), draw.texture_hash);
        if (found != missing.end() && *found == draw.texture_hash) {
            ++missing_entries[static_cast<std::size_t>(found - missing.begin())];
        }
    }
    const std::size_t unresolved_listed =
        std::min(missing.size(), kTrackUnresolvedListLimit);
    std::cout << "unresolved: listed " << unresolved_listed << " of "
              << missing.size() << " texture hashes\n";
    for (std::size_t k = 0; k < unresolved_listed; ++k) {
        std::cout << "  unresolved " << hex32(missing[k]) << " entries "
                  << missing_entries[k] << '\n';
    }

    nfsu::UploadTable upload;
    if (!nfsu::build_track_upload_table(scene, upload, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    nfsu::SceneMemoryRequirement gpu;
    if (!nfsu::upload_table_requirement(upload, nfsu::linear_image_layout, gpu,
                                        error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::cout << "gpu: buffers " << gpu.buffer_bytes << " extent "
              << gpu.buffer_extent << " images " << gpu.image_bytes
              << " extent " << gpu.image_extent << " staging largest "
              << gpu.staging_largest_bytes << " (linear layout, a lower bound)\n";

    std::vector<nfsu::TrackWholeBudgetRow> rows;
    nfsu::track_whole_budget_rows(scene, textures.decoded_bytes,
                                  prepared.prepare_resident_bytes, gpu, rows);
    std::size_t over = 0;
    for (const nfsu::TrackWholeBudgetRow& row : rows) {
        over += row.within ? 0U : 1U;
        std::cout << "budget: " << nfsu::track_whole_platform_name(row.platform)
                  << ' ' << row.name << ' ' << row.value << " of "
                  << row.ceiling;
        if (row.within) {
            std::cout << " within\n";
        } else {
            std::cout << " OVER by " << (row.value - row.ceiling) << '\n';
        }
    }
    std::cout << "budget: rows " << rows.size() << " over " << over << '\n';

    nfsu::TrackDrawListBounds bounds;
    if (!nfsu::track_draw_list_bounds(scene, list, bounds, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    if (bounds.drawn.valid) {
        std::cout << "world: drawn x " << format_double(bounds.drawn.min[0])
                  << " .. " << format_double(bounds.drawn.max[0]) << " y "
                  << format_double(bounds.drawn.min[1]) << " .. "
                  << format_double(bounds.drawn.max[1]) << " z "
                  << format_double(bounds.drawn.min[2]) << " .. "
                  << format_double(bounds.drawn.max[2]) << " points "
                  << bounds.drawn.points << '\n';
    } else {
        std::cout << "world: drawn none\n";
    }

    const bool reports_ok = zones.ok() && table.ok() && load.ok();
    if (!args.has_output) {
        return reports_ok ? 0 : 3;
    }

    nfsu::RenderOptions options = args.render;
    bool framed = false;
    if (nfsu::frame_track_bounds(bounds.drawn, options.width, options.height,
                                 args.framing, options.camera, error)) {
        framed = true;
    } else if (!args.has_distance || !args.has_target) {
        std::cerr << "error: the track could not be framed automatically ("
                  << error
                  << "); give --view-distance and --view-target explicitly\n";
        return 2;
    } else {
        std::cout << "render: framing failed (" << error
                  << "); drawing from the explicit distance and target\n";
        options.camera = nfsu::OrbitCamera{};
        options.camera.yaw_deg = args.framing.yaw_deg;
        options.camera.pitch_deg = args.framing.pitch_deg;
        options.camera.fov_y_deg = args.framing.fov_y_deg;
    }
    if (args.has_distance) {
        options.camera.distance = args.distance;
    }
    if (args.has_target) {
        options.camera.target = args.target;
    }
    if (args.has_near) {
        options.camera.near_plane = args.near_plane;
    }
    if (args.has_far) {
        options.camera.far_plane = args.far_plane;
    }
    if (!nfsu::validate_camera(options.camera, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track_stats;
    const nfsu::RenderLimits render_limits;
    if (!nfsu::render_track_draw_list(scene, list, options, render_limits,
                                      frame, stats, track_stats, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    nfsu::FrameWriteResult write;
    if (!nfsu::write_frame_tga(frame, args.output, inputs, write)) {
        std::cerr << "error: " << write.error << '\n';
        for (const fs::path& left : write.not_removed) {
            std::cerr << "error: left behind " << left.string() << '\n';
        }
        return 2;
    }
    const nfsu::Vec3 eye = nfsu::orbit_eye(options.camera);
    std::cout << "render: size " << frame.width() << 'x' << frame.height()
              << " camera " << (framed ? "framed" : "explicit") << '\n';
    std::cout << "render: camera yaw " << format_double(options.camera.yaw_deg)
              << " pitch " << format_double(options.camera.pitch_deg)
              << " distance " << format_double(options.camera.distance)
              << " target " << format_double(options.camera.target.x) << ','
              << format_double(options.camera.target.y) << ','
              << format_double(options.camera.target.z) << " fov_y "
              << format_double(options.camera.fov_y_deg) << " near "
              << format_double(options.camera.near_plane) << " far "
              << format_double(options.camera.far_plane) << '\n';
    std::cout << "render: eye " << format_double(eye.x) << ','
              << format_double(eye.y) << ',' << format_double(eye.z) << '\n';
    std::cout << "render: entries " << track_stats.entries << " drawn "
              << track_stats.entries_drawn << " resolved "
              << track_stats.entries_resolved << " unresolved "
              << track_stats.entries_unresolved << " unusable "
              << track_stats.entries_unusable << " binds "
              << track_stats.texture_binds << '\n';
    std::cout << "render: triangles submitted " << stats.triangles_submitted
              << " outside " << stats.triangles_outside << " clipped "
              << stats.triangles_clipped << " rasterized "
              << stats.triangles_rasterized << '\n';
    std::cout << "render: pixels drawn " << stats.pixels_drawn << " fallback "
              << stats.pixels_fallback << '\n';
    std::cout << "wrote: " << args.output.string() << '\n';
    return reports_ok ? 0 : 3;
}

// race-route: the route of one career event over its TRACKB bundle, and
// optionally a replay of positions through it. docs/RACE_ROUTE.md.

// The largest path file race-route reads, and the most positions in it.
constexpr std::uint64_t kRacePathFileLimit = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kRacePathPositionLimit = 2000000;
// One replay step: this project's choice, not a retail constant. It
// matters to the wrong-way test's speed gate, and to race-timing, which
// takes a replayed car's speed - and with it a crossing's backdate - as the
// step's length over it.
constexpr float kRaceStepSeconds = 1.0F / 60.0F;
// The spacing of a built-in drive, 0.5 per step, which at 60 steps a second
// is 30 per second: fast enough for the wrong-way speed gate of 10 mph.
constexpr float kRaceDriveSpacing = 0.5F;

const char* race_zone_type_name(std::uint32_t type) {
    // The executable's own names for the eleven types, 0x00700964..
    // 0x0070098C in order.
    static constexpr const char* kNames[] = {
        "NO_WRONG_WAY", "WRONG_WAY", "RESET", "RESET_TO_POINT", "TRAFFIC_DENSITY",
        "TUNNEL", "DRIFT_GATE", "DRIFT_HOT_SPOT", "TUTORIAL", "SHORTCUT",
        "TRACK_PATH_ZONE_STREAMER_PREDICTION"};
    return type < 11 ? kNames[type] : "unknown";
}

void print_race_route_issues(const std::vector<nfsu::RaceRouteIssue>& issues) {
    for (const nfsu::RaceRouteIssue& issue : issues) {
        std::cerr << "trackb issue at offset " << issue.offset << ": " << issue.message
                  << '\n';
    }
}

// A path file: one position per line, two decimal numbers - the horizontal
// raw slots slot0 and slot1 the markers and nodes use - separated by spaces,
// tabs or carriage returns. Blank lines and lines whose first non-blank
// character is '#' are skipped. Every other line must hold exactly two
// finite numbers and nothing else, and there must be at least one;
// anything else is refused with the line number, and nothing is replayed.
bool read_race_path_file(const fs::path& path, std::vector<float>& points,
                         std::string& error) {
    points.clear();
    std::error_code code;
    const std::uintmax_t size = fs::file_size(path, code);
    if (code) {
        error = "cannot size the path file: " + code.message();
        return false;
    }
    if (static_cast<std::uint64_t>(size) > kRacePathFileLimit) {
        error = "the path file is " + std::to_string(size) + " bytes, over the limit of " +
                std::to_string(kRacePathFileLimit);
        return false;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream || (size != 0 && !stream.read(text.data(),
                                                  static_cast<std::streamsize>(size)))) {
            error = "cannot read the path file";
            return false;
        }
    }
    std::size_t line_number = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string_view line(text.data() + at, end - at);
        at = end + 1;
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto blank = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; };
        std::size_t p = 0;
        while (p < line.size() && blank(line[p])) {
            ++p;
        }
        if (p == line.size() || line[p] == '#') {
            continue;
        }
        const std::string where = "path file line " + std::to_string(line_number) + ": ";
        float values[2] = {0.0F, 0.0F};
        for (int k = 0; k < 2; ++k) {
            while (p < line.size() && blank(line[p])) {
                ++p;
            }
            const char* first = line.data() + p;
            const char* last = line.data() + line.size();
            const char* stop = first;
            while (stop < last && !blank(*stop)) {
                ++stop;
            }
            if (first == stop) {
                error = where + "expected two numbers, found " + std::to_string(k);
                return false;
            }
            const auto result = std::from_chars(first, stop, values[k]);
            if (result.ec != std::errc() || result.ptr != stop || !std::isfinite(values[k])) {
                error = where + "'" + std::string(first, stop) + "' is not a finite number";
                return false;
            }
            p = static_cast<std::size_t>(stop - line.data());
        }
        while (p < line.size() && blank(line[p])) {
            ++p;
        }
        if (p != line.size()) {
            error = where + "expected two numbers and nothing after them";
            return false;
        }
        if (points.size() / 2 >= kRacePathPositionLimit) {
            error = where + "more than " + std::to_string(kRacePathPositionLimit) + " positions";
            return false;
        }
        points.push_back(values[0]);
        points.push_back(values[1]);
    }
    if (points.empty()) {
        error = "the path file holds no position";
        return false;
    }
    return true;
}

struct RaceRouteArgs {
    std::uint32_t event = 0;
    std::optional<fs::path> path;
    std::string drive;
    int skip_from = -1;
    int skip_to = -1;
};

// Lists the route of one event - the race's laps and direction, the path,
// its nodes, the markers, the zones and the boundaries with the percentage
// each was measured at - and, given a path file or a built-in drive,
// replays it one fixed step per position and prints every crossing, lap,
// finish and wrong-way change. Read-only; writes nothing. Prints only file
// names, not directories. docs/RACE_ROUTE.md.
int run_race_route(const fs::path& globalb_path, const fs::path& frontb_path,
                   const fs::path& trackb_path, const RaceRouteArgs& args) {
    std::vector<unsigned char> globalb_image;
    const nfsu::TrackInfoTable tracks =
        nfsu::load_track_info_table(globalb_path, globalb_image);
    std::cout << "globalb: " << globalb_path.filename().string() << '\n';
    print_race_table_issues("track table", tracks.issues);
    std::vector<unsigned char> frontb_image;
    const nfsu::RaceEventTable events =
        nfsu::load_race_event_table(frontb_path, frontb_image);
    std::cout << "frontb: " << frontb_path.filename().string() << '\n';
    print_race_table_issues("event table", events.issues);
    std::vector<unsigned char> trackb_image;
    const nfsu::RaceRouteTrack track = nfsu::load_race_route_track(trackb_path, trackb_image);
    std::cout << "trackb: " << trackb_path.filename().string() << '\n';
    std::cout << "trackb bytes: stored " << track.stored_size << " image " << track.image_size
              << " jdlz " << (track.jdlz ? 1 : 0) << '\n';
    print_race_route_issues(track.issues);
    if (!tracks.ok() || !events.ok() || !track.ok()) {
        return 3;
    }

    const nfsu::RaceEventRecord* event = nfsu::find_race_event(events, args.event);
    if (event == nullptr) {
        std::cerr << "error: the event table holds no event " << args.event << '\n';
        return 3;
    }
    const nfsu::RaceSlot& race = event->races.front();
    const nfsu::TrackInfoRecord* info = nfsu::find_track_info(tracks, race.track);
    std::cout << "event " << event->number << " @" << event->offset << " type "
              << nfsu::race_type_name(event->type) << " (" << event->type << ") race 0 of "
              << event->races.size() << " @" << race.offset << " track " << race.track
              << " laps " << static_cast<int>(race.laps) << " @"
              << race.offset + nfsu::kRaceSlotLapsOffset << " direction "
              << nfsu::race_direction_name(race.direction) << " ("
              << static_cast<int>(race.direction) << ") @"
              << race.offset + nfsu::kRaceSlotDirectionOffset << '\n';
    if (info == nullptr) {
        std::cerr << "error: event " << event->number << " names track " << race.track
                  << ", which is not in the track table\n";
        return 3;
    }
    const std::string bundle = nfsu::track_bundle_file_name(info->number);
    std::cout << "track " << info->number << " @" << info->offset << " lap-skip "
              << static_cast<int>(info->lap_skip) << " @"
              << info->offset + nfsu::kTrackInfoLapSkipOffset << " bundle " << bundle << '\n';
    if (lower_ascii(trackb_path.filename().string()) != lower_ascii(bundle)) {
        std::cerr << "error: event " << event->number << " loads " << bundle << ", not "
                  << trackb_path.filename().string() << '\n';
        return 3;
    }

    nfsu::RaceRoute route;
    std::string error;
    if (!nfsu::build_race_route(track, race.laps, race.direction, info->lap_skip, route,
                                error)) {
        std::cerr << "error: " << error << '\n';
        return 3;
    }
    std::cout << "race: laps " << route.laps << " (0x004342A4) direction "
              << (route.reverse ? "reverse" : "forward") << " point-to-point "
              << (route.point_to_point ? 1 : 0) << '\n';

    const nfsu::TrackPathRecord& path = track.paths.front();
    std::cout << "path container @" << track.container_offset << " records @"
              << track.path_chunk_offset << " nodes @" << track.node_chunk_offset << " zones @"
              << track.zone_chunk_offset << '\n';
    std::cout << "path 0 @" << path.offset << " closed " << static_cast<int>(path.closed)
              << " @" << path.offset + 1 << " nodes " << path.first << ".." << path.end
              << " count " << path.count << " @" << path.offset + 6 << " length "
              << format_float(path.length) << " @" << path.offset + 12 << " scale "
              << format_float(path.distance_scale) << " @" << path.offset + 16 << " offset "
              << format_float(path.distance_offset) << " @" << path.offset + 8 << '\n';
    float worst = 0.0F;
    for (std::size_t i = 0; i < route.spline.count; ++i) {
        const float running = route.spline.points[i * 3 + 2];
        const float stored = track.nodes[route.spline.first_node + i].distance;
        worst = std::max(worst, std::fabs(stored - running));
    }
    std::cout << "spline: uniform closed Catmull-Rom (0x007376B0), length "
              << format_float(route.spline.spline_length)
              << " (0x0059D9F0, 16 chords per segment); node distances differ from it by "
                 "at most "
              << format_float(worst) << '\n';
    for (std::size_t n = 0; n < track.nodes.size(); ++n) {
        const nfsu::TrackPathNode& node = track.nodes[n];
        const bool on_path = n >= route.spline.first_node &&
                             n < route.spline.first_node + route.spline.count;
        std::cout << "node " << n << " @" << node.offset << " x " << format_float(node.x)
                  << " y " << format_float(node.y) << " distance "
                  << format_float(node.distance) << " @" << node.offset + 16;
        if (on_path) {
            std::cout << " percent " << format_float(nfsu::race_node_percent(route, track, n));
        } else {
            std::cout << " not-on-path";
        }
        std::cout << '\n';
    }
    std::cout << "markers: chunk @" << track.marker_chunk_offset << " records @"
              << track.marker_record_offset << " count " << track.markers.size() << '\n';
    for (std::size_t m = 0; m < track.markers.size(); ++m) {
        const nfsu::TrackMarker& marker = track.markers[m];
        const char* name = nfsu::track_marker_name(marker.name_hash);
        std::cout << "marker " << m << " @" << marker.offset << ' '
                  << (name != nullptr ? name : "unnamed") << ' ' << hex32(marker.name_hash)
                  << " param " << marker.param << " x " << format_float(marker.position[0])
                  << " y " << format_float(marker.position[1]) << " z "
                  << format_float(marker.position[2]) << " angle " << hex32(marker.angle)
                  << '\n';
    }
    std::cout << "zones: " << track.zones.size() << '\n';
    for (std::size_t z = 0; z < track.zones.size(); ++z) {
        const nfsu::TrackPathZone& zone = track.zones[z];
        std::cout << "zone " << z << " @" << zone.offset << ' ' << race_zone_type_name(zone.type)
                  << " (" << zone.type << ") direction-filter "
                  << static_cast<int>(zone.direction_filter) << " box "
                  << format_float(zone.box_min[0]) << ' ' << format_float(zone.box_min[1])
                  << ' ' << format_float(zone.box_max[0]) << ' ' << format_float(zone.box_max[1])
                  << " vertices " << zone.vertex_count << '\n';
    }
    std::cout << "boundaries (0x00434810): " << route.boundaries.size() << " start "
              << route.start << " finish " << route.finish << '\n';
    for (std::size_t b = 0; b < route.boundaries.size(); ++b) {
        const nfsu::RaceBoundary& boundary = route.boundaries[b];
        std::cout << "boundary " << b << ' ' << boundary.name;
        if (boundary.marker >= 0) {
            std::cout << " marker " << boundary.marker << " @"
                      << track.markers[static_cast<std::size_t>(boundary.marker)].offset;
        } else {
            std::cout << " default";
        }
        std::cout << " percent " << format_float(boundary.percent) << " distance "
                  << format_float(boundary.percent * 0.01F * route.spline.path_length);
        if (static_cast<int>(b) == route.start) {
            std::cout << " start";
        }
        if (static_cast<int>(b) == route.finish) {
            std::cout << " finish";
        }
        std::cout << '\n';
    }
    std::cout << "lap rule: a forward crossing of boundary " << route.finish
              << " within " << format_float(nfsu::kRaceCrossingWindow)
              << " percent adds a lap, a backward one takes it back (0x004338C0); finished at "
              << route.laps << " laps\n";

    std::vector<float> points;
    std::string source;
    if (args.path.has_value()) {
        if (!read_race_path_file(*args.path, points, error)) {
            std::cerr << "error: " << error << '\n';
            return 3;
        }
        source = "path " + args.path->filename().string();
    } else if (!args.drive.empty()) {
        nfsu::RaceDrive drive;
        if (!nfsu::make_race_drive(route, track, args.drive == "backward", args.skip_from,
                                   args.skip_to, kRaceDriveSpacing, drive, error)) {
            std::cerr << "error: " << error << '\n';
            return 3;
        }
        points = std::move(drive.points);
        source = "drive " + args.drive + " from marker " + std::to_string(drive.grid_marker) +
                 " through " + std::to_string(drive.nodes.size()) + " nodes";
        if (args.skip_from >= 0) {
            source += ", jumping from node " + std::to_string(args.skip_from) + " to node " +
                      std::to_string(args.skip_to) + " past " + std::to_string(drive.skipped);
        }
    } else {
        return 0;
    }

    const std::size_t steps = points.size() / 2 - 1;
    std::cout << "replay: " << source << ", " << points.size() / 2 << " positions, " << steps
              << " steps of 1/60 s\n";
    nfsu::RaceProgress progress;
    nfsu::start_race_progress(route, track, points.data(), progress);
    std::cout << "start: x " << format_float(points[0]) << " y " << format_float(points[1])
              << " percent " << format_float(progress.previous_percent) << " next boundary "
              << progress.next_boundary << '\n';
    std::vector<nfsu::RaceProgressEvent> happened;
    for (std::size_t s = 1; s <= steps && !progress.finished; ++s) {
        happened.clear();
        nfsu::step_race_progress(route, track, progress, &points[s * 2], kRaceStepSeconds,
                                 happened);
        for (const nfsu::RaceProgressEvent& event_line : happened) {
            std::cout << "step " << event_line.step << ' '
                      << nfsu::race_event_kind_name(event_line.kind);
            switch (event_line.kind) {
            case nfsu::RaceEventKind::crossed_forward:
            case nfsu::RaceEventKind::crossed_backward:
            case nfsu::RaceEventKind::missed_forward:
            case nfsu::RaceEventKind::missed_backward:
                std::cout << " boundary " << event_line.index << ' '
                          << route.boundaries[static_cast<std::size_t>(event_line.index)].name;
                break;
            case nfsu::RaceEventKind::reset_zone:
                std::cout << " zone " << event_line.index;
                break;
            case nfsu::RaceEventKind::recovered:
            case nfsu::RaceEventKind::off_path:
                std::cout << " node " << event_line.index;
                break;
            default:
                break;
            }
            std::cout << " lap " << event_line.lap << " percent "
                      << format_float(event_line.percent) << '\n';
        }
    }
    std::cout << "result: steps " << progress.step << " laps "
              << nfsu::race_laps_completed(progress) << " of " << route.laps << " finished "
              << (progress.finished ? "yes at step " + std::to_string(progress.finish_step)
                                    : std::string("no"))
              << " lap-counter " << progress.lap << " highest " << progress.max_lap
              << " checkpoint " << progress.checkpoint << " highest-checkpoint "
              << progress.max_checkpoint << " next-boundary " << progress.next_boundary
              << " wrong-way " << (progress.wrong_way ? "yes" : "no") << '\n';
    return 0;
}

// ---------------------------------------------------------------------------
// race-timing: the race state machine of issue #62 over one career event's
// route - the countdown, the race clock, split, lap and total times, the
// finish and the result - replaying a path file or a built-in drive from
// the countdown to the finish. docs/RACE_TIMING.md.
// ---------------------------------------------------------------------------

std::string race_ticks_text(std::int32_t ticks) {
    return nfsu::format_race_ticks(ticks) + " (" + std::to_string(ticks) + ")";
}

std::string race_countdown_text(int value) {
    if (value == 4) {
        return "go";
    }
    if (value == 0) {
        return "off";
    }
    return std::to_string(value);
}

void print_race_timing_event(const nfsu::RaceRoute& route, const nfsu::RaceTimingState& state,
                             const nfsu::RaceTimingEvent& event) {
    std::cout << "step " << event.step << " tick " << event.now << ' ';
    const auto boundary_name = [&route](int index) {
        return route.boundaries[static_cast<std::size_t>(index)].name;
    };
    switch (event.kind) {
    case nfsu::RaceTimingEventKind::stage:
        std::cout << "stage "
                  << nfsu::race_stage_name(static_cast<nfsu::RaceStage>(event.value));
        break;
    case nfsu::RaceTimingEventKind::countdown:
        std::cout << "countdown " << race_countdown_text(event.value);
        break;
    case nfsu::RaceTimingEventKind::go:
        std::cout << "go race-start " << event.ticks;
        break;
    case nfsu::RaceTimingEventKind::split:
        std::cout << "split boundary " << event.boundary << ' ' << boundary_name(event.boundary)
                  << " lap " << event.lap << " time " << race_ticks_text(event.ticks)
                  << " crossing " << event.crossing << " backdate " << event.value;
        break;
    case nfsu::RaceTimingEventKind::lap: {
        std::cout << "lap " << event.lap << " boundary " << event.boundary << ' '
                  << boundary_name(event.boundary) << " time " << race_ticks_text(event.ticks);
        if (event.lap >= 0 && event.lap < nfsu::kRaceLapSlots && event.racer >= 0) {
            const nfsu::RaceRacerTiming& racer =
                state.racers[static_cast<std::size_t>(event.racer)];
            std::cout << " total "
                      << race_ticks_text(racer.lap_total_ticks[static_cast<std::size_t>(event.lap)]);
        }
        std::cout << " crossing " << event.crossing << " backdate " << event.value;
        break;
    }
    case nfsu::RaceTimingEventKind::untimed:
        std::cout << "untimed boundary " << event.boundary << ' ' << boundary_name(event.boundary)
                  << " lap " << event.lap
                  << (event.value == 1 ? " (no new checkpoint)" : " (no lap start)");
        break;
    case nfsu::RaceTimingEventKind::finished:
        std::cout << "finished position " << event.value << " time "
                  << race_ticks_text(event.ticks);
        break;
    case nfsu::RaceTimingEventKind::wrong_way_on:
        std::cout << "wrong-way-on";
        break;
    case nfsu::RaceTimingEventKind::wrong_way_off:
        std::cout << "wrong-way-off after " << race_ticks_text(event.ticks);
        break;
    case nfsu::RaceTimingEventKind::reset_zone:
        std::cout << "reset-zone zone " << event.value << " (the clock runs on)";
        break;
    case nfsu::RaceTimingEventKind::off_path:
        std::cout << "off-path node " << event.value << " (the clock runs on)";
        break;
    case nfsu::RaceTimingEventKind::restarted:
    case nfsu::RaceTimingEventKind::quit:
    case nfsu::RaceTimingEventKind::clock_limit:
        std::cout << nfsu::race_timing_event_kind_name(event.kind);
        break;
    }
    std::cout << '\n';
}

// Lists the route's boundaries and the clock's rules, then replays a path
// file or a built-in drive through nfsu::step_race_progress and the race
// state machine, 60 steps a second from the start of the countdown: the car
// holds the replay's first position until the step after go, then takes one
// position a step. Prints every stage and countdown change, every timed and
// untimed crossing with its time, every wrong-way change and reset, the
// finish, each lap's time and the result. Read-only; writes nothing.
int run_race_timing(const fs::path& globalb_path, const fs::path& frontb_path,
                    const fs::path& trackb_path, const RaceRouteArgs& args) {
    std::vector<unsigned char> globalb_image;
    const nfsu::TrackInfoTable tracks =
        nfsu::load_track_info_table(globalb_path, globalb_image);
    std::cout << "globalb: " << globalb_path.filename().string() << '\n';
    print_race_table_issues("track table", tracks.issues);
    std::vector<unsigned char> frontb_image;
    const nfsu::RaceEventTable events =
        nfsu::load_race_event_table(frontb_path, frontb_image);
    std::cout << "frontb: " << frontb_path.filename().string() << '\n';
    print_race_table_issues("event table", events.issues);
    std::vector<unsigned char> trackb_image;
    const nfsu::RaceRouteTrack track = nfsu::load_race_route_track(trackb_path, trackb_image);
    std::cout << "trackb: " << trackb_path.filename().string() << '\n';
    print_race_route_issues(track.issues);
    if (!tracks.ok() || !events.ok() || !track.ok()) {
        return 3;
    }
    const nfsu::RaceEventRecord* event = nfsu::find_race_event(events, args.event);
    if (event == nullptr) {
        std::cerr << "error: the event table holds no event " << args.event << '\n';
        return 3;
    }
    const nfsu::RaceSlot& race = event->races.front();
    const nfsu::TrackInfoRecord* info = nfsu::find_track_info(tracks, race.track);
    std::cout << "event " << event->number << " type " << nfsu::race_type_name(event->type)
              << " (" << event->type << ") track " << race.track << " laps "
              << static_cast<int>(race.laps) << " direction "
              << nfsu::race_direction_name(race.direction) << '\n';
    if (info == nullptr) {
        std::cerr << "error: event " << event->number << " names track " << race.track
                  << ", which is not in the track table\n";
        return 3;
    }
    const std::string bundle = nfsu::track_bundle_file_name(info->number);
    if (lower_ascii(trackb_path.filename().string()) != lower_ascii(bundle)) {
        std::cerr << "error: event " << event->number << " loads " << bundle << ", not "
                  << trackb_path.filename().string() << '\n';
        return 3;
    }
    nfsu::RaceRoute route;
    std::string error;
    if (!nfsu::build_race_route(track, race.laps, race.direction, info->lap_skip, route,
                                error)) {
        std::cerr << "error: " << error << '\n';
        return 3;
    }
    std::cout << "route: laps " << route.laps << " direction "
              << (route.reverse ? "reverse" : "forward") << " boundaries "
              << route.boundaries.size() << " start " << route.start << " finish "
              << route.finish << " path-length " << format_float(route.spline.path_length)
              << '\n';
    for (std::size_t b = 0; b < route.boundaries.size(); ++b) {
        std::cout << "boundary " << b << ' ' << route.boundaries[b].name << " percent "
                  << format_float(route.boundaries[b].percent) << '\n';
    }

    nfsu::RaceTimingConfig config;
    if (!nfsu::make_race_timing_config(60, 1, config, error)) {
        std::cerr << "error: " << error << '\n';
        return 3;
    }
    nfsu::RaceTimingState state;
    if (!nfsu::start_race_timing(route, config, state, error)) {
        std::cerr << "error: " << error << '\n';
        return 3;
    }
    std::cout << "clock: " << nfsu::kRaceTicksPerSecond
              << " ticks a second (0x006CC9FC), the world timer built at "
              << config.start_ticks << " (0x0041F7DA), " << config.steps_per_second
              << " steps a second of " << config.ticks_per_step
              << " ticks (0x00585CD5..0x00585E90), times rounded to 1/100 s (0x00586690)\n";
    std::cout << "countdown: three steps of " << format_float(config.countdown_step_seconds)
              << " s (0x006B79E0), go at " << format_float(config.countdown_step_seconds * 3.0F)
              << " s (0x00434D9F)\n";

    std::vector<float> points;
    std::string source;
    if (args.path.has_value()) {
        if (!read_race_path_file(*args.path, points, error)) {
            std::cerr << "error: " << error << '\n';
            return 3;
        }
        source = "path " + args.path->filename().string();
    } else {
        nfsu::RaceDrive drive;
        if (!nfsu::make_race_drive(route, track, args.drive == "backward", args.skip_from,
                                   args.skip_to, kRaceDriveSpacing, drive, error)) {
            std::cerr << "error: " << error << '\n';
            return 3;
        }
        points = std::move(drive.points);
        source = "drive " + args.drive + " from marker " + std::to_string(drive.grid_marker) +
                 " through " + std::to_string(drive.nodes.size()) + " nodes";
        if (args.skip_from >= 0) {
            source += ", jumping from node " + std::to_string(args.skip_from) + " to node " +
                      std::to_string(args.skip_to) + " past " + std::to_string(drive.skipped);
        }
    }
    const std::size_t positions = points.size() / 2;
    std::cout << "replay: " << source << ", " << positions
              << " positions; the car holds position 0 until the step after go\n";

    std::vector<nfsu::RaceTimingEvent> happened;
    nfsu::set_race_stage(state, nfsu::RaceStage::countdown, happened);
    nfsu::RaceProgress progress;
    nfsu::start_race_progress(route, track, points.data(), progress);
    std::size_t next = 1;
    float at[2] = {points[0], points[1]};
    std::vector<nfsu::RaceProgressEvent> moved;
    bool ended = false;
    while (state.outcome == nfsu::RaceOutcome::running) {
        for (const nfsu::RaceTimingEvent& line : happened) {
            print_race_timing_event(route, state, line);
        }
        happened.clear();
        float speed = 0.0F;
        if (!nfsu::race_car_held(state)) {
            if (next >= positions) {
                ended = true;
                break;
            }
            const float dx = points[next * 2] - at[0];
            const float dy = points[next * 2 + 1] - at[1];
            speed = std::sqrt(dy * dy + dx * dx) / kRaceStepSeconds;
            at[0] = points[next * 2];
            at[1] = points[next * 2 + 1];
            ++next;
        }
        moved.clear();
        if (!progress.finished) {
            nfsu::step_race_progress(route, track, progress, at, kRaceStepSeconds, moved);
        }
        for (const nfsu::RaceProgressEvent& step_event : moved) {
            if (step_event.kind == nfsu::RaceEventKind::crossed_backward ||
                step_event.kind == nfsu::RaceEventKind::missed_forward ||
                step_event.kind == nfsu::RaceEventKind::missed_backward) {
                std::cout << "step " << state.step << " tick " << state.now_ticks << " route "
                          << nfsu::race_event_kind_name(step_event.kind) << " boundary "
                          << step_event.index << ' '
                          << route.boundaries[static_cast<std::size_t>(step_event.index)].name
                          << " lap " << step_event.lap << " percent "
                          << format_float(step_event.percent) << '\n';
            }
        }
        nfsu::RaceTimingInput input;
        input.events = moved.data();
        input.event_count = moved.size();
        input.speed = speed;
        if (!nfsu::step_race_timing(route, config, state, &input, happened, error)) {
            std::cerr << "error: " << error << '\n';
            return 3;
        }
    }
    for (const nfsu::RaceTimingEvent& line : happened) {
        print_race_timing_event(route, state, line);
    }
    if (ended) {
        std::cout << "replay: the positions ran out after step "
                  << (state.step == 0 ? 0 : state.step - 1) << '\n';
    }

    const nfsu::RaceRacerTiming& racer = state.racers.front();
    for (int lap = 1; lap < nfsu::kRaceLapSlots && lap <= racer.highest_lap; ++lap) {
        const auto slot = static_cast<std::size_t>(lap);
        std::cout << "lap-time " << lap << ' ' << race_ticks_text(racer.lap_ticks[slot])
                  << " total " << race_ticks_text(racer.lap_total_ticks[slot]) << '\n';
    }
    for (const nfsu::RaceResultRow& row : nfsu::race_timing_results(route, state)) {
        std::cout << "result: racer " << row.racer << ' ' << nfsu::racer_status_name(row.status)
                  << " position " << row.position << " laps " << row.laps << " of "
                  << route.laps << " time "
                  << (row.total_valid ? race_ticks_text(row.total_ticks) : std::string("none"))
                  << " best-lap " << race_ticks_text(row.best_lap_ticks) << '\n';
    }
    std::cout << "race: outcome " << nfsu::race_outcome_name(state.outcome) << " stage "
              << nfsu::race_stage_name(state.stage) << " steps " << state.step << " go-step "
              << state.go_step << " race-start " << state.race_start_ticks << " race-time "
              << race_ticks_text(state.race_ticks) << " fastest-lap "
              << race_ticks_text(state.fastest_lap_ticks) << " wrong-way "
              << (racer.wrong_way ? "yes" : "no") << " wrong-way-steps " << racer.wrong_way_steps
              << " resets " << racer.resets << '\n';
    nfsu::RaceHud hud;
    nfsu::race_timing_hud(route, config, state, 0, hud);
    // Where retail's HUD shows no time: a text for the lap timer without a
    // lap start (0x004A1A58), "--:--:--" for a best lap of 0 (0x004A1ABD).
    std::cout << "hud: countdown " << race_countdown_text(hud.countdown) << " race-time "
              << (hud.race_time_valid ? nfsu::format_race_ticks(hud.race_ticks)
                                      : std::string("none"))
              << " lap-time "
              << (hud.lap_time_valid ? nfsu::format_race_ticks(hud.lap_ticks)
                                     : std::string("text"))
              << (hud.lap_time_held ? " held" : "") << " best-lap "
              << (hud.best_lap_valid ? nfsu::format_race_ticks(hud.best_lap_ticks)
                                     : std::string("--:--:--"))
              << " lap " << hud.lap << " of " << hud.laps << " wrong-way "
              << (hud.wrong_way ? "yes" : "no") << " finished " << (hud.finished ? "yes" : "no")
              << " position " << hud.position << '\n';
    return 0;
}

// ---------------------------------------------------------------------------
// vehicle-track: the vehicle model of docs/VEHICLE.md on a track's collision,
// with its body against the track and its resets - docs/VEHICLE_TRACK.md.
// The course is event 1's circuit, read from the retail files as race-route
// reads them, with the car on grid slot 0, or the proving ground, which is
// built in code. The run is a built-in scenario or a script file of
// docs/VEHICLE.md's form, started on the grid. It prints the course, the
// start, a line every `every` steps, every contact streak, reset and race
// event, the last step, a summary and the SHA-256 of the telemetry, which
// docs/VEHICLE_TRACK.md pins, and for a built-in scenario whether it equals
// the pin. Read-only; writes nothing.
// ---------------------------------------------------------------------------

struct VehicleTrackArgs {
    bool proving_ground = false;
    fs::path globalb;
    fs::path frontb;
    fs::path trackb;
    std::uint32_t event = 0;
    std::string scenario;
    bool has_script = false;
    fs::path script_file;
    bool has_steps = false;
    std::uint64_t steps = 0;
    std::uint64_t every = 120;
};

void print_vehicle_track_line(const char* label, const nfsu::VehicleTrackTelemetry& t) {
    const double w = t.orientation.w;
    const double x = t.orientation.x;
    const double y = t.orientation.y;
    const double z = t.orientation.z;
    const double forward_x = 1.0 - 2.0 * (y * y + z * z);
    const double forward_y = 2.0 * (x * y + w * z);
    const double forward_z = 2.0 * (x * z - w * y);
    const double up_z = 1.0 - 2.0 * (x * x + y * y);
    const double along =
        t.velocity.x * forward_x + t.velocity.y * forward_y + t.velocity.z * forward_z;
    const std::uint16_t heading = nfsu::track_angle(static_cast<float>(forward_x),
                                                    static_cast<float>(forward_y));
    std::cout << label << " step " << t.step << " t "
              << vehicle_fixed(static_cast<double>(t.step) * nfsu::kVehicleStepSeconds, 3)
              << " pos " << vehicle_fixed(t.position.x, 3) << ' '
              << vehicle_fixed(t.position.y, 3) << ' ' << vehicle_fixed(t.position.z, 4)
              << " kmh " << vehicle_fixed(along * 3.6, 2) << " heading "
              << hex32(heading) << " up " << vehicle_fixed(up_z, 4) << " gear "
              << vehicle_gear_name(t.model.gear) << " wheels";
    for (nfsu::VehicleTrackSurface surface : t.wheel_surface) {
        std::cout << ' ' << nfsu::vehicle_track_surface_name(surface);
    }
    std::cout << " hull " << t.hull_hits << " contacts " << t.contacts << " lap " << t.lap
              << " percent " << format_float(t.percent) << '\n';
}

int run_vehicle_track(const VehicleTrackArgs& args) {
    nfsu::VehicleTrackCourse course;
    std::string error;
    nfsu::VehicleTrackCourseKind kind = nfsu::VehicleTrackCourseKind::proving_ground;
    if (args.proving_ground) {
        if (!nfsu::build_vehicle_proving_ground(course, error)) {
            std::cerr << "error: the proving ground did not build: " << error << '\n';
            return 3;
        }
        std::cout << "course: proving ground, built in code, " << course.world.collision.polygons.size()
                  << " polygons, route of " << course.route.spline.count << " nodes, "
                  << course.track.zones.size() << " zones\n";
    } else {
        kind = nfsu::VehicleTrackCourseKind::circuit;
        std::vector<unsigned char> globalb_image;
        const nfsu::TrackInfoTable tracks =
            nfsu::load_track_info_table(args.globalb, globalb_image);
        print_race_table_issues("track table", tracks.issues);
        std::vector<unsigned char> frontb_image;
        const nfsu::RaceEventTable events =
            nfsu::load_race_event_table(args.frontb, frontb_image);
        print_race_table_issues("event table", events.issues);
        if (!tracks.ok() || !events.ok()) {
            std::cerr << "error: the track or event table is not valid\n";
            return 3;
        }
        const nfsu::RaceEventRecord* event = nfsu::find_race_event(events, args.event);
        if (event == nullptr) {
            std::cerr << "error: the event table holds no event " << args.event << '\n';
            return 3;
        }
        const nfsu::RaceSlot& race = event->races.front();
        const nfsu::TrackInfoRecord* info = nfsu::find_track_info(tracks, race.track);
        if (info == nullptr) {
            std::cerr << "error: event " << event->number << " names track " << race.track
                      << ", which is not in the track table\n";
            return 3;
        }
        const std::string bundle = nfsu::track_bundle_file_name(info->number);
        if (lower_ascii(args.trackb.filename().string()) != lower_ascii(bundle)) {
            std::cerr << "error: event " << event->number << " loads " << bundle << ", not "
                      << args.trackb.filename().string() << '\n';
            return 3;
        }
        std::cout << "event " << event->number << " race 0 track " << race.track << " laps "
                  << static_cast<int>(race.laps) << " direction "
                  << nfsu::race_direction_name(race.direction) << " lap-skip "
                  << static_cast<int>(info->lap_skip) << " bundle " << bundle << '\n';
        if (!nfsu::load_vehicle_track_course(args.trackb, race.laps, race.direction,
                                             info->lap_skip, 0, course, error)) {
            std::cerr << "error: " << error << '\n';
            return 3;
        }
        std::cout << "course: " << course.name << ", " << course.world.collision.polygons.size()
                  << " polygons, route of " << course.route.spline.count << " nodes, "
                  << course.track.zones.size() << " zones\n";
    }
    const nfsu::TrackMarker& grid =
        course.track.markers[static_cast<std::size_t>(course.start_marker)];
    std::cout << "start: grid slot 0 marker " << course.start_marker << " x "
              << format_float(grid.position[0]) << " y " << format_float(grid.position[1])
              << " z " << format_float(grid.position[2]) << " angle "
              << hex32(grid.angle) << " ground " << vehicle_fixed(course.start.ground.z, 4)
              << '\n';

    nfsu::VehicleTrackScenario scenario;
    if (args.has_script) {
        std::ifstream stream(args.script_file, std::ios::binary);
        if (!stream) {
            std::cerr << "error: cannot open the script " << args.script_file.string() << '\n';
            return 2;
        }
        const nfsu::VehicleScriptLimits limits;
        std::vector<char> buffer(limits.max_bytes + 1);
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::string text(buffer.data(), static_cast<std::size_t>(stream.gcount()));
        if (!nfsu::parse_vehicle_script(text, scenario.script, error, limits)) {
            std::cerr << "error: script " << args.script_file.filename().string() << ": "
                      << error << '\n';
            return 2;
        }
        scenario.name = args.script_file.filename().string();
        scenario.script.name = scenario.name;
        scenario.description = "a script file, from the start";
    } else if (!nfsu::vehicle_track_scenario(kind, args.scenario, scenario)) {
        std::cerr << "error: no built-in scenario is named '" << args.scenario
                  << "'; the names are";
        for (const nfsu::VehicleTrackScenario& s : nfsu::vehicle_track_scenarios(kind)) {
            std::cerr << ' ' << s.name;
        }
        std::cerr << '\n';
        return 1;
    }

    const nfsu::VehicleModel model(nfsu::stock_240sx_vehicle_spec());
    if (!model.valid()) {
        std::cerr << "error: the model refuses the built-in 240SX: " << model.error() << '\n';
        return 4;
    }
    nfsu::VehicleTrackSession session(model, course);
    if (!session.valid()) {
        std::cerr << "error: " << session.error() << '\n';
        return 4;
    }
    const nfsu::VehiclePose pose = nfsu::vehicle_track_scenario_pose(course.start, scenario);
    if (!session.start(pose)) {
        std::cerr << "error: the model refused the starting pose\n";
        return 4;
    }
    const std::uint64_t steps = args.has_steps ? args.steps : scenario.script.total_steps();
    std::cout << "scenario " << scenario.name << ": " << scenario.description << '\n';
    std::cout << "scenario start: forward " << vehicle_fixed(scenario.forward, 3) << " left "
              << vehicle_fixed(scenario.left, 3) << " up " << vehicle_fixed(scenario.up, 3)
              << " turn " << scenario.turn << " ground " << vehicle_fixed(pose.ground.x, 3)
              << ' ' << vehicle_fixed(pose.ground.y, 3) << ' ' << vehicle_fixed(pose.ground.z, 4)
              << " speed " << vehicle_fixed(pose.speed, 3) << " steps " << steps << " of 1/"
              << nfsu::kVehicleStepsPerSecond << " s\n";

    nfsu::VehicleTrackTelemetryHash hash;
    nfsu::VehicleTrackTelemetry telemetry;
    std::uint64_t contact_steps = 0;
    std::uint64_t impulses = 0;
    std::uint64_t off_road_steps = 0;
    double deepest = 0.0;
    // A contact streak is steps with contact less than 12 steps (0.1 s)
    // apart: a car held against a wall touches it every few steps.
    constexpr std::uint64_t kStreakGap = 12;
    std::uint64_t streak = 0;
    std::uint64_t streak_last = 0;
    double streak_depth = 0.0;
    std::uint64_t streak_impulses = 0;
    nfsu::VehicleTrackReset pending = nfsu::VehicleTrackReset::none;
    for (std::uint64_t i = 0; i < steps; ++i) {
        if (!session.step(scenario.script.input_at(i), telemetry)) {
            std::cerr << "error: the session stopped at step " << i + 1
                      << ": the model refused the step\n";
            return 4;
        }
        hash.add(telemetry);
        bool off_road = false;
        for (nfsu::VehicleTrackSurface surface : telemetry.wheel_surface) {
            off_road = off_road || surface != nfsu::VehicleTrackSurface::road;
        }
        off_road_steps += off_road ? 1U : 0U;
        impulses += telemetry.impulses;
        if (telemetry.contacts > 0) {
            ++contact_steps;
            deepest = std::max(deepest, telemetry.deepest_depth);
            if (streak == 0) {
                std::cout << "contact step " << telemetry.step << " polygon "
                          << telemetry.deepest_polygon << ' '
                          << nfsu::vehicle_track_surface_name(telemetry.deepest_surface)
                          << " depth " << vehicle_fixed(telemetry.deepest_depth, 4)
                          << " hull " << telemetry.hull_hits << " impulse "
                          << vehicle_fixed(telemetry.impulse.x, 1) << ' '
                          << vehicle_fixed(telemetry.impulse.y, 1) << ' '
                          << vehicle_fixed(telemetry.impulse.z, 1) << '\n';
                streak_depth = 0.0;
                streak_impulses = 0;
            }
            ++streak;
            streak_last = telemetry.step;
            streak_depth = std::max(streak_depth, telemetry.deepest_depth);
            streak_impulses += telemetry.impulses;
        } else if (streak != 0 && telemetry.step - streak_last >= kStreakGap) {
            std::cout << "contact-end step " << streak_last << ": " << streak
                      << " steps with contact, deepest " << vehicle_fixed(streak_depth, 4)
                      << " impulses " << streak_impulses << '\n';
            streak = 0;
        }
        if (telemetry.pending != nfsu::VehicleTrackReset::none &&
            telemetry.pending != pending) {
            std::cout << "reset-requested step " << telemetry.step << ' '
                      << nfsu::vehicle_track_reset_name(telemetry.pending) << '\n';
        }
        pending = telemetry.pending;
        if (telemetry.bounds == nfsu::VehicleTrackBoundsCheck::none) {
            std::cout << "bounds step " << telemetry.step
                      << ": no collision under the car nor one unit along +x\n";
        }
        if (telemetry.reset != nfsu::VehicleTrackReset::none) {
            const nfsu::VehicleTrackResetInfo& reset = session.last_reset();
            std::cout << "reset step " << telemetry.step << ' '
                      << nfsu::vehicle_track_reset_name(telemetry.reset);
            if (!reset.performed) {
                std::cout << " not performed\n";
            } else {
                if (reset.node >= 0) {
                    std::cout << " node " << reset.node;
                }
                if (reset.zone >= 0) {
                    std::cout << " zone " << reset.zone;
                }
                std::cout << " ground " << vehicle_fixed(reset.pose.ground.x, 3) << ' '
                          << vehicle_fixed(reset.pose.ground.y, 3) << ' '
                          << vehicle_fixed(reset.pose.ground.z, 4) << " heading "
                          << hex32(reset.heading_units) << " speed "
                          << vehicle_fixed(reset.pose.speed, 3) << '\n';
            }
        }
        if (telemetry.tick) {
            for (const nfsu::RaceProgressEvent& event : session.events()) {
                std::cout << "race step " << telemetry.step << ' '
                          << nfsu::race_event_kind_name(event.kind) << " index " << event.index
                          << " lap " << event.lap << " percent " << format_float(event.percent)
                          << '\n';
            }
        }
        if (args.every != 0 && (i + 1) % args.every == 0 && i + 1 != steps) {
            print_vehicle_track_line("at", telemetry);
        }
    }
    if (streak != 0) {
        std::cout << "contact-end step " << streak_last << ": " << streak
                  << " steps with contact, deepest " << vehicle_fixed(streak_depth, 4)
                  << " impulses " << streak_impulses << " (in contact to the end)\n";
    }
    if (steps != 0) {
        print_vehicle_track_line("final", telemetry);
    }
    std::cout << "summary: steps " << steps << " contact-steps " << contact_steps
              << " impulses " << impulses << " deepest " << vehicle_fixed(deepest, 4)
              << " resets " << telemetry.resets << " steps-with-a-wheel-off-road "
              << off_road_steps << '\n';
    const std::uint64_t records = hash.records();
    const std::string digest = nfsu::sha256_hex(hash.finish());
    std::cout << "telemetry: records " << records << " bytes "
              << records * nfsu::kVehicleTrackTelemetryRecordBytes << " sha256 " << digest
              << '\n';
    if (!scenario.pinned.empty() && !args.has_steps) {
        std::cout << "pinned: " << (digest == scenario.pinned ? "EQUAL to" : "DIFFERENT from")
                  << " docs/VEHICLE_TRACK.md " << scenario.pinned << '\n';
    }
    return 0;
}

// Parses everything after "vehicle-track". Every path is converted here,
// before anything is read, so a path argument the platform cannot decode is
// refused before any work.
int parse_vehicle_track(int argc, char** argv, VehicleTrackArgs& args) {
    int i = 2;
    if (argc > 2 && std::string(argv[2]) == "--proving-ground") {
        args.proving_ground = true;
        i = 3;
    } else {
        if (argc < 7) {
            std::cerr << "error: vehicle-track needs <GLOBALB.BUN> <FrontB.lzc> "
                         "<TRACKB####.lzc> <event> and a scenario or --script <FILE>, "
                         "or --proving-ground and a scenario or --script <FILE>\n";
            return 1;
        }
        args.globalb = nfsu::argument_path(argv[2]);
        args.frontb = nfsu::argument_path(argv[3]);
        args.trackb = nfsu::argument_path(argv[4]);
        std::uint64_t event = 0;
        if (!parse_u64(std::string(argv[5]), event) || event > 0xFFFFFFFFULL) {
            std::cerr << "error: the event must be a number\n";
            return 1;
        }
        args.event = static_cast<std::uint32_t>(event);
        i = 6;
    }
    bool has_name = false;
    bool has_every = false;
    for (; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--script") {
            if (args.has_script) {
                std::cerr << "error: --script is given twice\n";
                return 1;
            }
            if (i + 1 >= argc) {
                std::cerr << "error: --script needs a file\n";
                return 1;
            }
            ++i;
            args.has_script = true;
            args.script_file = nfsu::argument_path(argv[i]);
        } else if (argument == "--steps" || argument == "--every") {
            const bool steps = argument == "--steps";
            if (steps ? args.has_steps : has_every) {
                std::cerr << "error: " << argument << " is given twice\n";
                return 1;
            }
            if (i + 1 >= argc) {
                std::cerr << "error: " << argument << " needs a value\n";
                return 1;
            }
            ++i;
            std::uint64_t value = 0;
            if (!parse_u64(std::string(argv[i]), value) || value > 12000000U) {
                std::cerr << "error: " << argument
                          << " must be a decimal or 0x integer no larger than 12000000\n";
                return 1;
            }
            (steps ? args.steps : args.every) = value;
            (steps ? args.has_steps : has_every) = true;
        } else if (argument.size() > 1 && argument[0] == '-') {
            std::cerr << "error: unknown option " << argument << '\n';
            return 1;
        } else if (has_name) {
            std::cerr << "error: give one scenario name only\n";
            return 1;
        } else {
            has_name = true;
            args.scenario = argument;
        }
    }
    if (has_name == args.has_script) {
        std::cerr << "error: give either a built-in scenario name or --script <FILE>\n";
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// track-stream-path: the sections speed.exe's streamer keeps resident while
// one car drives a path, step by step, and what each resident set costs. See
// docs/TRACK_STREAMING.md.
//
// The track's TRACKB####.LZC supplies the streaming table and the zones; the
// path is a file of raw-slot vertices, or the hand-given polyline built in
// for track 1003. The command samples the path 60 times a second at one
// speed, runs nfsu::simulate_track_residency, prints every step that did
// something and a SHA-256 of the whole sequence, and loads every distinct
// resident set it meets - exactly their byte ranges and nothing else - to
// measure this project's own bytes for it against the budgets of issue #52.
// With --out it draws frames from only the resident sections with the CPU
// reference renderer, and never replaces a file.
// ---------------------------------------------------------------------------

constexpr double kStreamPathDefaultSpeed = 30.0;
constexpr double kStreamPathMaxSpeed = 1000.0;
constexpr std::uint64_t kStreamPathMaxPathBytes = 16ULL * 1024ULL * 1024ULL;
// The camera the frames are drawn from: an orbit camera whose target is
// kStreamPathCameraLead units ahead of the path point along the direction of
// travel and kStreamPathCameraHeight above the path, seen from
// kStreamPathCameraLead away and kStreamPathCameraPitch degrees above it - so
// the eye stands over the path point itself, about two units up, looking
// ahead. A camera behind the point would stand in whatever lines the street.
// It is NOT the game's chase camera, which is issue #59's.
constexpr double kStreamPathCameraHeight = 1.5;
constexpr double kStreamPathCameraLead = 10.0;
constexpr double kStreamPathCameraPitch = 3.0;
constexpr double kStreamPathCameraFov = 60.0;
constexpr double kStreamPathCameraNear = 0.5;
constexpr double kStreamPathCameraFar = 3000.0;

enum class StreamPathCost { table, prepared };
enum class StreamPathBudget { pool, switch_region, switch_arena, bytes };
enum class StreamPathList { events, all, none };

struct TrackStreamPathArgs {
    bool gpu_textures = false;
    bool has_path_file = false;
    fs::path path_file;
    bool has_hand_path = false;
    double speed = kStreamPathDefaultSpeed;
    nfsu::TrackResidencyPolicy policy = nfsu::TrackResidencyPolicy::retail;
    StreamPathCost cost = StreamPathCost::table;
    bool has_budget = false;
    StreamPathBudget budget = StreamPathBudget::pool;
    std::uint64_t budget_bytes = 0;
    bool has_pack = false;
    fs::path pack;
    std::vector<fs::path> textures;
    StreamPathList list = StreamPathList::events;
    bool has_frames = false;
    std::uint64_t frame_every = 0;
    bool has_output = false;
    fs::path output;
    std::uint32_t width = nfsu::RenderOptions{}.width;
    std::uint32_t height = nfsu::RenderOptions{}.height;
};

struct StreamPathTextureFiles {
    std::vector<std::vector<unsigned char>> bytes;
    std::vector<nfsu::TexturePackSet> sets;
};

// One resident set built as a scene. Held behind a pointer so that nothing
// the texture sources point into moves.
struct StreamPathScene {
    nfsu::TrackStreamLoad load;
    nfsu::RenderTextureSet textures;
    nfsu::PreparedTrackScene scene;
    nfsu::TrackDrawList list;
    std::vector<std::size_t> source_owner;
    std::size_t section_sources = 0;
};

// What one resident set costs this project.
struct StreamPathMeasure {
    std::vector<std::size_t> records;
    std::size_t first_step = 0;
    bool reports_ok = false;
    std::uint64_t bytes_read = 0;
    std::uint64_t triangles = 0;
    std::uint64_t placed = 0;
    std::uint64_t texture_bytes = 0;
    std::uint64_t resident_bytes = 0;
    std::uint64_t cpu_upload_peak = 0;
    std::uint64_t gpu_peak = 0;
    std::uint64_t largest_section = 0;
    std::vector<nfsu::TrackWholeBudgetRow> rows;
    std::uint64_t over = 0;
};

nfsu::TrackStreamReason stream_path_reason(const nfsu::TrackStreamTable& table,
                                           const nfsu::TrackZoneReport& zones,
                                           std::uint32_t track,
                                           std::size_t record) {
    const std::int32_t number = table.records[record].number;
    for (const std::int16_t fixed : nfsu::kTrackStreamFixedSections) {
        if (fixed == number) {
            return nfsu::TrackStreamReason::fixed;
        }
    }
    for (const nfsu::TrackZoneSectionList& list : zones.section_lists) {
        if (list.chunk_id != nfsu::kTrackZoneExtraListId || list.key != track) {
            continue;
        }
        for (std::size_t k = 0; list.count > 0 &&
                                k < static_cast<std::size_t>(list.count) &&
                                k < list.sections.size();
             ++k) {
            if (list.sections[k] == number) {
                return nfsu::TrackStreamReason::extra_list;
            }
        }
        break;
    }
    return table.records[record].track_flag != 0
               ? nfsu::TrackStreamReason::track_flag
               : nfsu::TrackStreamReason::zone_list;
}

// Every problem a load raised, each naming its section, printed once per
// table record however many resident sets load that record again, as
// track-whole prints its own. `reported` holds one flag per table record and
// one more for the file-level issues.
void print_stream_path_load_issues(const nfsu::TrackStreamLoad& load,
                                   std::vector<std::uint8_t>& reported) {
    if (!load.issues.empty() && reported.back() == 0) {
        reported.back() = 1;
        print_track_whole_issues("stream", load.issues);
    }
    for (const nfsu::TrackStreamSection& section : load.sections) {
        if (section.record >= reported.size() - 1 || reported[section.record] != 0) {
            continue;
        }
        reported[section.record] = 1;
        const std::string label =
            "section " + section.name + " (" + std::to_string(section.number) + ")";
        print_track_whole_issues(label.c_str(), section.issues);
        print_track_whole_issues((label + " geometry").c_str(), section.geometry.issues);
        print_track_whole_issues((label + " object").c_str(), section.objects.issues);
        for (const nfsu::TextureIssue& issue : section.packs.issues) {
            std::cerr << label << " pack issue at " << (section.offset + issue.offset)
                      << ": " << issue.message << '\n';
        }
        for (const nfsu::TexturePack& pack : section.packs.packs) {
            for (const nfsu::TextureIssue& issue : pack.issues) {
                std::cerr << label << " pack issue at "
                          << (section.offset + issue.offset) << ": " << issue.message
                          << '\n';
            }
        }
    }
}

// Loads exactly `records`, in that order, and builds and costs them as
// track-whole builds and costs its set.
bool build_stream_path_scene(const nfsu::TrackStreamTable& table,
                             const nfsu::TrackZoneReport& zones,
                             std::uint32_t track,
                             const std::vector<std::size_t>& records,
                             const nfsu::TrackStreamFiles& files,
                             StreamPathTextureFiles& texture_files,
                             bool gpu_textures,
                             std::vector<std::uint8_t>& reported,
                             StreamPathScene& out,
                             StreamPathMeasure& measure,
                             std::string& error) {
    nfsu::TrackStreamSelection selection;
    selection.records = records;
    for (const std::size_t record : records) {
        selection.reasons.push_back(stream_path_reason(table, zones, track, record));
    }
    const nfsu::TrackStreamLoadLimits load_limits;
    if (!nfsu::load_track_stream_sections(table, selection, files, load_limits,
                                          out.load, error)) {
        return false;
    }
    print_stream_path_load_issues(out.load, reported);
    const std::vector<std::uint32_t> wanted =
        nfsu::track_region_texture_hashes(out.load.geometry);
    std::vector<nfsu::RenderTextureSource> sources;
    std::vector<std::size_t> owners;
    nfsu::track_stream_texture_sources(out.load, &wanted, sources, owners);
    out.section_sources = sources.size();
    out.source_owner = owners;
    for (std::size_t i = 0; i < texture_files.bytes.size(); ++i) {
        nfsu::RenderTextureSource source;
        source.bytes = &texture_files.bytes[i];
        for (const nfsu::TexturePack& pack : texture_files.sets[i].packs) {
            source.packs.push_back(&pack);
        }
        source.complete = texture_files.sets[i].complete;
        source.wanted = &wanted;
        sources.push_back(std::move(source));
        out.source_owner.push_back(nfsu::kTrackStreamNoRecord);
    }
    nfsu::RenderTextureLimits texture_limits;
    texture_limits.max_total_bytes = kTrackWholeTextureBytes;
    texture_limits.gpu_blocks = gpu_textures;
    if (!nfsu::decode_render_texture_sources(sources, texture_limits,
                                             out.textures, error)) {
        return false;
    }
    nfsu::TrackSceneOptions scene_options;
    scene_options.region.max_texture_bytes = kTrackWholeTextureBytes;
    if (!nfsu::build_track_scene(out.load.geometry, out.load.objects,
                                 out.textures, scene_options, out.scene, error)) {
        return false;
    }
    std::vector<nfsu::TrackVisibleInstance> everything;
    for (const nfsu::TrackSceneSection& section : out.scene.sections) {
        for (std::size_t k = 0; k < section.source_instances; ++k) {
            nfsu::TrackVisibleInstance item;
            item.section_index = section.section_index;
            item.instance_index = k;
            everything.push_back(item);
        }
    }
    nfsu::TrackDrawListOptions list_options;
    list_options.order = nfsu::TrackDrawOrder::material_group;
    if (!nfsu::build_track_draw_list(out.scene, everything, list_options,
                                     out.list, error)) {
        return false;
    }
    nfsu::UploadTable upload;
    if (!nfsu::build_track_upload_table(out.scene, upload, error)) {
        return false;
    }
    nfsu::SceneMemoryRequirement gpu;
    if (!nfsu::upload_table_requirement(upload, gpu_textures ? nfsu::bounded_image_layout :
                                      nfsu::linear_image_layout, gpu, error)) {
        return false;
    }
    nfsu::TrackBudgetCost prepared;
    nfsu::track_budget_prepare_cost(out.scene, prepared);
    measure.records = records;
    measure.reports_ok = out.load.ok();
    measure.bytes_read = out.load.bytes_read;
    measure.triangles = out.scene.region.cost.triangles;
    measure.placed = out.scene.cost.placed_instances;
    measure.texture_bytes = prepared.prepare_texture_bytes;
    measure.resident_bytes = prepared.prepare_resident_bytes;
    measure.rows.clear();
    if (gpu_textures) {
        // Preparation temporarily owns both the decoded set and the scene's
        // copy. The set is released immediately after preparation; retained
        // blocks stay with their section until eviction. GPU staging has its
        // own budget and is reclaimed only after the upload fence.
        measure.cpu_upload_peak = prepared.prepare_resident_bytes + out.textures.decoded_bytes;
        const auto aligned = [](std::uint64_t n) { return (n + 4095) & ~std::uint64_t{4095}; };
        // Staging includes tight and padded copies simultaneously, plus the
        // ledger's per-resource alignment. Reserve 64 MiB for bounded draw
        // commands, uniforms, shaders, descriptors and presentation targets.
        std::uint64_t staging = upload.resources.size() * 32;
        for (const auto& r : upload.resources) {
            if (r.kind != nfsu::UploadResourceKind::texture) continue;
            const auto pitch = (r.texture.row_bytes + 31) & ~std::uint64_t{31};
            staging += r.bytes;
            if (pitch != r.texture.row_bytes) staging += pitch * r.texture.rows() + 31;
        }
        measure.gpu_peak = aligned(gpu.buffer_extent) + aligned(gpu.image_extent) +
                           aligned(staging) + 64 * nfsu::kMiB;
        std::vector<nfsu::TrackStreamSectionCost> section_costs;
        if (!nfsu::track_stream_section_costs(out.load, out.scene, out.source_owner,
                                             section_costs, error)) return false;
        for (const auto& c : section_costs) {
            measure.largest_section = std::max(measure.largest_section,
                c.vertices * nfsu::kTrackVertexStride + c.triangles * nfsu::kFaceStride +
                c.instances * sizeof(nfsu::PreparedTrackInstance) + c.retained_texture_bytes);
        }
        const auto row = [&](const char* name, std::uint64_t value, std::uint64_t ceiling) {
            measure.rows.push_back({nfsu::TrackWholePlatform::switch_plan, name, value, ceiling, value <= ceiling});
        };
        row("streaming.retained", measure.resident_bytes, nfsu::kResidentRegionsPlaceholder * nfsu::kStreamingRegionBudgetPlaceholder);
        row("streaming.prepare_peak", measure.cpu_upload_peak, nfsu::kResidentRegionsPlaceholder * nfsu::kStreamingRegionBudgetPlaceholder);
        row("streaming.largest_section", measure.largest_section, nfsu::kStreamingRegionBudgetPlaceholder);
        row("gpu.peak_bound", measure.gpu_peak, nfsu::kGpuBudgetPlaceholder);
        measure.over = 0;
        for (const auto& r : measure.rows) measure.over += r.within ? 0 : 1;
        out.textures = nfsu::RenderTextureSet{};
        return true;
    }
    nfsu::track_whole_budget_rows(out.scene, out.textures.decoded_bytes,
                                  prepared.prepare_resident_bytes, gpu,
                                  measure.rows);
    measure.over = 0;
    for (const nfsu::TrackWholeBudgetRow& row : measure.rows) {
        measure.over += row.within ? 0U : 1U;
    }
    return true;
}

void print_stream_path_rows(const std::vector<nfsu::TrackWholeBudgetRow>& rows) {
    for (const nfsu::TrackWholeBudgetRow& row : rows) {
        std::cout << "  budget: " << nfsu::track_whole_platform_name(row.platform)
                  << ' ' << row.name << ' ' << row.value << " of " << row.ceiling;
        if (row.within) {
            std::cout << " within\n";
        } else {
            std::cout << " OVER by " << (row.value - row.ceiling) << '\n';
        }
    }
}

fs::path stream_path_frame_name(const fs::path& prefix, std::size_t sample) {
    std::ostringstream name;
    name << '-' << std::setw(6) << std::setfill('0') << sample << ".tga";
    fs::path out = prefix;
    out += name.str();
    return out;
}

int run_track_stream_path(const fs::path& stream_path,
                          const fs::path& trackb_path,
                          std::uint32_t track,
                          const TrackStreamPathArgs& args) {
    if (args.gpu_textures && args.has_frames) {
        std::cerr << "error: --gpu-textures is upload-only; CPU frames require RGBA8\n";
        return 1;
    }
    std::vector<fs::path> inputs;
    inputs.push_back(stream_path);
    inputs.push_back(trackb_path);
    if (args.has_path_file) {
        inputs.push_back(args.path_file);
    }
    if (args.has_pack) {
        inputs.push_back(args.pack);
    }
    inputs.insert(inputs.end(), args.textures.begin(), args.textures.end());

    // The path, before any game file is read: its sampling decides which
    // frames the run would write, and every one of them is checked here.
    std::string path_text;
    std::string path_name;
    if (args.has_hand_path) {
        path_text = nfsu::kTrackResidencyHandPath1003;
        path_name = "hand-1003 (HAND-GIVEN, not the game's route)";
    } else {
        std::error_code code;
        const std::uintmax_t size = fs::file_size(args.path_file, code);
        if (code) {
            std::cerr << "error: cannot size the path file "
                      << args.path_file.filename().string() << '\n';
            return 2;
        }
        if (size > kStreamPathMaxPathBytes) {
            std::cerr << "error: the path file holds " << size
                      << " bytes, more than " << kStreamPathMaxPathBytes << '\n';
            return 2;
        }
        std::ifstream in(args.path_file, std::ios::binary);
        path_text.assign(static_cast<std::size_t>(size), '\0');
        if (!in || !in.read(&path_text[0], static_cast<std::streamsize>(size))) {
            std::cerr << "error: cannot read the path file "
                      << args.path_file.filename().string() << '\n';
            return 2;
        }
        path_name = args.path_file.filename().string();
    }
    std::vector<nfsu::TrackResidencyVertex> vertices;
    std::string error;
    if (!nfsu::parse_track_residency_path(path_text, vertices, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::vector<nfsu::TrackResidencySample> samples;
    std::vector<nfsu::TrackResidencyPathPoint> points;
    if (!nfsu::sample_track_residency_path(vertices, args.speed,
                                           nfsu::kTrackResidencyTicksPerSecond,
                                           samples, points, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::vector<std::size_t> frame_samples;
    if (args.has_frames) {
        for (std::size_t s = 0; s < samples.size();
             s += static_cast<std::size_t>(args.frame_every)) {
            frame_samples.push_back(s);
        }
        for (const std::size_t s : frame_samples) {
            const fs::path frame = stream_path_frame_name(args.output, s);
            fs::path partial = frame;
            partial += ".partial";
            for (const fs::path& source : inputs) {
                if (nfsu::same_file_path(source, frame) ||
                    nfsu::same_file_path(source, partial)) {
                    std::cerr << "error: frame " << frame.filename().string()
                              << " would name an input file\n";
                    return 1;
                }
            }
            std::error_code code;
            if (fs::exists(frame, code)) {
                std::cerr << "error: frame " << frame.string()
                          << " already exists; this command never replaces a"
                             " file\n";
                return 1;
            }
        }
    }

    std::vector<unsigned char> image;
    const nfsu::TrackZoneReport zones = nfsu::load_track_zones(trackb_path, image);
    std::cout << "stream: " << stream_path.filename().string() << '\n';
    std::cout << "trackb: " << trackb_path.filename().string() << " packed "
              << zones.cost.packed_bytes << " image " << image.size() << '\n';
    std::cout << "zones: valid " << (zones.valid ? "yes" : "no")
              << " polygons " << zones.polygons.size() << " zones "
              << zones.zones.size() << " section lists "
              << zones.section_lists.size() << '\n';
    print_track_whole_issues("zone", zones.issues);
    const nfsu::TrackStreamTable table =
        nfsu::parse_track_stream_table(image.data(), image.size());
    print_track_whole_issues("table", table.issues);
    if (!table.valid || !zones.valid) {
        std::cout << "table: " << (table.valid ? "valid" : "not valid")
                  << "; nothing is streamed\n";
        return 3;
    }
    std::uint64_t flagged = 0;
    for (const nfsu::TrackStreamRecord& record : table.records) {
        flagged += record.track_flag != 0 ? 1U : 0U;
    }
    const std::uint64_t pool = nfsu::track_stream_pool_bytes(table);
    std::cout << "table: records " << table.records.size() << " track-flagged "
              << flagged << " streaming pool " << pool << '\n';
    const nfsu::TrackZoneSectionList* route = nullptr;
    for (const nfsu::TrackZoneSectionList& list : zones.section_lists) {
        if (list.chunk_id == nfsu::kTrackZoneTrackListId && list.key == track) {
            route = &list;
            break;
        }
    }
    if (route != nullptr) {
        std::cout << "route: 0x00034153 record " << route->index << " keyed on "
                  << track << " names" << track_whole_list(*route) << '\n';
    } else {
        std::cout << "route: no 0x00034153 record is keyed on " << track
                  << "; a look-ahead point accepts any zone\n";
    }
    std::cout << "path: " << path_name << " vertices " << vertices.size()
              << " length " << format_double(nfsu::track_residency_path_length(vertices))
              << '\n';
    std::cout << "path: speed " << format_double(args.speed) << " steps per second "
              << nfsu::kTrackResidencyTicksPerSecond << " samples " << samples.size()
              << '\n';

    nfsu::TrackStreamFiles files;
    files.stream = stream_path;
    files.has_pack = args.has_pack;
    files.pack = args.pack;
    StreamPathTextureFiles texture_files;
    texture_files.bytes.resize(args.textures.size());
    texture_files.sets.resize(args.textures.size());
    for (std::size_t i = 0; i < args.textures.size(); ++i) {
        nfsu::TextureLimits limits;
        limits.max_file_size =
            std::min<std::uint64_t>(limits.max_file_size, kMaxTrackTextureInputBytes);
        texture_files.sets[i] = nfsu::load_texture_pack_set(
            args.textures[i], texture_files.bytes[i], limits);
        std::cout << "texture file " << i << ": "
                  << args.textures[i].filename().string() << " size "
                  << texture_files.sets[i].file_size << " packs "
                  << texture_files.sets[i].packs.size() << " complete "
                  << (texture_files.sets[i].complete ? 1 : 0) << '\n';
        for (const nfsu::TextureIssue& issue : texture_files.sets[i].issues) {
            std::cerr << "texture file " << i << " issue at " << issue.offset
                      << ": " << issue.message << '\n';
        }
    }

    // The currency the streamer decides in.
    std::vector<nfsu::TrackResidencyCost> costs;
    std::uint64_t outside = 0;
    bool reports_ok = zones.ok() && table.ok();
    std::vector<std::uint8_t> reported(table.records.size() + 1, 0);
    if (args.cost == StreamPathCost::table) {
        costs = nfsu::track_residency_table_costs(table);
        std::cout << "cost: table (place the byte count read, hold the bytes"
                     " kept)\n";
    } else {
        // Every section any zone lists, with the permanent set first: the
        // most the streamer can request, whatever the path.
        nfsu::TrackStreamSelection candidate;
        if (!nfsu::select_track_stream_sections(table, zones, track,
                                                nfsu::TrackStreamSet::zones,
                                                candidate, error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        auto scene = std::make_unique<StreamPathScene>();
        StreamPathMeasure measure;
        if (!build_stream_path_scene(table, zones, track, candidate.records, files,
                                     texture_files, args.gpu_textures, reported, *scene, measure,
                                     error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        reports_ok = reports_ok && measure.reports_ok;
        std::vector<nfsu::TrackStreamSectionCost> section_costs;
        if (!nfsu::track_stream_section_costs(scene->load, scene->scene,
                                              scene->source_owner, section_costs,
                                              error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        // Each section's share is an upper bound on what it can add to ANY
        // resident set (review comment 3630, F4): its valid meshes' vertex
        // and index bytes, every one of its scenery instances whether or not
        // it places, and the RGBA8 bytes of every texture its own packs
        // decode that the candidate geometry wants - decoded from that
        // section alone, so a texture another pack shadows in the candidate
        // scene is still charged to every pack that holds it. The texture
        // files are charged the same way, once, off the top of the budget.
        // A resident set's measured bytes are then at most the sum of its
        // shares plus the files', which the step loop below checks.
        const std::vector<std::uint32_t> wanted =
            nfsu::track_region_texture_hashes(scene->load.geometry);
        std::vector<nfsu::RenderTextureSource> section_sources;
        std::vector<std::size_t> section_owner;
        nfsu::track_stream_texture_sources(scene->load, &wanted, section_sources,
                                           section_owner);
        std::vector<std::uint64_t> section_texture(scene->load.sections.size(), 0);
        nfsu::RenderTextureLimits texture_limits;
        texture_limits.max_total_bytes = kTrackWholeTextureBytes;
        texture_limits.gpu_blocks = args.gpu_textures;
        for (std::size_t k = 0; k < section_sources.size(); ++k) {
            nfsu::RenderTextureSet alone;
            if (!nfsu::decode_render_texture_sources({section_sources[k]},
                                                     texture_limits, alone, error)) {
                std::cerr << "error: " << error << '\n';
                return 2;
            }
            section_texture[section_owner[k]] += alone.decoded_bytes;
        }
        for (std::size_t i = 0; i < texture_files.bytes.size(); ++i) {
            nfsu::RenderTextureSource source;
            source.bytes = &texture_files.bytes[i];
            for (const nfsu::TexturePack& pack : texture_files.sets[i].packs) {
                source.packs.push_back(&pack);
            }
            source.complete = texture_files.sets[i].complete;
            source.wanted = &wanted;
            nfsu::RenderTextureSet alone;
            if (!nfsu::decode_render_texture_sources({source}, texture_limits, alone,
                                                     error)) {
                std::cerr << "error: " << error << '\n';
                return 2;
            }
            outside += alone.decoded_bytes;
        }
        costs.assign(table.records.size(), nfsu::TrackResidencyCost{});
        std::uint64_t shares = 0;
        for (std::size_t s = 0; s < scene->load.sections.size(); ++s) {
            const nfsu::TrackStreamSectionCost& c = section_costs[s];
            const std::uint64_t share =
                c.vertices * nfsu::kTrackVertexStride +
                c.triangles * nfsu::kFaceStride + section_texture[s] +
                c.instances *
                    static_cast<std::uint64_t>(sizeof(nfsu::PreparedTrackInstance));
            costs[scene->load.sections[s].record].place_bytes = share;
            costs[scene->load.sections[s].record].held_bytes = share;
            shares += share;
        }
        std::cout << "cost: prepared (for each of the " << candidate.records.size()
                  << " sections any zone lists, an upper bound on what it adds to"
                     " any resident set: its vertex and index bytes, all its"
                     " instances, and every wanted texture its packs decode)\n";
        std::cout << "cost: shares " << shares << " texture files " << outside
                  << " candidate scene measured " << measure.resident_bytes << '\n';
    }

    const nfsu::MemoryBudget switch_budget = nfsu::switch_memory_budget();
    const std::uint64_t switch_arena = switch_budget.capacity[static_cast<std::size_t>(
        nfsu::MemoryCategory::streaming)];
    std::uint64_t budget = 0;
    std::string budget_name;
    StreamPathBudget kind = args.budget;
    if (!args.has_budget) {
        kind = args.cost == StreamPathCost::table ? StreamPathBudget::pool
                                                  : StreamPathBudget::switch_arena;
    }
    switch (kind) {
        case StreamPathBudget::pool:
            budget = pool;
            budget_name = "pool";
            break;
        case StreamPathBudget::switch_region:
            budget = nfsu::kStreamingRegionBudgetPlaceholder;
            budget_name = "switch-region";
            break;
        case StreamPathBudget::switch_arena:
            budget = switch_arena;
            budget_name = "switch-arena";
            break;
        case StreamPathBudget::bytes:
            budget = args.budget_bytes;
            budget_name = "given";
            break;
    }
    // The texture files are resident whatever the streamer does, so they
    // come off the budget before it starts.
    const std::uint64_t streamer_budget = budget > outside ? budget - outside : 0;
    std::cout << "budget: " << budget_name << ' ' << budget << " streamer "
              << streamer_budget << '\n';
    std::cout << "policy: " << nfsu::track_residency_policy_name(args.policy) << '\n';

    nfsu::TrackResidencyOptions options;
    options.policy = args.policy;
    options.budget_bytes = streamer_budget;
    nfsu::TrackResidencyRun run;
    if (!nfsu::simulate_track_residency(table, zones, track, costs, samples,
                                        options, run, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }
    std::cout << "start: steps " << run.start_steps << " loaded "
              << (run.start_loaded ? "yes" : "no") << '\n';

    // Replay the loads and releases, measuring every distinct resident set.
    std::vector<std::size_t> resident;
    std::map<std::vector<std::size_t>, std::size_t> set_ids;
    std::vector<StreamPathMeasure> sets;
    std::unique_ptr<StreamPathScene> current;
    std::size_t current_set = static_cast<std::size_t>(-1);
    std::size_t set = static_cast<std::size_t>(-1);
    std::size_t peak_step = 0;
    std::size_t peak_set = 0;
    std::uint64_t peak_bytes = 0;
    nfsu::Sha256 cost_hash;
    std::size_t next_frame = 0;
    std::uint64_t frames_written = 0;
    // In the prepared currency the budget and the measured bytes are the same
    // bytes, so every step's resident set is checked against it; the shares
    // above make an excess impossible, and this is what would report one.
    const bool measured_currency = args.cost == StreamPathCost::prepared;
    std::uint64_t measured_over_steps = 0;
    std::uint64_t measured_largest = 0;
    for (const nfsu::TrackResidencyStep& step : run.steps) {
        for (const std::size_t r : step.releases) {
            resident.erase(std::remove(resident.begin(), resident.end(), r),
                           resident.end());
        }
        for (const std::size_t r : step.loads) {
            resident.push_back(r);
        }
        const bool changed = !step.loads.empty() || !step.releases.empty() ||
                             set == static_cast<std::size_t>(-1);
        if (changed) {
            std::vector<std::size_t> key = resident;
            std::sort(key.begin(), key.end());
            const auto found = set_ids.find(key);
            if (found != set_ids.end()) {
                set = found->second;
            } else {
                set = sets.size();
                set_ids.emplace(key, set);
                StreamPathMeasure measure;
                measure.reports_ok = true;
                if (!resident.empty()) {
                    auto scene = std::make_unique<StreamPathScene>();
                    if (!build_stream_path_scene(table, zones, track, resident,
                                                 files, texture_files, args.gpu_textures, reported,
                                                 *scene, measure, error)) {
                        std::cerr << "error: " << error << '\n';
                        return 2;
                    }
                    current = std::move(scene);
                    current_set = set;
                }
                measure.first_step = step.index;
                reports_ok = reports_ok && measure.reports_ok;
                sets.push_back(measure);
                std::cout << "set " << set << " at step " << step.index
                          << " sections " << measure.records.size() << " read "
                          << measure.bytes_read << " triangles "
                          << measure.triangles << " placed " << measure.placed
                          << " texture bytes " << measure.texture_bytes
                          << " resident bytes " << measure.resident_bytes
                          << " rows over " << measure.over << '\n';
                if (args.list != StreamPathList::none) {
                    print_stream_path_rows(measure.rows);
                }
            }
        }
        const StreamPathMeasure& now = sets[set];
        if (args.gpu_textures) {
            std::cout << "gpu-budget step " << step.index << " cpu-streaming " << now.resident_bytes
                      << " cpu-prepare-peak " << now.cpu_upload_peak << " gpu-peak-bound " << now.gpu_peak
                      << " largest-section " << now.largest_section << " within " << (now.over == 0) << '\n';
            if (now.over) reports_ok = false;
        }
        if (now.resident_bytes > peak_bytes || step.index == 0) {
            peak_bytes = now.resident_bytes;
            peak_step = step.index;
            peak_set = set;
        }
        const std::string cost_line = "step " + std::to_string(step.index) +
                                      " set " + std::to_string(set) +
                                      " resident " +
                                      std::to_string(now.resident_bytes) + "\n";
        cost_hash.update(cost_line.data(), cost_line.size());

        const bool measured_over =
            measured_currency && now.resident_bytes > budget;
        if (measured_currency && now.resident_bytes > measured_largest) {
            measured_largest = now.resident_bytes;
        }
        measured_over_steps += measured_over ? 1U : 0U;
        const bool frame_step = step.on_path && next_frame < frame_samples.size() &&
                                frame_samples[next_frame] == step.sample;
        const bool event = step.rebuilt || !step.loads.empty() ||
                           !step.releases.empty() || step.over_budget ||
                           step.release_capped || step.unplaced_bytes != 0 ||
                           frame_step || measured_over;
        if (args.list == StreamPathList::all ||
            (args.list == StreamPathList::events && event)) {
            std::cout << nfsu::track_residency_step_line(table, step) << " set "
                      << set << " resident bytes " << now.resident_bytes;
            if (measured_over) {
                std::cout << " MEASURED OVER the budget by "
                          << (now.resident_bytes - budget);
            }
            std::cout << '\n';
        }
        if (!frame_step) {
            continue;
        }
        ++next_frame;
        if (resident.empty()) {
            std::cout << "frame: sample " << step.sample << " step " << step.index
                      << " set " << set << " nothing is resident; no frame\n";
            continue;
        }
        if (current_set != set) {
            auto scene = std::make_unique<StreamPathScene>();
            StreamPathMeasure again;
            if (!build_stream_path_scene(table, zones, track, resident, files,
                                         texture_files, args.gpu_textures, reported, *scene, again,
                                         error)) {
                std::cerr << "error: " << error << '\n';
                return 2;
            }
            current = std::move(scene);
            current_set = set;
        }
        const nfsu::TrackResidencyPathPoint& point = points[step.sample];
        nfsu::RenderOptions options_render;
        options_render.width = args.width;
        options_render.height = args.height;
        nfsu::OrbitCamera& camera = options_render.camera;
        // World x is slot 0, world y slot 2 and world z minus slot 1.
        camera.target = nfsu::Vec3{
            point.slot[0] + kStreamPathCameraLead * point.direction[0],
            point.slot[2] + kStreamPathCameraHeight,
            -(point.slot[1] + kStreamPathCameraLead * point.direction[1])};
        // The eye lies from the target along -(direction of travel), which
        // in the world frame is (-d0, +d1) on the ground plane.
        camera.yaw_deg = std::atan2(point.direction[1], -point.direction[0]) *
                         (180.0 / 3.14159265358979323846);
        camera.pitch_deg = kStreamPathCameraPitch;
        camera.distance = kStreamPathCameraLead;
        camera.fov_y_deg = kStreamPathCameraFov;
        camera.near_plane = kStreamPathCameraNear;
        camera.far_plane = kStreamPathCameraFar;
        if (!nfsu::validate_camera(camera, error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        nfsu::TrackRenderStats track_stats;
        const nfsu::RenderLimits render_limits;
        if (!nfsu::render_track_draw_list(current->scene, current->list,
                                          options_render, render_limits, frame,
                                          stats, track_stats, error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        const fs::path frame_path = stream_path_frame_name(args.output, step.sample);
        nfsu::FrameWriteResult write;
        if (!nfsu::write_frame_tga(frame, frame_path, inputs, write)) {
            std::cerr << "error: " << write.error << '\n';
            for (const fs::path& left : write.not_removed) {
                std::cerr << "error: left behind " << left.string() << '\n';
            }
            return 2;
        }
        ++frames_written;
        std::cout << "frame: sample " << step.sample << " step " << step.index
                  << " set " << set << " at " << format_double(point.slot[0])
                  << ',' << format_double(point.slot[1]) << ','
                  << format_double(point.slot[2]) << " yaw "
                  << format_double(camera.yaw_deg) << " entries "
                  << track_stats.entries << " triangles rasterized "
                  << stats.triangles_rasterized << " pixels drawn "
                  << stats.pixels_drawn << " fallback " << stats.pixels_fallback
                  << " wrote " << frame_path.filename().string() << '\n';
    }

    std::cout << "summary: steps " << run.steps.size() << " start "
              << run.start_steps << " path " << samples.size() << " rebuilds "
              << run.rebuilds << " loads " << run.loads << " releases "
              << run.releases << " over-budget steps " << run.over_budget_steps
              << " unplaced steps " << run.unplaced_steps << " capped steps "
              << run.release_capped_steps << '\n';
    std::cout << "summary: samples above the route-query speed "
              << run.route_query_samples
              << " (retail asks the undecoded path structure first there)\n";
    std::size_t resident_at_end = 0;
    for (const std::uint8_t r : run.resident) {
        resident_at_end += r;
    }
    std::cout << "summary: resident at the end " << resident_at_end
              << " distinct sets " << sets.size() << " frames " << frames_written
              << '\n';
    std::cout << "peak: held " << run.peak_held_bytes << " at step "
              << run.peak_step << " of streamer budget " << streamer_budget << '\n';
    std::cout << "peak: resident bytes " << peak_bytes << " at step " << peak_step
              << " set " << peak_set << " sections "
              << sets[peak_set].records.size() << '\n';
    if (measured_currency) {
        std::cout << "measured: every step against the budget " << budget
                  << ": largest " << measured_largest << ", steps over "
                  << measured_over_steps << '\n';
    } else {
        std::cout << "measured: not compared with the budget, which is in the"
                     " table's bytes; the rows above compare it with #52\n";
    }
    print_stream_path_rows(sets[peak_set].rows);
    std::cout << "sequence: sha256 "
              << nfsu::sha256_hex(nfsu::track_residency_sequence_sha256(table, run))
              << " steps " << run.steps.size() << '\n';
    std::cout << "costs: sha256 " << nfsu::sha256_hex(cost_hash.finish())
              << " steps " << run.steps.size() << '\n';
    return reports_ok ? 0 : 3;
}

void print_usage();

// The single registration point for an nfsu_probe command. One entry carries
// the name main() dispatches on, the rest of the usage line, and the handler
// that runs the command.
//
// print_usage() composes the usage line out of `name`, so the listed name and
// the dispatched name are the same bytes rather than two spellings a comment
// asks a reader to keep in agreement: an entry cannot be dispatched without
// being listed, and an entry that leaves `args` and `tail` empty is still
// listed under its own name.
//
// Adding a command is an entry here plus the matching `build/nfsu_probe
// <name>` line in README.md. tests/usage_commands.cmake enforces both from
// outside: it reads the table out of this file, the listing out of the built
// program, and the inventory out of the README, and requires all three sets
// to be equal.
struct Command {
    std::string_view name;
    // What follows the name on the usage line, such as " <game-root>". No
    // leading "  nfsu_probe ", no trailing newline: print_usage() supplies
    // both. Empty is allowed and still produces a listed line.
    std::string_view args;
    // Continuation lines printed under the usage line, each already indented.
    // Empty for a command that fits on one line. print_usage() terminates a
    // last line that does not terminate itself, so a missing trailing newline
    // here cannot run the next entry's usage line onto this text. A line here
    // is documentation only; the usage check reads the command inventory from
    // the composed usage lines, never from this text.
    std::string_view tail;
    int (*run)(int argc, char** argv);
};

// This order is the order print_usage() prints.
const Command kCommands[] = {
    {"summary", " <game-root>", "",
     [](int, char** argv) { return run_summary(nfsu::argument_path(argv[2])); }},
    {"asset-check", " <root> [--set <first-lap|stock-car|event-1>]",
     "      (the retail files a set needs under root: each found by name\n"
     "       without regard to ASCII case, then checked by size and SHA-256;\n"
     "       reads nothing else and writes nothing; first-lap is the default)\n",
     [](int argc, char** argv) {
         if (argc != 3 && argc != 5) {
             print_usage();
             return 1;
         }
         std::string set = "first-lap";
         if (argc == 5) {
             const std::string option(argv[3]);
             if (option != "--set") {
                 std::cerr << "error: unknown option " << option << '\n';
                 return 1;
             }
             set = argv[4];
         }
         nfsu::AssetManifest manifest;
         if (!nfsu::retail_asset_manifest(set, manifest)) {
             std::cerr << "error: --set must be one of";
             for (const std::string& name : nfsu::retail_asset_manifest_names()) {
                 std::cerr << ' ' << name;
             }
             std::cerr << '\n';
             return 1;
         }
         return run_asset_check(nfsu::argument_path(argv[2]), manifest);
     }},
    {"chunks", " <file> [display-limit]", "",
     [](int argc, char** argv) {
         std::size_t limit = 128;
         if (argc >= 4) {
             try {
                 limit = static_cast<std::size_t>(std::stoull(argv[3]));
             } catch (const std::exception&) {
                 std::cerr << "error: display-limit must be an integer\n";
                 return 1;
             }
         }
         return run_chunks(nfsu::argument_path(argv[2]), limit);
     }},
    {"meshes", " <GEOMETRY.BIN>", "",
     [](int, char** argv) { return run_meshes(nfsu::argument_path(argv[2])); }},
    {"obj", " <GEOMETRY.BIN> <output.obj>", "",
     [](int argc, char** argv) {
         if (argc < 4) {
             print_usage();
             return 1;
         }
         return run_obj(nfsu::argument_path(argv[2]), nfsu::argument_path(argv[3]));
     }},
    {"textures", " <TEXTURES.BIN>", "",
     [](int, char** argv) { return run_textures(nfsu::argument_path(argv[2])); }},
    {"pack-set",
     " <file>   (metadata of every texture pack, e.g. in a BUN)", "",
     [](int, char** argv) { return run_pack_set(nfsu::argument_path(argv[2])); }},
    {"export-textures", " <TEXTURES.BIN> <output-dir> [tga|dds]", "",
     [](int argc, char** argv) {
         if (argc < 4) {
             print_usage();
             return 1;
         }
         return run_export_textures(nfsu::argument_path(argv[2]),
                                    nfsu::argument_path(argv[3]),
                                    argc >= 5 ? std::string(argv[4]) : "tga");
     }},
    {"texture-bindings", " <GEOMETRY.BIN> <TEXTURES.BIN>", "",
     [](int argc, char** argv) {
         if (argc < 4) {
             print_usage();
             return 1;
         }
         return run_texture_bindings(nfsu::argument_path(argv[2]),
                                     nfsu::argument_path(argv[3]));
     }},
    {"car-parts", " <GLOBALB.BUN> [car-type]", "",
     [](int argc, char** argv) {
         if (argc > 4) {
             print_usage();
             return 1;
         }
         return run_car_parts(nfsu::argument_path(argv[2]),
                              argc == 4 ? std::string(argv[3]) : "");
     }},
    {"stock-parts", " <GLOBALB.BUN> <car-type> [GEOMETRY.BIN ...]", "",
     [](int argc, char** argv) {
         if (argc < 4) {
             print_usage();
             return 1;
         }
         std::vector<fs::path> geometry;
         for (int i = 4; i < argc; ++i) {
             geometry.push_back(nfsu::argument_path(argv[i]));
         }
         return run_stock_parts(nfsu::argument_path(argv[2]), std::string(argv[3]),
                                geometry);
     }},
    {"stock-scene", " <GLOBALB.BUN> <car-type> <A|B|C|D>",
     "      [--front-end] [--place-wheels] [GEOMETRY.BIN ...]\n",
     [](int argc, char** argv) {
         if (argc < 5) {
             print_usage();
             return 1;
         }
         const std::string lod_name(argv[4]);
         if (lod_name.size() != 1 || lod_name[0] < 'A' ||
             lod_name[0] >= 'A' + static_cast<char>(nfsu::kCarPartLodCount)) {
             std::cerr << "error: the LOD must be one of A, B, C or D\n";
             return 1;
         }
         const std::size_t lod = static_cast<std::size_t>(lod_name[0] - 'A');
         bool front_end = false;
         bool place_wheels = false;
         std::vector<fs::path> geometry;
         for (int i = 5; i < argc; ++i) {
             const std::string argument(argv[i]);
             if (argument == "--front-end") {
                 front_end = true;
             } else if (argument == "--place-wheels") {
                 place_wheels = true;
             } else if (argument.size() > 2 && argument.compare(0, 2, "--") == 0) {
                 std::cerr << "error: unknown option " << argument << '\n';
                 return 1;
             } else {
                 geometry.push_back(nfsu::argument_path(argv[i]));
             }
         }
         return run_stock_scene(nfsu::argument_path(argv[2]), std::string(argv[3]),
                                lod, front_end, place_wheels, geometry);
     }},
    {"track-region", " <STREAM.BUN> <offset> <size> [TEXTURE.BIN ...]", "",
     [](int argc, char** argv) {
         if (argc < 5) {
             print_usage();
             return 1;
         }
         std::uint64_t offset = 0;
         std::uint64_t size = 0;
         if (!parse_u64(std::string(argv[3]), offset)) {
             std::cerr << "error: offset must be a decimal or 0x integer\n";
             return 1;
         }
         if (!parse_u64(std::string(argv[4]), size)) {
             std::cerr << "error: size must be a decimal or 0x integer\n";
             return 1;
         }
         std::vector<fs::path> textures;
         for (int i = 5; i < argc; ++i) {
             textures.push_back(nfsu::argument_path(argv[i]));
         }
         return run_track_region(nfsu::argument_path(argv[2]), offset, size, textures);
     }},
    {"track-objects", " <STREAM.BUN> <offset> <size>",
     "      [<geometry-offset> <geometry-size>]\n",
     [](int argc, char** argv) {
         if (argc != 5 && argc != 7) {
             print_usage();
             return 1;
         }
         std::uint64_t offset = 0;
         std::uint64_t size = 0;
         if (!parse_u64(std::string(argv[3]), offset)) {
             std::cerr << "error: offset must be a decimal or 0x integer\n";
             return 1;
         }
         if (!parse_u64(std::string(argv[4]), size)) {
             std::cerr << "error: size must be a decimal or 0x integer\n";
             return 1;
         }
         std::uint64_t geometry_offset = 0;
         std::uint64_t geometry_size = 0;
         const bool has_geometry = argc == 7;
         if (has_geometry) {
             if (!parse_u64(std::string(argv[5]), geometry_offset)) {
                 std::cerr << "error: geometry-offset must be a decimal or 0x integer\n";
                 return 1;
             }
             if (!parse_u64(std::string(argv[6]), geometry_size)) {
                 std::cerr << "error: geometry-size must be a decimal or 0x integer\n";
                 return 1;
             }
         }
         return run_track_objects(nfsu::argument_path(argv[2]), offset, size,
                                  has_geometry, geometry_offset,
                                  geometry_size);
     }},
    {"track-visibility",
     " <STREAM.BUN> <offset> <size>",
     "      <eye-x> <eye-y> <eye-z> <target-x> <target-y> <target-z>\n"
     "      [--fov <radians>] [--aspect <ratio>] [--near <n>] [--far <f>]\n"
     "      [--policy <retail-tree|draw-everything|flat-instance-test>]\n"
     "      [--residency <every|no-zone|zone>] [--zone <section-number>]...\n"
     "      [--zones <TRACKB####.lzc> --zone-point <slot0> <slot1>]\n"
     "      [--mask <0xNN>] [--screen-scale <s>] [--list <count>]\n"
     "      (eye and target are in the world frame of docs/FORMATS.md:286;\n"
     "       --zones replaces the residency with the zone that holds the\n"
     "       point, which is stated in the raw slot order instead)\n",
     [](int argc, char** argv) {
         if (argc < 11) {
             print_usage();
             return 1;
         }
         std::uint64_t offset = 0;
         std::uint64_t size = 0;
         if (!parse_u64(std::string(argv[3]), offset)) {
             std::cerr << "error: offset must be a decimal or 0x integer\n";
             return 1;
         }
         if (!parse_u64(std::string(argv[4]), size)) {
             std::cerr << "error: size must be a decimal or 0x integer\n";
             return 1;
         }
         TrackVisibilityArgs args;
         for (int i = 0; i < 3; ++i) {
             if (!parse_f32(std::string(argv[5 + i]), args.camera.eye[i])) {
                 std::cerr << "error: the eye must be three finite floats\n";
                 return 1;
             }
             if (!parse_f32(std::string(argv[8 + i]), args.camera.target[i])) {
                 std::cerr << "error: the target must be three finite floats\n";
                 return 1;
             }
         }
         for (int i = 11; i < argc; ++i) {
             const std::string flag = argv[i];
             const bool has_value = i + 1 < argc;
             const std::string value = has_value ? argv[i + 1] : std::string();
             const auto take_float = [&](float& out) {
                 if (!has_value || !parse_f32(value, out)) {
                     std::cerr << "error: " << flag
                               << " needs a finite float\n";
                     return false;
                 }
                 ++i;
                 return true;
             };
             if (flag == "--fov") {
                 if (!take_float(args.camera.vertical_fov)) {
                     return 1;
                 }
             } else if (flag == "--aspect") {
                 if (!take_float(args.camera.aspect)) {
                     return 1;
                 }
             } else if (flag == "--near") {
                 if (!take_float(args.camera.near_plane)) {
                     return 1;
                 }
             } else if (flag == "--far") {
                 if (!take_float(args.camera.far_plane)) {
                     return 1;
                 }
             } else if (flag == "--screen-scale") {
                 if (!take_float(args.options.screen_scale)) {
                     return 1;
                 }
                 args.options.use_screen_size = true;
                 nfsu::track_raw_from_world(args.camera.eye,
                                            args.options.screen_eye);
                 float forward_world[3] = {
                     args.camera.target[0] - args.camera.eye[0],
                     args.camera.target[1] - args.camera.eye[1],
                     args.camera.target[2] - args.camera.eye[2]};
                 nfsu::track_raw_from_world(forward_world,
                                            args.options.screen_forward);
             } else if (flag == "--policy") {
                 if (value == "retail-tree") {
                     args.options.policy = nfsu::TrackCullPolicy::retail_tree;
                 } else if (value == "draw-everything") {
                     args.options.policy =
                         nfsu::TrackCullPolicy::draw_everything;
                 } else if (value == "flat-instance-test") {
                     args.options.policy =
                         nfsu::TrackCullPolicy::flat_instance_test;
                 } else {
                     std::cerr << "error: --policy must be retail-tree, "
                                  "draw-everything or flat-instance-test\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--residency") {
                 if (value == "every") {
                     args.options.residency.rule =
                         nfsu::TrackResidencyRule::every_section;
                 } else if (value == "no-zone") {
                     args.options.residency.rule =
                         nfsu::TrackResidencyRule::no_zone;
                 } else if (value == "zone") {
                     args.options.residency.rule =
                         nfsu::TrackResidencyRule::zone_list;
                 } else {
                     std::cerr << "error: --residency must be every, no-zone "
                                  "or zone\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--zone") {
                 std::uint64_t number = 0;
                 if (!has_value || !parse_u64(value, number) ||
                     number > 0xFFFFU) {
                     std::cerr << "error: --zone needs a section number\n";
                     return 1;
                 }
                 args.options.residency.zone_sections.push_back(
                     static_cast<std::uint32_t>(number));
                 ++i;
             } else if (flag == "--zones") {
                 if (!has_value) {
                     std::cerr << "error: --zones needs a TRACKB####.lzc "
                                  "path\n";
                     return 1;
                 }
                 args.zone_file = nfsu::argument_path(argv[i + 1]);
                 args.has_zone_file = true;
                 ++i;
             } else if (flag == "--zone-point") {
                 if (i + 2 >= argc ||
                     !parse_f32(std::string(argv[i + 1]), args.zone_point[0]) ||
                     !parse_f32(std::string(argv[i + 2]), args.zone_point[1])) {
                     std::cerr << "error: --zone-point needs two finite "
                                  "floats\n";
                     return 1;
                 }
                 args.has_zone_point = true;
                 i += 2;
             } else if (flag == "--mask") {
                 std::uint64_t mask = 0;
                 if (!has_value || !parse_u64(value, mask) || mask > 0xFFU) {
                     std::cerr << "error: --mask needs a byte\n";
                     return 1;
                 }
                 args.options.instance_mask =
                     static_cast<std::uint8_t>(mask);
                 ++i;
             } else if (flag == "--list") {
                 std::uint64_t count = 0;
                 if (!has_value || !parse_u64(value, count)) {
                     std::cerr << "error: --list needs a count\n";
                     return 1;
                 }
                 args.list_limit = static_cast<std::size_t>(count);
                 ++i;
             } else {
                 std::cerr << "error: unknown option " << flag << '\n';
                 return 1;
             }
         }
         return run_track_visibility(nfsu::argument_path(argv[2]), offset, size,
                                     args);
     }},
    {"track-zones", " <TRACKB####.lzc> [slot0 slot1]",
     "      (slot0 and slot1 are the two horizontal raw slots, px and pz)\n",
     [](int argc, char** argv) {
         if (argc != 3 && argc != 5) {
             print_usage();
             return 1;
         }
         double slot0 = 0.0;
         double slot1 = 0.0;
         if (argc == 5) {
             try {
                 std::size_t used0 = 0;
                 std::size_t used1 = 0;
                 const std::string first(argv[3]);
                 const std::string second(argv[4]);
                 slot0 = std::stod(first, &used0);
                 slot1 = std::stod(second, &used1);
                 if (used0 != first.size() || used1 != second.size()) {
                     std::cerr << "error: slot0 and slot1 must be numbers\n";
                     return 1;
                 }
             } catch (const std::exception&) {
                 std::cerr << "error: slot0 and slot1 must be numbers\n";
                 return 1;
             }
         }
         return run_track_zones(nfsu::argument_path(argv[2]), argc == 5,
                                slot0, slot1,
                                kTrackZoneListLimit);
     }},
    {"track-scene",
     " <STREAM.BUN> <geometry-offset> <geometry-size>",
     "      <object-offset> <object-size>\n"
     "      [--texture <file>]... [--lod-slot <0..5>]\n"
     "      [--order <visible-instance|material-group>]\n"
     "      [--eye <x> <y> <z>] [--target <x> <y> <z>] [--far <f>]\n"
     "      [--residency <every|no-zone>] [--list <count>]\n"
     "      (eye and target are in the world frame of docs/FORMATS.md:286;\n"
     "       with no camera the fallback policy draws every resident\n"
     "       section, which is not what the game draws)\n",
     [](int argc, char** argv) {
         if (argc < 7) {
             print_usage();
             return 1;
         }
         std::uint64_t numbers[4] = {0, 0, 0, 0};
         static const char* const kNames[4] = {"geometry-offset",
                                               "geometry-size", "object-offset",
                                               "object-size"};
         for (int k = 0; k < 4; ++k) {
             if (!parse_u64(std::string(argv[3 + k]), numbers[k])) {
                 std::cerr << "error: " << kNames[k]
                           << " must be a decimal or 0x integer\n";
                 return 1;
             }
         }
         nfsu::TrackRegionSelector geometry_region;
         geometry_region.offset = numbers[0];
         geometry_region.size = numbers[1];
         nfsu::TrackRegionSelector object_region;
         object_region.offset = numbers[2];
         object_region.size = numbers[3];

         TrackSceneArgs args;
         args.cull.policy = nfsu::TrackCullPolicy::draw_everything;
         bool has_eye = false;
         bool has_target = false;
         for (int i = 7; i < argc; ++i) {
             const std::string flag = argv[i];
             const bool has_value = i + 1 < argc;
             const std::string value = has_value ? argv[i + 1] : std::string();
             const auto take_triple = [&](float out[3]) {
                 if (i + 3 >= argc) {
                     std::cerr << "error: " << flag
                               << " needs three finite floats\n";
                     return false;
                 }
                 for (int k = 0; k < 3; ++k) {
                     if (!parse_f32(std::string(argv[i + 1 + k]), out[k])) {
                         std::cerr << "error: " << flag
                                   << " needs three finite floats\n";
                         return false;
                     }
                 }
                 i += 3;
                 return true;
             };
             if (flag == "--texture") {
                 if (!has_value) {
                     std::cerr << "error: --texture needs a path\n";
                     return 1;
                 }
                 args.textures.push_back(nfsu::argument_path(argv[i + 1]));
                 ++i;
             } else if (flag == "--lod-slot") {
                 std::uint64_t slot = 0;
                 if (!has_value || !parse_u64(value, slot) ||
                     slot >= nfsu::kSceneryObjectMeshSlots) {
                     std::cerr << "error: --lod-slot needs a slot from 0 to "
                               << (nfsu::kSceneryObjectMeshSlots - 1) << '\n';
                     return 1;
                 }
                 args.scene.lod_slot = static_cast<std::size_t>(slot);
                 ++i;
             } else if (flag == "--order") {
                 if (value == "visible-instance") {
                     args.list.order = nfsu::TrackDrawOrder::visible_instance;
                 } else if (value == "material-group") {
                     args.list.order = nfsu::TrackDrawOrder::material_group;
                 } else {
                     std::cerr << "error: --order must be visible-instance or"
                                  " material-group\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--eye") {
                 if (!take_triple(args.camera.eye)) {
                     return 1;
                 }
                 has_eye = true;
             } else if (flag == "--target") {
                 if (!take_triple(args.camera.target)) {
                     return 1;
                 }
                 has_target = true;
             } else if (flag == "--far") {
                 if (!has_value || !parse_f32(value, args.camera.far_plane)) {
                     std::cerr << "error: --far needs a finite float\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--residency") {
                 if (value == "every") {
                     args.cull.residency.rule =
                         nfsu::TrackResidencyRule::every_section;
                 } else if (value == "no-zone") {
                     args.cull.residency.rule =
                         nfsu::TrackResidencyRule::no_zone;
                 } else {
                     std::cerr << "error: --residency must be every or"
                                  " no-zone\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--list") {
                 std::uint64_t count = 0;
                 if (!has_value || !parse_u64(value, count)) {
                     std::cerr << "error: --list needs a count\n";
                     return 1;
                 }
                 args.list_limit = static_cast<std::size_t>(count);
                 ++i;
             } else {
                 std::cerr << "error: unknown option " << flag << '\n';
                 return 1;
             }
         }
         if (has_eye != has_target) {
             std::cerr << "error: --eye and --target are given together\n";
             return 1;
         }
         if (has_eye) {
             args.has_camera = true;
             args.cull.policy = nfsu::TrackCullPolicy::retail_tree;
         }
         return run_track_scene(nfsu::argument_path(argv[2]), geometry_region,
                                object_region, args);
     }},
    {"track-render",
     " <STREAM.BUN> <geometry-offset> <geometry-size>",
     "      <object-offset> <object-size> --out <FILE>\n"
     "      [--texture <file>]... [--lod-slot <0..5>]\n"
     "      [--order <visible-instance|material-group>]\n"
     "      [--eye <x> <y> <z>] [--target <x> <y> <z>] [--far <f>]\n"
     "      [--residency <every|no-zone>] [--list <count>]\n"
     "      [--width <N>] [--height <N>]\n"
     "      [--view-yaw <deg>] [--view-pitch <deg>] [--view-fov <deg>]\n"
     "      [--view-margin <m>] [--view-distance <d>]\n"
     "      [--view-target <x> <y> <z>] [--view-near <d>] [--view-far <d>]\n"
     "      [--cull <none|back|front>] [--shading <flat|none>] [--flip-v]\n"
     "      [--alpha-cutoff <0..255>] [--vertex-color]\n"
     "      [--background <r> <g> <b>]\n"
     "      (the scene options above --width are track-scene's and mean the\n"
     "       same; --eye, --target and --far select the CULL camera, and\n"
     "       every --view-* option is the RENDER camera. With no --view-*\n"
     "       override the render camera frames the drawn region\n"
     "       automatically. --out is required and is never replaced.)\n",
     [](int argc, char** argv) {
         if (argc < 7) {
             print_usage();
             return 1;
         }
         std::uint64_t numbers[4] = {0, 0, 0, 0};
         static const char* const kNames[4] = {"geometry-offset",
                                               "geometry-size", "object-offset",
                                               "object-size"};
         for (int k = 0; k < 4; ++k) {
             if (!parse_u64(std::string(argv[3 + k]), numbers[k])) {
                 std::cerr << "error: " << kNames[k]
                           << " must be a decimal or 0x integer\n";
                 return 1;
             }
         }
         nfsu::TrackRegionSelector geometry_region;
         geometry_region.offset = numbers[0];
         geometry_region.size = numbers[1];
         nfsu::TrackRegionSelector object_region;
         object_region.offset = numbers[2];
         object_region.size = numbers[3];

         TrackRenderArgs args;
         args.scene.cull.policy = nfsu::TrackCullPolicy::draw_everything;
         // The CULL camera, not the render camera: --view-target
         // below is the other one, and the two are deliberately never
         // the same flag.
         bool has_cull_eye = false;
         bool has_cull_target = false;
         bool has_output = false;
         for (int i = 7; i < argc; ++i) {
             const std::string flag = argv[i];
             const bool has_value = i + 1 < argc;
             const std::string value = has_value ? argv[i + 1] : std::string();
             const auto take_triple = [&](float out[3]) {
                 if (i + 3 >= argc) {
                     std::cerr << "error: " << flag
                               << " needs three finite floats\n";
                     return false;
                 }
                 for (int k = 0; k < 3; ++k) {
                     if (!parse_f32(std::string(argv[i + 1 + k]), out[k])) {
                         std::cerr << "error: " << flag
                                   << " needs three finite floats\n";
                         return false;
                     }
                 }
                 i += 3;
                 return true;
             };
             const auto take_double = [&](double& out) {
                 if (!has_value || !parse_f64(value, out)) {
                     std::cerr << "error: " << flag << " needs a finite number\n";
                     return false;
                 }
                 ++i;
                 return true;
             };
             const auto take_ranged = [&](double& out, double low,
                                         double high) {
                 double parsed = 0.0;
                 if (!has_value || !parse_f64(value, parsed) ||
                     parsed < low || parsed > high) {
                     std::cerr << "error: " << flag
                               << " needs a number from "
                               << format_double(low) << " to "
                               << format_double(high) << '\n';
                     return false;
                 }
                 out = parsed;
                 ++i;
                 return true;
             };
             const auto take_size = [&](std::uint32_t& out) {
                 std::uint64_t parsed = 0;
                 if (!has_value || !parse_u64(value, parsed) || parsed == 0 ||
                     parsed > 8192) {
                     std::cerr << "error: " << flag << " needs a size from 1 to"
                                  " 8192\n";
                     return false;
                 }
                 out = static_cast<std::uint32_t>(parsed);
                 ++i;
                 return true;
             };
             if (flag == "--out") {
                 if (!has_value) {
                     std::cerr << "error: --out needs a path\n";
                     return 1;
                 }
                 args.output = nfsu::argument_path(argv[i + 1]);
                 has_output = true;
                 ++i;
             } else if (flag == "--texture") {
                 if (!has_value) {
                     std::cerr << "error: --texture needs a path\n";
                     return 1;
                 }
                 args.scene.textures.push_back(nfsu::argument_path(argv[i + 1]));
                 ++i;
             } else if (flag == "--lod-slot") {
                 std::uint64_t slot = 0;
                 if (!has_value || !parse_u64(value, slot) ||
                     slot >= nfsu::kSceneryObjectMeshSlots) {
                     std::cerr << "error: --lod-slot needs a slot from 0 to "
                               << (nfsu::kSceneryObjectMeshSlots - 1) << '\n';
                     return 1;
                 }
                 args.scene.scene.lod_slot = static_cast<std::size_t>(slot);
                 ++i;
             } else if (flag == "--order") {
                 if (value == "visible-instance") {
                     args.scene.list.order =
                         nfsu::TrackDrawOrder::visible_instance;
                 } else if (value == "material-group") {
                     args.scene.list.order = nfsu::TrackDrawOrder::material_group;
                 } else {
                     std::cerr << "error: --order must be visible-instance or"
                                  " material-group\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--eye") {
                 if (!take_triple(args.scene.camera.eye)) {
                     return 1;
                 }
                 has_cull_eye = true;
             } else if (flag == "--target") {
                 if (!take_triple(args.scene.camera.target)) {
                     return 1;
                 }
                 has_cull_target = true;
             } else if (flag == "--far") {
                 if (!has_value ||
                     !parse_f32(value, args.scene.camera.far_plane)) {
                     std::cerr << "error: --far needs a finite float\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--residency") {
                 if (value == "every") {
                     args.scene.cull.residency.rule =
                         nfsu::TrackResidencyRule::every_section;
                 } else if (value == "no-zone") {
                     args.scene.cull.residency.rule =
                         nfsu::TrackResidencyRule::no_zone;
                 } else {
                     std::cerr << "error: --residency must be every or"
                                  " no-zone\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--list") {
                 std::uint64_t count = 0;
                 if (!has_value || !parse_u64(value, count)) {
                     std::cerr << "error: --list needs a count\n";
                     return 1;
                 }
                 args.list_limit = static_cast<std::size_t>(count);
                 ++i;
             } else if (flag == "--width") {
                 if (!take_size(args.render.width)) {
                     return 1;
                 }
             } else if (flag == "--height") {
                 if (!take_size(args.render.height)) {
                     return 1;
                 }
             } else if (flag == "--view-yaw") {
                 // The same ranges frame_track_bounds enforces, checked
                 // here so that the diagnostic names the option and
                 // arrives before any file is opened.
                 if (!take_ranged(args.framing.yaw_deg,
                                  -nfsu::kMaxYawMagnitude,
                                  nfsu::kMaxYawMagnitude)) {
                     return 1;
                 }
             } else if (flag == "--view-pitch") {
                 if (!take_ranged(args.framing.pitch_deg, nfsu::kMinPitch,
                                  nfsu::kMaxPitch)) {
                     return 1;
                 }
             } else if (flag == "--view-fov") {
                 if (!take_ranged(args.framing.fov_y_deg, nfsu::kMinFov,
                                  nfsu::kMaxFov)) {
                     return 1;
                 }
             } else if (flag == "--view-margin") {
                 if (!take_ranged(args.framing.margin,
                                  nfsu::kMinTrackFramingMargin,
                                  nfsu::kMaxTrackFramingMargin)) {
                     return 1;
                 }
             } else if (flag == "--view-distance") {
                 if (!take_double(args.distance)) {
                     return 1;
                 }
                 args.has_distance = true;
             } else if (flag == "--view-near") {
                 if (!take_double(args.near_plane)) {
                     return 1;
                 }
                 args.has_near = true;
             } else if (flag == "--view-far") {
                 if (!take_double(args.far_plane)) {
                     return 1;
                 }
                 args.has_far = true;
             } else if (flag == "--view-target") {
                 if (i + 3 >= argc) {
                     std::cerr << "error: --view-target needs three finite"
                                  " numbers\n";
                     return 1;
                 }
                 double triple[3] = {0.0, 0.0, 0.0};
                 for (int k = 0; k < 3; ++k) {
                     if (!parse_f64(std::string(argv[i + 1 + k]), triple[k])) {
                         std::cerr << "error: --view-target needs three finite"
                                      " numbers\n";
                         return 1;
                     }
                 }
                 args.target = nfsu::Vec3{triple[0], triple[1], triple[2]};
                 args.has_target = true;
                 i += 3;
             } else if (flag == "--cull") {
                 if (value == "none") {
                     args.render.cull = nfsu::CullMode::none;
                 } else if (value == "back") {
                     args.render.cull = nfsu::CullMode::back;
                 } else if (value == "front") {
                     args.render.cull = nfsu::CullMode::front;
                 } else {
                     std::cerr << "error: --cull must be none, back or front\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--shading") {
                 if (value == "flat") {
                     args.render.shading = nfsu::ShadingMode::flat;
                 } else if (value == "none") {
                     args.render.shading = nfsu::ShadingMode::none;
                 } else {
                     std::cerr << "error: --shading must be flat or none\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--flip-v") {
                 args.render.flip_v = true;
             } else if (flag == "--vertex-color") {
                 args.render.vertex_color = true;
             } else if (flag == "--alpha-cutoff") {
                 std::uint64_t cutoff = 0;
                 if (!has_value || !parse_u64(value, cutoff) || cutoff > 255) {
                     std::cerr << "error: --alpha-cutoff needs 0 to 255\n";
                     return 1;
                 }
                 args.render.alpha_cutoff = static_cast<std::uint8_t>(cutoff);
                 ++i;
             } else if (flag == "--background") {
                 if (i + 3 >= argc) {
                     std::cerr << "error: --background needs three bytes\n";
                     return 1;
                 }
                 for (int k = 0; k < 3; ++k) {
                     std::uint64_t channel = 0;
                     if (!parse_u64(std::string(argv[i + 1 + k]), channel) ||
                         channel > 255) {
                         std::cerr << "error: --background needs three bytes\n";
                         return 1;
                     }
                     args.render.background[k] =
                         static_cast<unsigned char>(channel);
                 }
                 i += 3;
             } else {
                 std::cerr << "error: unknown option " << flag << '\n';
                 return 1;
             }
         }
         if (!has_output) {
             std::cerr << "error: --out <FILE> is required\n";
             return 1;
         }
         if (has_cull_eye != has_cull_target) {
             std::cerr << "error: --eye and --target are given together\n";
             return 1;
         }
         if (has_cull_eye) {
             args.scene.has_camera = true;
             args.scene.cull.policy = nfsu::TrackCullPolicy::retail_tree;
         }
         return run_track_render(nfsu::argument_path(argv[2]), geometry_region,
                                 object_region, args);
     }},
    {"track-budget",
     " <STREAM.BUN> <geometry-offset> <geometry-size>",
     "      <object-offset> <object-size>\n"
     "      [--texture <file>]... [--lod-slot <0..5>]\n"
     "      [--order <visible-instance|material-group>]\n"
     "      [--eye <x> <y> <z>] [--target <x> <y> <z>] [--far <f>]\n"
     "      [--residency <every|no-zone>]\n"
     "      [--repeat <N>] [--width <N>] [--height <N>]\n"
     "      [--view-yaw <deg>] [--view-pitch <deg>] [--view-fov <deg>]\n"
     "      [--view-margin <m>] [--no-render] [--no-time]\n"
     "      [--limit <counter> <ceiling>]...\n"
     "      (the scene options above --repeat are track-scene's and mean the\n"
     "       same; this command writes no file and renders into memory.\n"
     "       --repeat times each stage that many times after one discarded\n"
     "       warm-up and reports the spread; the cost: lines are exact and\n"
     "       the time: lines are not. --limit names a cost: counter and a\n"
     "       ceiling it may not exceed. Exit 4 means a limit was exceeded,\n"
     "       3 that nothing was over but the region did not fully parse, so\n"
     "       the counts may be short, and 5 that two repetitions disagreed\n"
     "       about a count.)\n",
     [](int argc, char** argv) {
         if (argc < 7) {
             print_usage();
             return 1;
         }
         std::uint64_t numbers[4] = {0, 0, 0, 0};
         static const char* const kNames[4] = {"geometry-offset",
                                               "geometry-size", "object-offset",
                                               "object-size"};
         for (int k = 0; k < 4; ++k) {
             if (!parse_u64(std::string(argv[3 + k]), numbers[k])) {
                 std::cerr << "error: " << kNames[k]
                           << " must be a decimal or 0x integer\n";
                 return 1;
             }
         }
         nfsu::TrackRegionSelector geometry_region;
         geometry_region.offset = numbers[0];
         geometry_region.size = numbers[1];
         nfsu::TrackRegionSelector object_region;
         object_region.offset = numbers[2];
         object_region.size = numbers[3];

         TrackBudgetArgs args;
         args.scene.cull.policy = nfsu::TrackCullPolicy::draw_everything;
         bool has_eye = false;
         bool has_target = false;
         for (int i = 7; i < argc; ++i) {
             const std::string flag = argv[i];
             const bool has_value = i + 1 < argc;
             const std::string value = has_value ? argv[i + 1] : std::string();
             const auto take_triple = [&](float out[3]) {
                 if (i + 3 >= argc) {
                     std::cerr << "error: " << flag
                               << " needs three finite floats\n";
                     return false;
                 }
                 for (int k = 0; k < 3; ++k) {
                     if (!parse_f32(std::string(argv[i + 1 + k]), out[k])) {
                         std::cerr << "error: " << flag
                                   << " needs three finite floats\n";
                         return false;
                     }
                 }
                 i += 3;
                 return true;
             };
             // The same two helpers track-render uses, and the same ranges:
             // a budget option that accepted a value the renderer refuses
             // would fail deep in the pipeline instead of naming the option.
             const auto take_ranged = [&](double& out, double low,
                                          double high) {
                 double parsed = 0.0;
                 if (!has_value || !parse_f64(value, parsed) || parsed < low ||
                     parsed > high) {
                     std::cerr << "error: " << flag << " needs a number from "
                               << format_double(low) << " to "
                               << format_double(high) << '\n';
                     return false;
                 }
                 out = parsed;
                 ++i;
                 return true;
             };
             const auto take_size = [&](std::uint32_t& out) {
                 std::uint64_t parsed = 0;
                 if (!has_value || !parse_u64(value, parsed) || parsed == 0 ||
                     parsed > 8192) {
                     std::cerr << "error: " << flag << " needs a size from 1 to"
                                  " 8192\n";
                     return false;
                 }
                 out = static_cast<std::uint32_t>(parsed);
                 ++i;
                 return true;
             };
             if (flag == "--texture") {
                 if (!has_value) {
                     std::cerr << "error: --texture needs a path\n";
                     return 1;
                 }
                 args.scene.textures.push_back(
                     nfsu::argument_path(argv[i + 1]));
                 ++i;
             } else if (flag == "--lod-slot") {
                 std::uint64_t slot = 0;
                 if (!has_value || !parse_u64(value, slot) ||
                     slot >= nfsu::kSceneryObjectMeshSlots) {
                     std::cerr << "error: --lod-slot needs a slot from 0 to "
                               << (nfsu::kSceneryObjectMeshSlots - 1) << '\n';
                     return 1;
                 }
                 args.scene.scene.lod_slot = static_cast<std::size_t>(slot);
                 ++i;
             } else if (flag == "--order") {
                 if (value == "visible-instance") {
                     args.scene.list.order =
                         nfsu::TrackDrawOrder::visible_instance;
                 } else if (value == "material-group") {
                     args.scene.list.order =
                         nfsu::TrackDrawOrder::material_group;
                 } else {
                     std::cerr << "error: --order must be visible-instance or"
                                  " material-group\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--eye") {
                 if (!take_triple(args.scene.camera.eye)) {
                     return 1;
                 }
                 has_eye = true;
             } else if (flag == "--target") {
                 if (!take_triple(args.scene.camera.target)) {
                     return 1;
                 }
                 has_target = true;
             } else if (flag == "--far") {
                 if (!has_value ||
                     !parse_f32(value, args.scene.camera.far_plane)) {
                     std::cerr << "error: --far needs a finite float\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--residency") {
                 if (value == "every") {
                     args.scene.cull.residency.rule =
                         nfsu::TrackResidencyRule::every_section;
                 } else if (value == "no-zone") {
                     args.scene.cull.residency.rule =
                         nfsu::TrackResidencyRule::no_zone;
                 } else {
                     std::cerr << "error: --residency must be every or"
                                  " no-zone\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--repeat") {
                 std::uint64_t count = 0;
                 if (!has_value || !parse_u64(value, count) || count < 1 ||
                     count > kMaxTrackBudgetRepeat) {
                     std::cerr << "error: --repeat needs a count from 1 to "
                               << kMaxTrackBudgetRepeat << '\n';
                     return 1;
                 }
                 args.repeat = count;
                 ++i;
             } else if (flag == "--width") {
                 if (!take_size(args.width)) {
                     return 1;
                 }
             } else if (flag == "--height") {
                 if (!take_size(args.height)) {
                     return 1;
                 }
             } else if (flag == "--view-yaw") {
                 if (!take_ranged(args.framing.yaw_deg,
                                  -nfsu::kMaxYawMagnitude,
                                  nfsu::kMaxYawMagnitude)) {
                     return 1;
                 }
             } else if (flag == "--view-pitch") {
                 if (!take_ranged(args.framing.pitch_deg, nfsu::kMinPitch,
                                  nfsu::kMaxPitch)) {
                     return 1;
                 }
             } else if (flag == "--view-fov") {
                 if (!take_ranged(args.framing.fov_y_deg, nfsu::kMinFov,
                                  nfsu::kMaxFov)) {
                     return 1;
                 }
             } else if (flag == "--view-margin") {
                 if (!take_ranged(args.framing.margin,
                                  nfsu::kMinTrackFramingMargin,
                                  nfsu::kMaxTrackFramingMargin)) {
                     return 1;
                 }
             } else if (flag == "--no-render") {
                 args.render = false;
             } else if (flag == "--no-time") {
                 args.show_time = false;
             } else if (flag == "--limit") {
                 if (i + 2 >= argc) {
                     std::cerr << "error: --limit needs a counter name and a"
                                  " ceiling\n";
                     return 1;
                 }
                 std::uint64_t ceiling = 0;
                 if (!parse_u64(std::string(argv[i + 2]), ceiling)) {
                     std::cerr << "error: --limit needs a decimal or 0x"
                                  " ceiling\n";
                     return 1;
                 }
                 nfsu::TrackBudgetLimit limit;
                 limit.name = argv[i + 1];
                 limit.ceiling = ceiling;
                 args.limits.push_back(limit);
                 i += 2;
             } else {
                 std::cerr << "error: unknown option " << flag << '\n';
                 return 1;
             }
         }
         if (has_eye != has_target) {
             std::cerr << "error: --eye and --target are given together\n";
             return 1;
         }
         if (has_eye) {
             args.scene.has_camera = true;
             args.scene.cull.policy = nfsu::TrackCullPolicy::retail_tree;
         }
         // A limit naming a render counter under --no-render would read as a
         // budget that passed, because nothing measured the stage and its
         // value is zero. check_track_budget refuses that rather than
         // reporting it as within budget, and run_track_budget returns 1 on
         // the refusal.
         return run_track_budget(nfsu::argument_path(argv[2]), geometry_region,
                                 object_region, args);
     }},
    {"track-probe",
     " <STREAM.BUN> <geometry-offset> <geometry-size>",
     "      <object-offset> <object-size>\n"
     "      [--texture <file>]... [--lod-slot <0..5>]\n"
     "      [--order <visible-instance|material-group>]\n"
     "      [--policy <retail-tree|draw-everything|flat-instance-test>]\n"
     "      [--residency <every|no-zone|zone>] [--zone <section-number>]...\n"
     "      [--zones <TRACKB####.lzc> --zone-point <slot0> <slot1>]\n"
     "      [--mask <0xNN>] [--screen-scale <s>]\n"
     "      [--width <N>] [--height <N>] [--view-fov <deg>]\n"
     "      [--view-yaw <deg>] [--view-pitch <deg>] [--view-margin <m>]\n"
     "      [--eye <x> <y> <z>] [--look-at <x> <y> <z>] [--up <x> <y> <z>]\n"
     "      [--view-near <n>] [--view-far <f>]\n"
     "      [--turn <yaw> <pitch>] [--fly <forward> <right> <up>]\n"
     "      [--section <n>] [--instance <n>]... [--mesh <NAME>]\n"
     "      [--limit <count>]\n"
     "      (ONE free camera drives the frustum, the screen-size gate and\n"
     "       the report. With no --eye it frames the whole placed region,\n"
     "       which is view-independent, and --turn then --fly move it from\n"
     "       there. Every option is given at most once except --texture,\n"
     "       --zone and --instance. This command writes no file.)\n",
     [](int argc, char** argv) {
         if (argc < 7) {
             print_usage();
             return 1;
         }
         std::uint64_t numbers[4] = {0, 0, 0, 0};
         static const char* const kNames[4] = {"geometry-offset",
                                               "geometry-size", "object-offset",
                                               "object-size"};
         for (int k = 0; k < 4; ++k) {
             if (!parse_u64(std::string(argv[3 + k]), numbers[k])) {
                 std::cerr << "error: " << kNames[k]
                           << " must be a decimal or 0x integer\n";
                 return 1;
             }
         }
         nfsu::TrackRegionSelector geometry_region;
         geometry_region.offset = numbers[0];
         geometry_region.size = numbers[1];
         nfsu::TrackRegionSelector object_region;
         object_region.offset = numbers[2];
         object_region.size = numbers[3];

         TrackProbeArgs args;
         // The recovered rule is the default here. track-scene defaults to
         // the fallback because it may be given no camera at all; this
         // command always has one, so the frustum rule is always meaningful.
         args.scene.cull.policy = nfsu::TrackCullPolicy::retail_tree;

         // Every option this command takes, and the three that may repeat.
         // An option outside the first list is refused by name; one outside
         // the second is refused when it is given twice, because an option
         // written twice would make the answer depend on the order the
         // command line was written in, and a deterministic selection cannot
         // depend on that.
         static const char* const kFlags[] = {
             "--texture", "--lod-slot", "--order", "--policy", "--residency",
             "--zone", "--zones", "--zone-point", "--mask", "--screen-scale",
             "--width", "--height", "--view-fov", "--view-yaw", "--view-pitch",
             "--view-margin", "--eye", "--look-at", "--up", "--view-near",
             "--view-far", "--turn", "--fly", "--section", "--instance",
             "--mesh", "--limit"};
         static const char* const kRepeatable[] = {"--texture", "--zone",
                                                   "--instance"};
         std::vector<std::string> seen;
         const auto given = [&seen](const char* name) {
             return std::find(seen.begin(), seen.end(), std::string(name)) !=
                    seen.end();
         };

         for (int i = 7; i < argc; ++i) {
             const std::string flag = argv[i];
             bool known = false;
             for (const char* const name : kFlags) {
                 if (flag == name) {
                     known = true;
                     break;
                 }
             }
             if (!known) {
                 std::cerr << "error: unknown option " << flag << '\n';
                 return 1;
             }
             bool repeatable = false;
             for (const char* const name : kRepeatable) {
                 if (flag == name) {
                     repeatable = true;
                     break;
                 }
             }
             if (!repeatable) {
                 if (given(flag.c_str())) {
                     std::cerr << "error: " << flag
                               << " is given at most once\n";
                     return 1;
                 }
                 seen.push_back(flag);
             }

             const bool has_value = i + 1 < argc;
             const std::string value = has_value ? argv[i + 1] : std::string();
             const auto take_ranged = [&](double& out, double low,
                                          double high) {
                 double parsed = 0.0;
                 if (!has_value || !parse_f64(value, parsed) || parsed < low ||
                     parsed > high) {
                     std::cerr << "error: " << flag << " needs a number from "
                               << format_double(low) << " to "
                               << format_double(high) << '\n';
                     return false;
                 }
                 out = parsed;
                 ++i;
                 return true;
             };
             const auto take_size = [&](std::uint32_t& out) {
                 std::uint64_t parsed = 0;
                 if (!has_value || !parse_u64(value, parsed) || parsed == 0 ||
                     parsed > nfsu::kMaxTrackProbeFrameSize) {
                     std::cerr << "error: " << flag << " needs a size from 1 to "
                               << nfsu::kMaxTrackProbeFrameSize << '\n';
                     return false;
                 }
                 out = static_cast<std::uint32_t>(parsed);
                 ++i;
                 return true;
             };
             const auto take_count = [&](std::uint64_t& out) {
                 std::uint64_t parsed = 0;
                 if (!has_value || !parse_u64(value, parsed)) {
                     std::cerr << "error: " << flag
                               << " needs a decimal or 0x integer\n";
                     return false;
                 }
                 out = parsed;
                 ++i;
                 return true;
             };
             const auto take_vec3 = [&](nfsu::Vec3& out) {
                 double triple[3] = {0.0, 0.0, 0.0};
                 if (i + 3 >= argc) {
                     std::cerr << "error: " << flag
                               << " needs three finite numbers\n";
                     return false;
                 }
                 for (int k = 0; k < 3; ++k) {
                     if (!parse_f64(std::string(argv[i + 1 + k]), triple[k])) {
                         std::cerr << "error: " << flag
                                   << " needs three finite numbers\n";
                         return false;
                     }
                 }
                 out = nfsu::Vec3{triple[0], triple[1], triple[2]};
                 i += 3;
                 return true;
             };

             if (flag == "--texture") {
                 if (!has_value) {
                     std::cerr << "error: --texture needs a path\n";
                     return 1;
                 }
                 args.scene.textures.push_back(nfsu::argument_path(argv[i + 1]));
                 ++i;
             } else if (flag == "--lod-slot") {
                 std::uint64_t slot = 0;
                 if (!has_value || !parse_u64(value, slot) ||
                     slot >= nfsu::kSceneryObjectMeshSlots) {
                     std::cerr << "error: --lod-slot needs a slot from 0 to "
                               << (nfsu::kSceneryObjectMeshSlots - 1) << '\n';
                     return 1;
                 }
                 args.scene.scene.lod_slot = static_cast<std::size_t>(slot);
                 ++i;
             } else if (flag == "--order") {
                 if (value == "visible-instance") {
                     args.scene.list.order =
                         nfsu::TrackDrawOrder::visible_instance;
                 } else if (value == "material-group") {
                     args.scene.list.order =
                         nfsu::TrackDrawOrder::material_group;
                 } else {
                     std::cerr << "error: --order must be visible-instance or"
                                  " material-group\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--policy") {
                 if (value == "retail-tree") {
                     args.scene.cull.policy = nfsu::TrackCullPolicy::retail_tree;
                 } else if (value == "draw-everything") {
                     args.scene.cull.policy =
                         nfsu::TrackCullPolicy::draw_everything;
                 } else if (value == "flat-instance-test") {
                     args.scene.cull.policy =
                         nfsu::TrackCullPolicy::flat_instance_test;
                 } else {
                     std::cerr << "error: --policy must be retail-tree,"
                                  " draw-everything or flat-instance-test\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--residency") {
                 if (value == "every") {
                     args.scene.cull.residency.rule =
                         nfsu::TrackResidencyRule::every_section;
                 } else if (value == "no-zone") {
                     args.scene.cull.residency.rule =
                         nfsu::TrackResidencyRule::no_zone;
                 } else if (value == "zone") {
                     args.scene.cull.residency.rule =
                         nfsu::TrackResidencyRule::zone_list;
                 } else {
                     std::cerr << "error: --residency must be every, no-zone or"
                                  " zone\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--zone") {
                 std::uint64_t section = 0;
                 if (!has_value || !parse_u64(value, section) ||
                     section > 0xFFFFFFFFULL) {
                     std::cerr << "error: --zone needs a section number\n";
                     return 1;
                 }
                 args.scene.cull.residency.zone_sections.push_back(
                     static_cast<std::uint32_t>(section));
                 ++i;
             } else if (flag == "--zones") {
                 if (!has_value) {
                     std::cerr << "error: --zones needs a path\n";
                     return 1;
                 }
                 args.zone_file = nfsu::argument_path(argv[i + 1]);
                 args.has_zone_file = true;
                 ++i;
             } else if (flag == "--zone-point") {
                 float point[2] = {0.0F, 0.0F};
                 if (i + 2 >= argc ||
                     !parse_f32(std::string(argv[i + 1]), point[0]) ||
                     !parse_f32(std::string(argv[i + 2]), point[1])) {
                     std::cerr << "error: --zone-point needs two finite"
                                  " floats\n";
                     return 1;
                 }
                 args.zone_point[0] = point[0];
                 args.zone_point[1] = point[1];
                 args.has_zone_point = true;
                 i += 2;
             } else if (flag == "--mask") {
                 std::uint64_t mask = 0;
                 if (!has_value || !parse_u64(value, mask) || mask > 0xFFULL) {
                     std::cerr << "error: --mask needs a byte from 0 to 255\n";
                     return 1;
                 }
                 args.scene.cull.instance_mask =
                     static_cast<std::uint8_t>(mask);
                 ++i;
             } else if (flag == "--screen-scale") {
                 double parsed = 0.0;
                 if (!has_value || !parse_f64(value, parsed)) {
                     std::cerr << "error: --screen-scale needs a finite"
                                  " number\n";
                     return 1;
                 }
                 args.screen_scale = parsed;
                 args.use_screen_size = true;
                 ++i;
             } else if (flag == "--width") {
                 if (!take_size(args.width)) {
                     return 1;
                 }
             } else if (flag == "--height") {
                 if (!take_size(args.height)) {
                     return 1;
                 }
             } else if (flag == "--view-fov") {
                 if (!take_ranged(args.framing.fov_y_deg, nfsu::kMinFov,
                                  nfsu::kMaxFov)) {
                     return 1;
                 }
             } else if (flag == "--view-yaw") {
                 if (!take_ranged(args.framing.yaw_deg,
                                  -nfsu::kMaxYawMagnitude,
                                  nfsu::kMaxYawMagnitude)) {
                     return 1;
                 }
             } else if (flag == "--view-pitch") {
                 if (!take_ranged(args.framing.pitch_deg, nfsu::kMinPitch,
                                  nfsu::kMaxPitch)) {
                     return 1;
                 }
             } else if (flag == "--view-margin") {
                 if (!take_ranged(args.framing.margin,
                                  nfsu::kMinTrackFramingMargin,
                                  nfsu::kMaxTrackFramingMargin)) {
                     return 1;
                 }
             } else if (flag == "--eye") {
                 if (!take_vec3(args.eye)) {
                     return 1;
                 }
                 args.has_eye = true;
             } else if (flag == "--look-at") {
                 if (!take_vec3(args.look_at)) {
                     return 1;
                 }
                 args.has_look_at = true;
             } else if (flag == "--up") {
                 if (!take_vec3(args.up)) {
                     return 1;
                 }
             } else if (flag == "--view-near") {
                 if (!take_ranged(args.near_plane, nfsu::kMinNear,
                                  nfsu::kMaxNear)) {
                     return 1;
                 }
                 args.has_near = true;
             } else if (flag == "--view-far") {
                 if (!take_ranged(args.far_plane, nfsu::kMinNear,
                                  nfsu::kMaxFar)) {
                     return 1;
                 }
                 args.has_far = true;
             } else if (flag == "--turn") {
                 double pair[2] = {0.0, 0.0};
                 if (i + 2 >= argc ||
                     !parse_f64(std::string(argv[i + 1]), pair[0]) ||
                     !parse_f64(std::string(argv[i + 2]), pair[1])) {
                     std::cerr << "error: --turn needs a yaw and a pitch in"
                                  " degrees\n";
                     return 1;
                 }
                 args.moves.turn_yaw_deg = pair[0];
                 args.moves.turn_pitch_deg = pair[1];
                 args.moves.has_turn = true;
                 i += 2;
             } else if (flag == "--fly") {
                 nfsu::Vec3 legs;
                 if (!take_vec3(legs)) {
                     return 1;
                 }
                 args.moves.fly_forward = legs.x;
                 args.moves.fly_right = legs.y;
                 args.moves.fly_up = legs.z;
                 args.moves.has_fly = true;
             } else if (flag == "--section") {
                 std::uint64_t index = 0;
                 if (!take_count(index)) {
                     return 1;
                 }
                 args.select.has_section = true;
                 args.select.section_index = static_cast<std::size_t>(index);
             } else if (flag == "--instance") {
                 std::uint64_t index = 0;
                 if (!take_count(index)) {
                     return 1;
                 }
                 args.select.instances.push_back(
                     static_cast<std::size_t>(index));
             } else if (flag == "--mesh") {
                 if (!has_value || value.empty()) {
                     std::cerr << "error: --mesh needs a prepared mesh name\n";
                     return 1;
                 }
                 args.select.mesh_name = value;
                 ++i;
             } else if (flag == "--limit") {
                 std::uint64_t limit = 0;
                 if (!take_count(limit)) {
                     return 1;
                 }
                 args.list_limit = static_cast<std::size_t>(limit);
             } else {
                 // kFlags accepted it and no branch above claimed it, so the
                 // two lists have drifted apart. Refusing is the only answer
                 // that cannot be mistaken for the option having been used.
                 std::cerr << "error: option " << flag
                           << " is listed and not handled\n";
                 return 1;
             }
         }

         if (args.has_eye != args.has_look_at) {
             std::cerr << "error: --eye and --look-at are given together\n";
             return 1;
         }
         if (args.has_eye && (given("--view-yaw") || given("--view-pitch") ||
                              given("--view-margin"))) {
             std::cerr << "error: --view-yaw, --view-pitch and --view-margin"
                          " describe the automatic framing, which --eye"
                          " replaces\n";
             return 1;
         }
         if (args.has_zone_file != args.has_zone_point) {
             std::cerr << "error: --zones and --zone-point are used together\n";
             return 1;
         }
         if (args.has_zone_file &&
             (given("--residency") ||
              !args.scene.cull.residency.zone_sections.empty())) {
             std::cerr << "error: --zones replaces the residency, so it cannot"
                          " be combined with --residency or --zone\n";
             return 1;
         }
         if (!args.scene.cull.residency.zone_sections.empty() &&
             args.scene.cull.residency.rule !=
                 nfsu::TrackResidencyRule::zone_list) {
             std::cerr << "error: --zone needs --residency zone\n";
             return 1;
         }
         if ((given("--mask") || given("--screen-scale")) &&
             args.scene.cull.policy ==
                 nfsu::TrackCullPolicy::draw_everything) {
             std::cerr << "error: --mask and --screen-scale are not consulted"
                          " under --policy draw-everything\n";
             return 1;
         }
         if (!args.select.instances.empty() && !args.select.has_section) {
             std::cerr << "error: --instance needs --section\n";
             return 1;
         }
         return run_track_probe(nfsu::argument_path(argv[2]), geometry_region,
                                object_region, args);
     }},
    {"track-collision", " <TRACKB####.lzc> [px pz]", "",
     [](int argc, char** argv) {
         if (argc != 3 && argc != 5) {
             print_usage();
             return 1;
         }
         double slot0 = 0.0;
         double slot1 = 0.0;
         if (argc == 5) {
             try {
                 std::size_t used0 = 0;
                 std::size_t used1 = 0;
                 const std::string first(argv[3]);
                 const std::string second(argv[4]);
                 slot0 = std::stod(first, &used0);
                 slot1 = std::stod(second, &used1);
                 if (used0 != first.size() || used1 != second.size()) {
                     std::cerr << "error: px and pz must be numbers\n";
                     return 1;
                 }
             } catch (const std::exception&) {
                 std::cerr << "error: px and pz must be numbers\n";
                 return 1;
             }
         }
         return run_track_collision(nfsu::argument_path(argv[2]), argc == 5, slot0,
                                    slot1);
     }},
    {"track-contact", " <TRACKB####.lzc> [px pz | px pz py px pz py]", "",
     [](int argc, char** argv) {
         if (argc != 3 && argc != 5 && argc != 9) {
             print_usage();
             return 1;
         }
         float probe[6] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
         for (int i = 3; i < argc; ++i) {
             const std::string value(argv[i]);
             double number = 0.0;
             std::size_t used = 0;
             try {
                 number = std::stod(value, &used);
             } catch (const std::exception&) {
                 used = 0;
             }
             const float narrowed = static_cast<float>(number);
             if (used == 0 || used != value.size() || !std::isfinite(narrowed)) {
                 std::cerr << "error: every coordinate must be a finite number, got '"
                           << value << "'\n";
                 return 1;
             }
             probe[i - 3] = narrowed;
         }
         const int query = argc == 3 ? 0 : (argc == 5 ? 1 : 2);
         return run_track_contact(nfsu::argument_path(argv[2]), query, probe);
     }},
    {"car-placement", " <GLOBALB.BUN> [car-type]", "",
     [](int argc, char** argv) {
         if (argc != 3 && argc != 4) {
             print_usage();
             return 1;
         }
         return run_car_placement(nfsu::argument_path(argv[2]),
                                  argc == 4 ? std::string(argv[3]) : "");
     }},
    {"vehicle-sim", " <script-name> | --script <FILE>",
     "      [--steps N] [--every N] [--handling <GLOBALB.BUN> [--car NAME]]\n"
     "      (script names: rest accelerate brake corner slide top-speed manual mixed)\n",
     [](int argc, char** argv) {
         VehicleSimArgs args;
         const int parsed = parse_vehicle_sim(argc, argv, args);
         if (parsed != 0) {
             return parsed;
         }
         return run_vehicle_sim(args);
     }},
    {"vehicle-tuning", " <GLOBALB.BUN>",
     "      (the per-level tuning tables of docs/VEHICLE_TUNING.md for every car\n"
     "       type, and the 240SX's compared with the built-in tables)\n",
     [](int argc, char** argv) {
         if (argc != 3) {
             print_usage();
             return 1;
         }
         return run_vehicle_tuning(nfsu::argument_path(argv[2]));
     }},
    {"drive-replay", " --demo | --input <FILE>",
     "      [--controls <FILE>] [--pov <0|1|4>] [--frames N] [--every N]\n"
     "      [--root <DIR> [--render-every N] [--out <PREFIX>] [--width N]\n"
     "       [--height N]]\n"
     "      (replays a recorded controller stream, docs/INPUT.md's form, through\n"
     "       the driving controls, the vehicle model on flat ground and the chase\n"
     "       camera, one 1/60 s tick per frame, and prints the stream's hash;\n"
     "       with --root, the game root, it renders the stock 240SX from the\n"
     "       camera every N frames and prints each frame's hash. --out writes\n"
     "       PREFIX-NNNNNN.tga and never replaces a file.)\n",
     [](int argc, char** argv) {
         DriveReplayArgs args;
         const int parsed = parse_drive_replay(argc, argv, args);
         if (parsed != 0) {
             return parsed;
         }
         return run_drive_replay(args);
     }},
    {"vehicle-track", " <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>",
     "      (<scenario> | --script <FILE>) [--steps N] [--every N], or\n"
     "      vehicle-track --proving-ground (<scenario> | --script <FILE>) [...]\n"
     "      (event 1's circuit from grid slot 0, or the proving ground built\n"
     "       in code; circuit scenarios: grid drive wall wall-square drop\n"
     "       leave; proving-ground scenarios: rest drive slope wall\n"
     "       wall-glance wall-fast wall-square wall-oblique drop leave zone\n"
     "       zone-point far; docs/VEHICLE_TRACK.md)\n",
     [](int argc, char** argv) {
         VehicleTrackArgs args;
         const int parsed = parse_vehicle_track(argc, argv, args);
         if (parsed != 0) {
             return parsed;
         }
         return run_vehicle_track(args);
     }},
    {"race-events", " <GLOBALB.BUN> <FrontB.lzc>",
     "      (either file raw or JDLZ-compressed, as the game ships them)\n",
     [](int argc, char** argv) {
         if (argc != 4) {
             print_usage();
             return 1;
         }
         return run_race_events(nfsu::argument_path(argv[2]),
                                nfsu::argument_path(argv[3]));
     }},
    {"track-whole",
     " <STREAM.BUN> <TRACKB####.lzc> <track-number>",
     "      [--set <permanent|zones|stream>] [--pack <STREAM_NNNN.BUN>]\n"
     "      [--texture <file>]... [--lod-slot <0..5>] [--list <count>]\n"
     "      [--out <FILE>] [--width <N>] [--height <N>]\n"
     "      [--view-yaw <deg>] [--view-pitch <deg>] [--view-fov <deg>]\n"
     "      [--view-margin <m>] [--view-distance <d>]\n"
     "      [--view-target <x> <y> <z>] [--view-near <d>] [--view-far <d>]\n"
     "      (loads the sections the streaming table of the TRACKB file\n"
     "       selects for the track and places them as one scene;\n"
     "       permanent is the rule speed.exe applies, zones and stream\n"
     "       are bounds. --out draws the whole track and is never\n"
     "       replaced; every --view-* option needs it. Every option is\n"
     "       given at most once except --texture.)\n",
     [](int argc, char** argv) {
         if (argc < 5) {
             print_usage();
             return 1;
         }
         std::uint64_t track = 0;
         if (!parse_u64(std::string(argv[4]), track) || track == 0 ||
             track > 0x7FFFU) {
             std::cerr << "error: track-number must be a track number from 1"
                          " to 32767\n";
             return 1;
         }
         TrackWholeArgs args;
         static const char* const kFlags[] = {
             "--set",        "--pack",       "--texture",     "--lod-slot",
             "--list",       "--out",        "--width",       "--height",
             "--view-yaw",   "--view-pitch", "--view-fov",    "--view-margin",
             "--view-distance", "--view-target", "--view-near", "--view-far"};
         std::vector<std::string> seen;
         const auto given = [&seen](const char* flag) {
             return std::find(seen.begin(), seen.end(), flag) != seen.end();
         };
         for (int i = 5; i < argc; ++i) {
             const std::string flag = argv[i];
             if (std::find(std::begin(kFlags), std::end(kFlags), flag) ==
                 std::end(kFlags)) {
                 std::cerr << "error: unknown option " << flag << '\n';
                 return 1;
             }
             if (flag != "--texture" && given(flag.c_str())) {
                 std::cerr << "error: " << flag << " is given more than once\n";
                 return 1;
             }
             seen.push_back(flag);
             const bool has_value = i + 1 < argc;
             const std::string value = has_value ? argv[i + 1] : std::string();
             const auto take_ranged = [&](double& out, double low,
                                         double high) {
                 double parsed = 0.0;
                 if (!has_value || !parse_f64(value, parsed) || parsed < low ||
                     parsed > high) {
                     std::cerr << "error: " << flag << " needs a number from "
                               << format_double(low) << " to "
                               << format_double(high) << '\n';
                     return false;
                 }
                 out = parsed;
                 ++i;
                 return true;
             };
             const auto take_size = [&](std::uint32_t& out) {
                 std::uint64_t parsed = 0;
                 if (!has_value || !parse_u64(value, parsed) || parsed == 0 ||
                     parsed > 8192) {
                     std::cerr << "error: " << flag
                               << " needs a size from 1 to 8192\n";
                     return false;
                 }
                 out = static_cast<std::uint32_t>(parsed);
                 ++i;
                 return true;
             };
             if (flag == "--set") {
                 if (value == "permanent") {
                     args.set = nfsu::TrackStreamSet::permanent;
                 } else if (value == "zones") {
                     args.set = nfsu::TrackStreamSet::zones;
                 } else if (value == "stream") {
                     args.set = nfsu::TrackStreamSet::stream;
                 } else {
                     std::cerr << "error: --set must be permanent, zones or"
                                  " stream\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--pack") {
                 if (!has_value) {
                     std::cerr << "error: --pack needs a path\n";
                     return 1;
                 }
                 args.pack = nfsu::argument_path(argv[i + 1]);
                 args.has_pack = true;
                 ++i;
             } else if (flag == "--texture") {
                 if (!has_value) {
                     std::cerr << "error: --texture needs a path\n";
                     return 1;
                 }
                 args.textures.push_back(nfsu::argument_path(argv[i + 1]));
                 ++i;
             } else if (flag == "--lod-slot") {
                 std::uint64_t slot = 0;
                 if (!has_value || !parse_u64(value, slot) ||
                     slot >= nfsu::kSceneryObjectMeshSlots) {
                     std::cerr << "error: --lod-slot needs a slot from 0 to "
                               << (nfsu::kSceneryObjectMeshSlots - 1) << '\n';
                     return 1;
                 }
                 args.lod_slot = static_cast<std::size_t>(slot);
                 ++i;
             } else if (flag == "--list") {
                 std::uint64_t count = 0;
                 if (!has_value || !parse_u64(value, count)) {
                     std::cerr << "error: --list needs a count\n";
                     return 1;
                 }
                 args.has_list = true;
                 args.list_limit = static_cast<std::size_t>(count);
                 ++i;
             } else if (flag == "--out") {
                 if (!has_value) {
                     std::cerr << "error: --out needs a path\n";
                     return 1;
                 }
                 args.output = nfsu::argument_path(argv[i + 1]);
                 args.has_output = true;
                 ++i;
             } else if (flag == "--width") {
                 if (!take_size(args.render.width)) {
                     return 1;
                 }
             } else if (flag == "--height") {
                 if (!take_size(args.render.height)) {
                     return 1;
                 }
             } else if (flag == "--view-yaw") {
                 if (!take_ranged(args.framing.yaw_deg, -nfsu::kMaxYawMagnitude,
                                  nfsu::kMaxYawMagnitude)) {
                     return 1;
                 }
             } else if (flag == "--view-pitch") {
                 if (!take_ranged(args.framing.pitch_deg, nfsu::kMinPitch,
                                  nfsu::kMaxPitch)) {
                     return 1;
                 }
             } else if (flag == "--view-fov") {
                 if (!take_ranged(args.framing.fov_y_deg, nfsu::kMinFov,
                                  nfsu::kMaxFov)) {
                     return 1;
                 }
             } else if (flag == "--view-margin") {
                 if (!take_ranged(args.framing.margin,
                                  nfsu::kMinTrackFramingMargin,
                                  nfsu::kMaxTrackFramingMargin)) {
                     return 1;
                 }
             } else if (flag == "--view-distance") {
                 if (!take_ranged(args.distance, nfsu::kMinDistance,
                                  nfsu::kMaxDistance)) {
                     return 1;
                 }
                 args.has_distance = true;
             } else if (flag == "--view-near") {
                 if (!take_ranged(args.near_plane, nfsu::kMinNear,
                                  nfsu::kMaxNear)) {
                     return 1;
                 }
                 args.has_near = true;
             } else if (flag == "--view-far") {
                 if (!take_ranged(args.far_plane, nfsu::kMinNear,
                                  nfsu::kMaxFar)) {
                     return 1;
                 }
                 args.has_far = true;
             } else if (flag == "--view-target") {
                 double xyz[3] = {0.0, 0.0, 0.0};
                 if (i + 3 >= argc) {
                     std::cerr << "error: --view-target needs three numbers"
                                  " from "
                               << format_double(-nfsu::kMaxTargetMagnitude)
                               << " to "
                               << format_double(nfsu::kMaxTargetMagnitude)
                               << '\n';
                     return 1;
                 }
                 for (int k = 0; k < 3; ++k) {
                     if (!parse_f64(std::string(argv[i + 1 + k]), xyz[k]) ||
                         xyz[k] < -nfsu::kMaxTargetMagnitude ||
                         xyz[k] > nfsu::kMaxTargetMagnitude) {
                         std::cerr << "error: --view-target needs three"
                                      " numbers from "
                                   << format_double(-nfsu::kMaxTargetMagnitude)
                                   << " to "
                                   << format_double(nfsu::kMaxTargetMagnitude)
                                   << '\n';
                         return 1;
                     }
                 }
                 args.target = nfsu::Vec3{xyz[0], xyz[1], xyz[2]};
                 args.has_target = true;
                 i += 3;
             } else {
                 // kFlags accepted it and no branch above claimed it.
                 std::cerr << "error: option " << flag
                           << " is listed and not handled\n";
                 return 1;
             }
         }
         if (!args.has_output) {
             // A named array rather than a braced list at the start of a
             // line: tests/usage_commands.cmake reads every line of this
             // table that opens with a brace and a string as a command.
             static const char* const kRenderFlags[] = {
                 "--width",       "--height",     "--view-yaw",
                 "--view-pitch",  "--view-fov",   "--view-margin",
                 "--view-distance", "--view-target", "--view-near",
                 "--view-far"};
             for (const char* view : kRenderFlags) {
                 if (given(view)) {
                     std::cerr << "error: " << view
                               << " describes the render, which needs --out\n";
                     return 1;
                 }
             }
         }
         return run_track_whole(nfsu::argument_path(argv[2]),
                                nfsu::argument_path(argv[3]),
                                static_cast<std::uint32_t>(track), args);
     }},
    {"race-route", " <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>",
     "      [--path <FILE> | --drive <forward|backward> [--skip <node> <node>]]\n"
     "      (the route of the event's first race over the bundle it loads;\n"
     "       --path replays one position per line, slot0 and slot1, and\n"
     "       --drive replays the built-in drive of docs/RACE_ROUTE.md)\n",
     [](int argc, char** argv) {
         if (argc < 6) {
             print_usage();
             return 1;
         }
         RaceRouteArgs args;
         std::uint64_t event = 0;
         if (!parse_u64(argv[5], event) || event > 0xFFFFFFFFULL) {
             std::cerr << "error: the event must be a number\n";
             return 1;
         }
         args.event = static_cast<std::uint32_t>(event);
         for (int i = 6; i < argc; ++i) {
             const std::string option = argv[i];
             if (option == "--path" && i + 1 < argc && !args.path.has_value()) {
                 args.path = nfsu::argument_path(argv[++i]);
             } else if (option == "--drive" && i + 1 < argc && args.drive.empty()) {
                 args.drive = argv[++i];
                 if (args.drive != "forward" && args.drive != "backward") {
                     std::cerr << "error: --drive is forward or backward\n";
                     return 1;
                 }
             } else if (option == "--skip" && i + 2 < argc && args.skip_from < 0) {
                 std::uint64_t from = 0;
                 std::uint64_t to = 0;
                 if (!parse_u64(argv[i + 1], from) || !parse_u64(argv[i + 2], to) ||
                     from > 32767 || to > 32767) {
                     std::cerr << "error: --skip takes two node numbers\n";
                     return 1;
                 }
                 args.skip_from = static_cast<int>(from);
                 args.skip_to = static_cast<int>(to);
                 i += 2;
             } else {
                 std::cerr << "error: unexpected or repeated argument '" << option << "'\n";
                 return 1;
             }
         }
         if (args.path.has_value() && !args.drive.empty()) {
             std::cerr << "error: --path and --drive are two replays; give one\n";
             return 1;
         }
         if (args.skip_from >= 0 && args.drive.empty()) {
             std::cerr << "error: --skip needs --drive\n";
             return 1;
         }
         return run_race_route(nfsu::argument_path(argv[2]), nfsu::argument_path(argv[3]),
                               nfsu::argument_path(argv[4]), args);
     }},
    {"race-timing", " <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>",
     "      (--path <FILE> | --drive <forward|backward> [--skip <node> <node>])\n"
     "      (the race state machine over the event's first race, 60 steps a\n"
     "       second: the countdown, the race clock, split, lap and total times,\n"
     "       the finish and the result; the replay holds its first position\n"
     "       until the step after go. docs/RACE_TIMING.md)\n",
     [](int argc, char** argv) {
         if (argc < 6) {
             print_usage();
             return 1;
         }
         RaceRouteArgs args;
         std::uint64_t event = 0;
         if (!parse_u64(argv[5], event) || event > 0xFFFFFFFFULL) {
             std::cerr << "error: the event must be a number\n";
             return 1;
         }
         args.event = static_cast<std::uint32_t>(event);
         for (int i = 6; i < argc; ++i) {
             const std::string option = argv[i];
             if (option == "--path" && i + 1 < argc && !args.path.has_value()) {
                 args.path = nfsu::argument_path(argv[++i]);
             } else if (option == "--drive" && i + 1 < argc && args.drive.empty()) {
                 args.drive = argv[++i];
                 if (args.drive != "forward" && args.drive != "backward") {
                     std::cerr << "error: --drive is forward or backward\n";
                     return 1;
                 }
             } else if (option == "--skip" && i + 2 < argc && args.skip_from < 0) {
                 std::uint64_t from = 0;
                 std::uint64_t to = 0;
                 if (!parse_u64(argv[i + 1], from) || !parse_u64(argv[i + 2], to) ||
                     from > 32767 || to > 32767) {
                     std::cerr << "error: --skip takes two node numbers\n";
                     return 1;
                 }
                 args.skip_from = static_cast<int>(from);
                 args.skip_to = static_cast<int>(to);
                 i += 2;
             } else {
                 std::cerr << "error: unexpected or repeated argument '" << option << "'\n";
                 return 1;
             }
         }
         if (args.path.has_value() && !args.drive.empty()) {
             std::cerr << "error: --path and --drive are two replays; give one\n";
             return 1;
         }
         if (!args.path.has_value() && args.drive.empty()) {
             std::cerr << "error: race-timing replays a path file or a drive; give --path "
                          "or --drive\n";
             return 1;
         }
         if (args.skip_from >= 0 && args.drive.empty()) {
             std::cerr << "error: --skip needs --drive\n";
             return 1;
         }
         return run_race_timing(nfsu::argument_path(argv[2]), nfsu::argument_path(argv[3]),
                                nfsu::argument_path(argv[4]), args);
     }},
    {"track-stream-path",
     " <STREAM.BUN> <TRACKB####.lzc> <track-number>",
     "      (--path <FILE> | --hand-path 1003) [--speed <units/s>]\n"
     "      [--policy <retail|zone-only>] [--cost <table|prepared>]\n"
     "      [--budget <pool|switch-region|switch-arena|BYTES>]\n"
     "      [--pack <STREAM_NNNN.BUN>] [--texture <file>]...\n"
     "      [--list <events|all|none>] [--frame-every <N> --out <PREFIX>]\n"
     "      [--width <N>] [--height <N>] [--gpu-textures]\n"
     "      (runs the section streamer of speed.exe along a path, 60 steps\n"
     "       a second, and measures every resident set it meets; hand-1003\n"
     "       is a HAND-GIVEN path, not the game's route. --out writes\n"
     "       PREFIX-NNNNNN.tga every N samples and never replaces a file.\n"
     "       Every option is given at most once except --texture.)\n",
     [](int argc, char** argv) {
         if (argc < 5) {
             print_usage();
             return 1;
         }
         std::uint64_t track = 0;
         if (!parse_u64(std::string(argv[4]), track) || track == 0 ||
             track > 0x7FFFU) {
             std::cerr << "error: track-number must be a track number from 1"
                          " to 32767\n";
             return 1;
         }
         TrackStreamPathArgs args;
         static const char* const kFlags[] = {
             "--path",   "--hand-path", "--speed",       "--policy", "--cost",
             "--budget", "--pack",      "--texture",     "--list",
             "--frame-every", "--out",  "--width",       "--height", "--gpu-textures"};
         std::vector<std::string> seen;
         const auto given = [&seen](const char* flag) {
             return std::find(seen.begin(), seen.end(), flag) != seen.end();
         };
         for (int i = 5; i < argc; ++i) {
             const std::string flag = argv[i];
             if (std::find(std::begin(kFlags), std::end(kFlags), flag) ==
                 std::end(kFlags)) {
                 std::cerr << "error: unknown option " << flag << '\n';
                 return 1;
             }
             if (flag != "--texture" && given(flag.c_str())) {
                 std::cerr << "error: " << flag << " is given more than once\n";
                 return 1;
             }
             seen.push_back(flag);
             const bool has_value = i + 1 < argc;
             const std::string value = has_value ? argv[i + 1] : std::string();
             if (flag == "--gpu-textures") {
                 args.gpu_textures = true;
             } else if (flag == "--path") {
                 if (!has_value) {
                     std::cerr << "error: --path needs a path\n";
                     return 1;
                 }
                 args.path_file = nfsu::argument_path(argv[i + 1]);
                 args.has_path_file = true;
                 ++i;
             } else if (flag == "--hand-path") {
                 if (value != "1003") {
                     std::cerr << "error: --hand-path knows one path, 1003\n";
                     return 1;
                 }
                 args.has_hand_path = true;
                 ++i;
             } else if (flag == "--speed") {
                 double speed = 0.0;
                 if (!has_value || !parse_f64(value, speed) || !(speed > 0.0) ||
                     speed > kStreamPathMaxSpeed) {
                     std::cerr << "error: --speed needs a number above 0 and at"
                                  " most "
                               << format_double(kStreamPathMaxSpeed) << '\n';
                     return 1;
                 }
                 args.speed = speed;
                 ++i;
             } else if (flag == "--policy") {
                 if (value == "retail") {
                     args.policy = nfsu::TrackResidencyPolicy::retail;
                 } else if (value == "zone-only") {
                     args.policy = nfsu::TrackResidencyPolicy::zone_only;
                 } else {
                     std::cerr << "error: --policy must be retail or zone-only\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--cost") {
                 if (value == "table") {
                     args.cost = StreamPathCost::table;
                 } else if (value == "prepared") {
                     args.cost = StreamPathCost::prepared;
                 } else {
                     std::cerr << "error: --cost must be table or prepared\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--budget") {
                 std::uint64_t bytes = 0;
                 args.has_budget = true;
                 if (value == "pool") {
                     args.budget = StreamPathBudget::pool;
                 } else if (value == "switch-region") {
                     args.budget = StreamPathBudget::switch_region;
                 } else if (value == "switch-arena") {
                     args.budget = StreamPathBudget::switch_arena;
                 } else if (has_value && parse_u64(value, bytes)) {
                     args.budget = StreamPathBudget::bytes;
                     args.budget_bytes = bytes;
                 } else {
                     std::cerr << "error: --budget must be pool, switch-region,"
                                  " switch-arena or a byte count\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--pack") {
                 if (!has_value) {
                     std::cerr << "error: --pack needs a path\n";
                     return 1;
                 }
                 args.pack = nfsu::argument_path(argv[i + 1]);
                 args.has_pack = true;
                 ++i;
             } else if (flag == "--texture") {
                 if (!has_value) {
                     std::cerr << "error: --texture needs a path\n";
                     return 1;
                 }
                 args.textures.push_back(nfsu::argument_path(argv[i + 1]));
                 ++i;
             } else if (flag == "--list") {
                 if (value == "events") {
                     args.list = StreamPathList::events;
                 } else if (value == "all") {
                     args.list = StreamPathList::all;
                 } else if (value == "none") {
                     args.list = StreamPathList::none;
                 } else {
                     std::cerr << "error: --list must be events, all or none\n";
                     return 1;
                 }
                 ++i;
             } else if (flag == "--frame-every") {
                 std::uint64_t every = 0;
                 if (!has_value || !parse_u64(value, every) || every == 0 ||
                     every > 10000000U) {
                     std::cerr << "error: --frame-every needs a sample count"
                                  " from 1 to 10000000\n";
                     return 1;
                 }
                 args.frame_every = every;
                 args.has_frames = true;
                 ++i;
             } else if (flag == "--out") {
                 if (!has_value) {
                     std::cerr << "error: --out needs a path\n";
                     return 1;
                 }
                 args.output = nfsu::argument_path(argv[i + 1]);
                 args.has_output = true;
                 ++i;
             } else if (flag == "--width" || flag == "--height") {
                 std::uint64_t size = 0;
                 if (!has_value || !parse_u64(value, size) || size == 0 ||
                     size > 8192) {
                     std::cerr << "error: " << flag
                               << " needs a size from 1 to 8192\n";
                     return 1;
                 }
                 (flag == "--width" ? args.width : args.height) =
                     static_cast<std::uint32_t>(size);
                 ++i;
             } else {
                 // kFlags accepted it and no branch above claimed it.
                 std::cerr << "error: option " << flag
                           << " is listed and not handled\n";
                 return 1;
             }
         }
         if (args.has_path_file == args.has_hand_path) {
             std::cerr << "error: give exactly one of --path and --hand-path\n";
             return 1;
         }
         if (args.has_hand_path && track != 1003U) {
             std::cerr << "error: --hand-path 1003 is drawn for track 1003\n";
             return 1;
         }
         if (args.has_frames != args.has_output) {
             std::cerr << "error: --frame-every and --out are given together\n";
             return 1;
         }
         if (!args.has_output && (given("--width") || given("--height"))) {
             std::cerr << "error: --width and --height describe the frames,"
                          " which need --out\n";
             return 1;
         }
         return run_track_stream_path(nfsu::argument_path(argv[2]),
                                      nfsu::argument_path(argv[3]),
                                      static_cast<std::uint32_t>(track), args);
     }},
    {"part-markers", " <GEOMETRY.BIN> [role]", "",
     [](int argc, char** argv) {
         if (argc != 3 && argc != 4) {
             print_usage();
             return 1;
         }
         return run_part_markers(nfsu::argument_path(argv[2]),
                                 argc == 4 ? std::string(argv[3]) : "");
     }},
    {"body-skin-inputs", " <car-name> <option>...",
     "    --source <car-textures|car-vinyls|other> <FILE>   (repeatable,\n"
     "        in priority order; the first source claiming a hash wins)\n"
     "    --size <WIDTHxHEIGHT>   --target-texture <NAME>\n"
     "    --target <type32|type8>   --assemble\n"
     "    --hood <hood-spoiler|hood|spoiler>\n"
     "    --vinyl <0-3> <TEXTURE> <NUMCOLOURS> <remap|no-remap>\n"
     "    --vinyl-color <0-3> <0-3> <R> <G> <B> <GLOSS>\n"
     "    --doorline <plain|kit|widebody>\n"
     "    --base <R> <G> <B> <GLOSS>\n"
     "    --special <0-3> <R> <G> <B> <GLOSS>\n",
     [](int argc, char** argv) { return run_body_skin_inputs(argc, argv); }},
};

void print_usage() {
    std::cout << "Usage:\n";
    for (const Command& command : kCommands) {
        std::cout << "  nfsu_probe " << command.name << command.args << "\n"
                  << command.tail;
        // A `tail` whose last line is unterminated would otherwise leave the
        // next entry's usage line glued to the end of it, where nothing reads
        // it as a usage line any more and the entry drops out of the listing
        // without dropping out of the dispatch. Terminating it here is what
        // makes "one entry, one usage line of its own" hold for every entry
        // however its text was written, rather than for every entry whose
        // author remembered.
        if (!command.tail.empty() && command.tail.back() != '\n') {
            std::cout << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    const std::string_view command = argv[1];
    // Before the command runs, and before any of its arguments reaches a
    // file operation. Every argument goes through the same check, not only
    // the ones that become paths: the rest are command and option names,
    // decimal and hexadecimal numbers, and retail texture, mesh and car
    // names, and none of those has a spelling this refuses.
    // docs/PATH_ARGUMENTS.md says what is accepted and what is not.
    std::string argument_error;
    if (!nfsu::check_arguments(argc, argv, 1, argument_error)) {
        std::cerr << "error: " << argument_error << '\n';
        return 1;
    }
    for (const Command& entry : kCommands) {
        if (command == entry.name) {
            return entry.run(argc, argv);
        }
    }

    // Naming the command is what lets the usage check tell a command this
    // program dispatches from one it merely lists: every other refusal prints
    // the same usage text this one does, and says nothing else.
    std::cerr << "error: unknown command '" << command << "'\n";
    print_usage();
    return 1;
}
