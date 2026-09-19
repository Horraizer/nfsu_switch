#include "nfsu/car_parts.h"
#include "nfsu/chunk.h"
#include "nfsu/geometry.h"
#include "nfsu/texture.h"

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

std::string hex32(std::uint32_t value);

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

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  nfsu_probe summary <game-root>\n"
        << "  nfsu_probe chunks <file> [display-limit]\n"
        << "  nfsu_probe meshes <GEOMETRY.BIN>\n"
        << "  nfsu_probe obj <GEOMETRY.BIN> <output.obj>\n"
        << "  nfsu_probe textures <TEXTURES.BIN>\n"
        << "  nfsu_probe pack-set <file>   (metadata of every texture pack, e.g. in a BUN)\n"
        << "  nfsu_probe export-textures <TEXTURES.BIN> <output-dir> [tga|dds]\n"
        << "  nfsu_probe texture-bindings <GEOMETRY.BIN> <TEXTURES.BIN>\n"
        << "  nfsu_probe car-parts <GLOBALB.BUN> [car-type]\n"
        << "  nfsu_probe stock-parts <GLOBALB.BUN> <car-type> [GEOMETRY.BIN ...]\n";
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
    if (command == "meshes") {
        return run_meshes(fs::path(argv[2]));
    }
    if (command == "obj") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        return run_obj(fs::path(argv[2]), fs::path(argv[3]));
    }
    if (command == "textures") {
        return run_textures(fs::path(argv[2]));
    }
    if (command == "pack-set") {
        return run_pack_set(fs::path(argv[2]));
    }
    if (command == "export-textures") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        return run_export_textures(fs::path(argv[2]), fs::path(argv[3]),
                                   argc >= 5 ? std::string(argv[4]) : "tga");
    }
    if (command == "texture-bindings") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        return run_texture_bindings(fs::path(argv[2]), fs::path(argv[3]));
    }
    if (command == "car-parts") {
        if (argc > 4) {
            print_usage();
            return 1;
        }
        return run_car_parts(fs::path(argv[2]), argc == 4 ? std::string(argv[3]) : "");
    }
    if (command == "stock-parts") {
        if (argc < 4) {
            print_usage();
            return 1;
        }
        std::vector<fs::path> geometry;
        for (int i = 4; i < argc; ++i) {
            geometry.emplace_back(argv[i]);
        }
        return run_stock_parts(fs::path(argv[2]), std::string(argv[3]), geometry);
    }

    print_usage();
    return 1;
}

