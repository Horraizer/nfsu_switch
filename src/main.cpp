#include "nfsu/arg_path.h"
#include "nfsu/car_parts.h"
#include "nfsu/chunk.h"
#include "nfsu/geometry.h"
#include "nfsu/part_placement.h"
#include "nfsu/skin_inputs.h"
#include "nfsu/stock_scene.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"
#include "nfsu/track.h"
#include "nfsu/track_collision.h"
#include "nfsu/track_draw_list.h"
#include "nfsu/track_objects.h"
#include "nfsu/track_scene.h"
#include "nfsu/track_visibility.h"

#include <algorithm>
#include <array>
#include <cctype>
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

struct TrackVisibilityArgs {
    nfsu::TrackCamera camera;
    nfsu::TrackCullOptions options;
    std::size_t list_limit = kTrackVisibleListLimit;
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
        nfsu::cull_track_region(objects, frustum, args.options);

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
bool decode_track_scene_textures(
    const std::vector<fs::path>& paths,
    const std::vector<std::uint32_t>& wanted,
    std::vector<std::vector<unsigned char>>& source_bytes,
    std::vector<nfsu::TexturePackSet>& sets,
    std::vector<nfsu::RenderTextureSource>& sources,
    nfsu::RenderTextureSet& textures) {
    source_bytes.resize(paths.size());
    sets.resize(paths.size());
    sources.resize(paths.size());
    std::uint64_t read_bytes = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        nfsu::TextureLimits limits;
        const std::uint64_t remaining = kMaxTrackTextureInputBytes - read_bytes;
        limits.max_file_size = std::min(limits.max_file_size, remaining);
        sets[i] = nfsu::load_texture_pack_set(paths[i], source_bytes[i], limits);
        read_bytes += source_bytes[i].size();
        std::cout << "texture source " << i << ": " << paths[i].string()
                  << " size " << sets[i].file_size << " read "
                  << source_bytes[i].size() << " packs " << sets[i].packs.size()
                  << " complete " << (sets[i].complete ? 1 : 0) << '\n';
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

int run_track_scene(
    const fs::path& path,
    const nfsu::TrackRegionSelector& geometry_region,
    const nfsu::TrackRegionSelector& object_region,
    const TrackSceneArgs& args) {
    std::vector<unsigned char> geometry_bytes;
    const nfsu::TrackRegionReport geometry =
        nfsu::load_track_region(path, geometry_region, geometry_bytes);
    std::vector<unsigned char> object_bytes;
    const nfsu::TrackObjectReport objects =
        nfsu::load_track_objects(path, object_region, object_bytes);

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

    const std::vector<std::uint32_t> wanted =
        nfsu::track_region_texture_hashes(geometry);
    std::vector<std::vector<unsigned char>> source_bytes;
    std::vector<nfsu::TexturePackSet> sets;
    std::vector<nfsu::RenderTextureSource> sources;
    nfsu::RenderTextureSet textures;
    if (!decode_track_scene_textures(args.textures, wanted, source_bytes, sets,
                                     sources, textures)) {
        return 2;
    }
    std::cout << "textures: wanted " << wanted.size() << " decoded "
              << textures.textures.size() << " bytes " << textures.decoded_bytes
              << '\n';

    nfsu::PreparedTrackScene scene;
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

    nfsu::TrackFrustum frustum;
    if (!nfsu::track_frustum_look_at(args.camera, frustum)) {
        std::cerr << "error: the camera is degenerate and builds no frustum\n";
        return 1;
    }
    if (args.has_camera) {
        std::cout << "camera: world eye " << args.camera.eye[0] << ' '
                  << args.camera.eye[1] << ' ' << args.camera.eye[2]
                  << " target " << args.camera.target[0] << ' '
                  << args.camera.target[1] << ' ' << args.camera.target[2]
                  << " far " << args.camera.far_plane << '\n';
    } else {
        std::cout << "camera: none given; the fallback policy consults no"
                     " frustum\n";
    }

    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(objects, frustum, args.cull);
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

    nfsu::TrackDrawList list;
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

    std::size_t listed = 0;
    for (const nfsu::TrackDrawListEntry& entry : list.entries) {
        if (listed >= args.list_limit) {
            std::cout << "  ... " << (list.entries.size() - listed)
                      << " more entries not listed\n";
            break;
        }
        ++listed;
        const nfsu::PreparedTrackInstance& placed = scene.instances[entry.instance];
        const nfsu::TrackDraw& draw = scene.region.draws[entry.draw];
        std::cout << "  instance " << entry.instance << " section "
                  << placed.section_index << " source " << placed.instance_index
                  << " mesh " << placed.mesh << " draw " << entry.draw
                  << " indices " << draw.index_count << " texture "
                  << hex32(draw.texture_hash) << '\n';
    }

    return (geometry.ok() && objects.ok() && cull.ok()) ? 0 : 3;
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
     "      [--mask <0xNN>] [--screen-scale <s>] [--list <count>]\n"
     "      (eye and target are in the world frame of docs/FORMATS.md:286)\n",
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
