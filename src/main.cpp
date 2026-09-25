#include "nfsu/arg_path.h"
#include "nfsu/car_parts.h"
#include "nfsu/chunk.h"
#include "nfsu/geometry.h"
#include "nfsu/part_placement.h"
#include "nfsu/race_events.h"
#include "nfsu/skin_inputs.h"
#include "nfsu/stock_scene.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"
#include "nfsu/texture_export.h"
#include "nfsu/track.h"
#include "nfsu/track_budget.h"
#include "nfsu/track_collision.h"
#include "nfsu/track_draw_list.h"
#include "nfsu/track_objects.h"
#include "nfsu/track_probe.h"
#include "nfsu/track_render.h"
#include "nfsu/track_scene.h"
#include "nfsu/track_visibility.h"
#include "nfsu/track_zones.h"
#include "nfsu/vehicle.h"

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
