#include "nfsu/geometry.h"
#include "nfsu/render.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Bounds on texture input held in memory, in addition to the 256 MiB per
// file limit of TextureLimits: at most this many --shared-textures files,
// and at most this many bytes read for the car's pack plus all of them.
constexpr std::size_t kMaxSharedTextureSources = 8;
constexpr std::uint64_t kMaxTextureInputBytes = 512ULL * 1024ULL * 1024ULL;

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> <out.tga> [options]\n"
        << "Texture sources, in priority order:\n"
        << "  <TEXTURES.BIN>       the car's own pack, always first\n"
        << "  --shared-textures F  a file of texture packs searched after it, for\n"
        << "                       example GLOBAL/GLOBALB.BUN; may be repeated, each\n"
        << "                       ranking below the ones before it. No file is\n"
        << "                       ever searched unless it is named here. At most\n"
        << "                       8 files; each at most 256 MiB, and at most\n"
        << "                       512 MiB read for <TEXTURES.BIN> and all of them.\n"
        << "Options (each at most once):\n"
        << "  --width N            1..8192, default 1280\n"
        << "  --height N           1..8192, default 720 (at most 16777216 pixels)\n"
        << "  --yaw DEG            -36000..36000, default 35 (0 = front, 90 = right side)\n"
        << "  --pitch DEG          -89..89, default 15 (positive = from above)\n"
        << "  --distance D         0.01..10000, default 7\n"
        << "  --target X,Y,Z       each -10000..10000, default 0.2,0.35,0\n"
        << "  --fov DEG            vertical field of view 1..170, default 35\n"
        << "  --near D             0.0001..10, default 0.05\n"
        << "  --far D              near..100000, default 100\n"
        << "  --flip-v             sample (u, 1 - v) instead of raw (u, v)\n"
        << "  --alpha-cutoff N     0..255, default 0 (decoded RGB drawn opaquely);\n"
        << "                       a nonzero cutoff discards texels with lower\n"
        << "                       alpha and is diagnostic only (e.g. 128)\n"
        << "  --vertex-color       multiply by vertex color bytes 0..2\n"
        << "  --cull MODE          none (default), back or front\n"
        << "  --shading MODE       flat (default) or none\n"
        << "Retail material state (docs/MATERIALS.md). Every option below\n"
        << "the first one needs --material-state retail:\n"
        << "  --material-state M   off (default) or retail. retail applies the\n"
        << "                       texture-info alpha test and blend selection,\n"
        << "                       the coefficient gates and the bounded\n"
        << "                       transparent draw order\n"
        << "  --material-alpha M,R the undecoded diffuse alpha lane, each\n"
        << "                       0..16, default 1,0, which leaves the source\n"
        << "                       alpha equal to the texel alpha\n"
        << "  --environment M,R,P  EnvmapMin and EnvmapRange, each 0..16 and\n"
        << "                       applied to all three channels, and a whole\n"
        << "                       EnvmapPower 0..64; default 0,0,1 = no term\n"
        << "  --environment-color R,G,B  stands in for a cube-map sample,\n"
        << "                       each 0..255, default 0,0,0\n"
        << "  --environment-global S  on (default) or off, the runtime global\n"
        << "                       the coefficient upload also tests.\n"
        << "                       No value here is a retail value: the retail\n"
        << "                       coefficients are not decoded.\n"
        << "Mesh selection (repeatable, may be mixed; default: all meshes):\n"
        << "  --mesh-index N       the mesh at source position N\n"
        << "  --mesh-name NAME     the one mesh whose stored name is exactly NAME\n"
        << "  --mesh-hash HEX      the one mesh with this verified directory hash\n"
        << "                       Selected meshes are drawn in source order. At\n"
        << "                       most 256 selectors and 16384 name bytes. A\n"
        << "                       selection is a diagnostic subset, not a decoded\n"
        << "                       game car assembly.\n";
}

// Printable ASCII; quote, backslash and other bytes as \xHH.
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20U && byte <= 0x7EU && ch != '"' && ch != '\\') {
            out += ch;
        } else {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\x%02X", static_cast<unsigned int>(byte));
            out += buffer;
        }
    }
    return out + "\"";
}

bool parse_double(const std::string& text, double& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return !text.empty() && result.ec == std::errc{} && result.ptr == end &&
           std::isfinite(value);
}

bool parse_uint(const std::string& text, std::uint64_t max, std::uint64_t& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, 10);
    return !text.empty() && result.ec == std::errc{} && result.ptr == end &&
           value <= max;
}

bool parse_hex32(const char* text, std::uint32_t& value) {
    const std::size_t length = std::strlen(text);
    const char* begin = text;
    if (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        begin += 2;
    }
    const char* end = text + length;
    if (begin == end || end - begin > 8) {
        return false;
    }
    const auto result = std::from_chars(begin, end, value, 16);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_pair(const std::string& text, double& first, double& second) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    return parse_double(text.substr(0, comma), first) &&
           parse_double(text.substr(comma + 1), second);
}

bool parse_vec3(const std::string& text, nfsu::Vec3& value) {
    const std::size_t first = text.find(',');
    if (first == std::string::npos) {
        return false;
    }
    const std::size_t second = text.find(',', first + 1);
    if (second == std::string::npos || text.find(',', second + 1) != std::string::npos) {
        return false;
    }
    return parse_double(text.substr(0, first), value.x) &&
           parse_double(text.substr(first + 1, second - first - 1), value.y) &&
           parse_double(text.substr(second + 1), value.z);
}

std::string format_double(double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

std::string format_rgba(const unsigned char (&rgba)[4]) {
    return std::to_string(rgba[0]) + "," + std::to_string(rgba[1]) + "," +
           std::to_string(rgba[2]) + "," + std::to_string(rgba[3]);
}

std::string hex32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned int>(value));
    return buffer;
}

// Returns 0 on success, otherwise the exit code after printing an error.
int parse_options(
    int argc,
    char** argv,
    nfsu::RenderOptions& options,
    std::vector<fs::path>& shared,
    nfsu::MeshSelection& selection) {
    const nfsu::RenderLimits limits;
    const nfsu::SceneLimits scene_limits;
    std::size_t selector_text = 0;
    std::set<std::string> seen;
    for (int i = 4; i < argc; ++i) {
        const std::string name = argv[i];
        if (name == "--mesh-index" || name == "--mesh-name" ||
            name == "--mesh-hash") {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << name << '\n';
                return 1;
            }
            // Both bounds are checked before the selector is stored.
            if (selection.selectors.size() >= scene_limits.max_selectors) {
                std::cerr << "error: at most " << scene_limits.max_selectors
                          << " mesh selectors are allowed\n";
                return 1;
            }
            const char* value = argv[++i];
            nfsu::MeshSelector selector;
            if (name == "--mesh-index") {
                std::uint64_t index = 0;
                // At most 10 digits fit below 2^32; longer text is refused
                // before it is copied.
                if (std::strlen(value) > 10 || !parse_uint(value, UINT32_MAX, index)) {
                    std::cerr << "error: invalid value for " << name << '\n';
                    return 1;
                }
                selector.kind = nfsu::MeshSelectorKind::index;
                selector.index = index;
            } else if (name == "--mesh-name") {
                const std::size_t length = std::strlen(value);
                if (length > scene_limits.max_selector_text_bytes - selector_text) {
                    std::cerr << "error: mesh names exceed "
                              << scene_limits.max_selector_text_bytes << " bytes\n";
                    return 1;
                }
                selector_text += length;
                selector.kind = nfsu::MeshSelectorKind::name;
                selector.name.assign(value, length);
            } else {
                std::uint32_t hash = 0;
                if (!parse_hex32(value, hash)) {
                    std::cerr << "error: invalid value for " << name << '\n';
                    return 1;
                }
                selector.kind = nfsu::MeshSelectorKind::hash;
                selector.hash = hash;
            }
            selection.selectors.push_back(std::move(selector));
            continue;
        }
        if (name == "--shared-textures") {
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << name << '\n';
                return 1;
            }
            if (shared.size() >= kMaxSharedTextureSources) {
                std::cerr << "error: at most " << kMaxSharedTextureSources
                          << " --shared-textures files are allowed\n";
                return 1;
            }
            shared.emplace_back(argv[++i]);
            continue;
        }
        if (!seen.insert(name).second) {
            std::cerr << "error: option given twice: " << name << '\n';
            return 1;
        }
        if (name == "--flip-v") {
            options.flip_v = true;
            continue;
        }
        if (name == "--vertex-color") {
            options.vertex_color = true;
            continue;
        }
        static const std::set<std::string> valued = {
            "--width", "--height", "--alpha-cutoff", "--yaw", "--pitch",
            "--distance", "--fov", "--near", "--far", "--target", "--cull",
            "--shading", "--material-state", "--material-alpha", "--environment",
            "--environment-color", "--environment-global"};
        if (valued.count(name) == 0) {
            std::cerr << "error: unknown option " << name << '\n';
            return 1;
        }
        if (i + 1 >= argc) {
            std::cerr << "error: missing value for " << name << '\n';
            return 1;
        }
        const std::string value = argv[++i];
        bool ok = true;
        std::uint64_t integer = 0;
        if (name == "--width") {
            ok = parse_uint(value, limits.max_width, integer) && integer > 0;
            options.width = static_cast<std::uint32_t>(integer);
        } else if (name == "--height") {
            ok = parse_uint(value, limits.max_height, integer) && integer > 0;
            options.height = static_cast<std::uint32_t>(integer);
        } else if (name == "--alpha-cutoff") {
            ok = parse_uint(value, 255, integer);
            options.alpha_cutoff = static_cast<std::uint8_t>(integer);
        } else if (name == "--yaw") {
            ok = parse_double(value, options.camera.yaw_deg);
        } else if (name == "--pitch") {
            ok = parse_double(value, options.camera.pitch_deg);
        } else if (name == "--distance") {
            ok = parse_double(value, options.camera.distance);
        } else if (name == "--fov") {
            ok = parse_double(value, options.camera.fov_y_deg);
        } else if (name == "--near") {
            ok = parse_double(value, options.camera.near_plane);
        } else if (name == "--far") {
            ok = parse_double(value, options.camera.far_plane);
        } else if (name == "--target") {
            ok = parse_vec3(value, options.camera.target);
        } else if (name == "--cull") {
            if (value == "none") {
                options.cull = nfsu::CullMode::none;
            } else if (value == "back") {
                options.cull = nfsu::CullMode::back;
            } else if (value == "front") {
                options.cull = nfsu::CullMode::front;
            } else {
                ok = false;
            }
        } else if (name == "--shading") {
            if (value == "flat") {
                options.shading = nfsu::ShadingMode::flat;
            } else if (value == "none") {
                options.shading = nfsu::ShadingMode::none;
            } else {
                ok = false;
            }
        } else if (name == "--material-state") {
            if (value == "off") {
                options.material_state = nfsu::MaterialStateMode::off;
            } else if (value == "retail") {
                options.material_state = nfsu::MaterialStateMode::retail;
            } else {
                ok = false;
            }
        } else if (name == "--material-alpha") {
            ok = parse_pair(value, options.coefficients.alpha_min,
                            options.coefficients.alpha_range);
        } else if (name == "--environment") {
            nfsu::Vec3 parsed;
            ok = parse_vec3(value, parsed) && parsed.z >= 0.0 &&
                 parsed.z <= static_cast<double>(nfsu::kMaxEnvironmentPower) &&
                 parsed.z == std::floor(parsed.z);
            if (ok) {
                for (int k = 0; k < 3; ++k) {
                    options.coefficients.environment_min[k] = parsed.x;
                    options.coefficients.environment_range[k] = parsed.y;
                }
                options.coefficients.environment_power =
                    static_cast<std::uint32_t>(parsed.z);
            }
        } else if (name == "--environment-color") {
            nfsu::Vec3 parsed;
            ok = parse_vec3(value, parsed);
            const double channels[3] = {parsed.x, parsed.y, parsed.z};
            for (int k = 0; ok && k < 3; ++k) {
                ok = channels[k] >= 0.0 && channels[k] <= 255.0 &&
                     channels[k] == std::floor(channels[k]);
                if (ok) {
                    options.coefficients.environment_color[k] =
                        static_cast<unsigned char>(channels[k]);
                }
            }
        } else if (name == "--environment-global") {
            if (value == "on") {
                options.coefficients.environment_enabled = true;
            } else if (value == "off") {
                options.coefficients.environment_enabled = false;
            } else {
                ok = false;
            }
        }
        if (!ok) {
            std::cerr << "error: invalid value for " << name << ": " << value << '\n';
            return 1;
        }
    }
    // A coefficient without the mode would be a silent no-op.
    if (options.material_state == nfsu::MaterialStateMode::off) {
        static const std::set<std::string> retail_only = {
            "--material-alpha", "--environment", "--environment-color",
            "--environment-global"};
        for (const std::string& option : retail_only) {
            if (seen.count(option) != 0) {
                std::cerr << "error: " << option
                          << " needs --material-state retail\n";
                return 1;
            }
        }
    }
    std::string coefficient_error;
    if (options.material_state == nfsu::MaterialStateMode::retail &&
        !nfsu::validate_material_coefficients(options.coefficients,
                                              coefficient_error)) {
        std::cerr << "error: " << coefficient_error << '\n';
        return 1;
    }
    if (std::uint64_t{options.width} * options.height > limits.max_pixels) {
        std::cerr << "error: width * height exceeds " << limits.max_pixels
                  << " pixels\n";
        return 1;
    }
    std::string error;
    if (!nfsu::validate_camera(options.camera, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    return 0;
}

void print_stats(const nfsu::RenderStats& s) {
    std::cout << "meshes: " << s.meshes << " rendered " << s.meshes_rendered
              << " skipped_invalid " << s.meshes_skipped_invalid << '\n';
    std::cout << "material groups: " << s.material_groups << " resolved "
              << s.groups_resolved << " unresolved " << s.groups_unresolved
              << " unusable " << s.groups_unusable << " invalid "
              << s.groups_invalid << '\n';
    std::cout << "unresolved hashes: " << s.unresolved_hashes << '\n';
    std::cout << "triangles submitted: " << s.triangles_submitted
              << " textured " << s.triangles_resolved << " fallback "
              << s.triangles_fallback << '\n';
    std::cout << "triangles bad_index: " << s.triangles_bad_index
              << " degenerate_source: " << s.triangles_degenerate_source << '\n';
    std::cout << "triangles outside: " << s.triangles_outside
              << " clipped: " << s.triangles_clipped
              << " degenerate_screen: " << s.triangles_degenerate_screen
              << " culled: " << s.triangles_culled << '\n';
    std::cout << "triangles front: " << s.triangles_front << " back: "
              << s.triangles_back << " rasterized: " << s.triangles_rasterized
              << " raster_pieces: " << s.raster_triangles << '\n';
    std::cout << "pixel tests: " << s.pixel_tests << '\n';
    std::cout << "fragments: " << s.fragments << " alpha_discarded "
              << s.fragments_alpha_discarded << " far_discarded "
              << s.fragments_far_discarded << " depth_failed "
              << s.fragments_depth_failed << " written " << s.fragments_written
              << '\n';
    std::cout << "pixels drawn: " << s.pixels_drawn << " fallback "
              << s.pixels_fallback << '\n';
    if (s.pixels_drawn > 0) {
        std::cout << "drawn bounds: x " << s.min_x << ".." << s.max_x << " y "
                  << s.min_y << ".." << s.max_y << '\n';
    } else {
        std::cout << "drawn bounds: none\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        print_usage();
        return 1;
    }
    const fs::path geometry_path(argv[1]);
    const fs::path texture_path(argv[2]);
    const fs::path output_path(argv[3]);

    nfsu::RenderOptions options;
    std::vector<fs::path> shared_paths;
    nfsu::MeshSelection selection;
    const int parse_result = parse_options(argc, argv, options, shared_paths, selection);
    if (parse_result != 0) {
        return parse_result;
    }
    std::vector<fs::path> inputs = {geometry_path, texture_path};
    inputs.insert(inputs.end(), shared_paths.begin(), shared_paths.end());

    // Fail early; write_frame_tga repeats these checks before writing.
    {
        std::error_code error;
        fs::path partial = output_path;
        partial += ".partial";
        for (const fs::path& source : inputs) {
            if (fs::equivalent(source, output_path, error) ||
                fs::equivalent(source, partial, error)) {
                std::cerr << "error: output path must differ from every input file\n";
                return 1;
            }
        }
        // A texture source named twice would only shadow itself.
        for (std::size_t i = 0; i < shared_paths.size(); ++i) {
            for (std::size_t j = 1; j < i + 2; ++j) {
                if (fs::equivalent(shared_paths[i], inputs[j], error)) {
                    std::cerr << "error: shared texture source " << (i + 1)
                              << " names the same file as texture source "
                              << (j - 1) << '\n';
                    return 1;
                }
            }
        }
        if (fs::exists(fs::symlink_status(output_path, error))) {
            std::cerr << "error: refusing to overwrite the output file\n";
            return 1;
        }
    }

    const nfsu::GeometryReport geometry = nfsu::load_geometry_file(geometry_path);
    for (const nfsu::GeometryIssue& issue : geometry.issues) {
        std::cerr << "geometry issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    // Selection errors are usage errors, found before any texture is read.
    const nfsu::SceneLimits scene_limits;
    std::vector<std::size_t> selected;
    {
        std::string error;
        if (!nfsu::resolve_mesh_selection(geometry, selection, scene_limits, selected,
                                          nullptr, error)) {
            std::cerr << "error: mesh selection: " << error << '\n';
            return 1;
        }
    }
    std::vector<unsigned char> bytes;
    const nfsu::TexturePack pack = nfsu::load_texture_pack(texture_path, bytes);
    for (const nfsu::TextureIssue& issue : pack.issues) {
        std::cerr << "texture issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    // Shared source k is texture source k; the car's pack is source 0.
    // Both vectors are sized once, so the pointers below stay valid.
    std::vector<std::vector<unsigned char>> shared_bytes(shared_paths.size());
    std::vector<nfsu::TexturePackSet> shared_sets(shared_paths.size());
    // Each file is refused before it is read if it would take the bytes
    // read so far past kMaxTextureInputBytes. The car's pack is at most
    // 256 MiB, so read_bytes never exceeds the limit.
    std::uint64_t read_bytes = bytes.size();
    for (std::size_t i = 0; i < shared_paths.size(); ++i) {
        nfsu::TextureLimits source_limits;
        const std::uint64_t remaining = kMaxTextureInputBytes - read_bytes;
        source_limits.max_file_size = std::min(source_limits.max_file_size, remaining);
        shared_sets[i] =
            nfsu::load_texture_pack_set(shared_paths[i], shared_bytes[i], source_limits);
        read_bytes += shared_bytes[i].size();
        const nfsu::TexturePackSet& set = shared_sets[i];
        if (shared_bytes[i].empty() && set.file_size > remaining &&
            remaining < nfsu::TextureLimits{}.max_file_size) {
            std::cerr << "texture source " << (i + 1) << " not read: "
                      << (kMaxTextureInputBytes - remaining)
                      << " texture input bytes already read, total limit is "
                      << kMaxTextureInputBytes << '\n';
        }
        for (const nfsu::TextureIssue& issue : set.issues) {
            std::cerr << "texture source " << (i + 1) << " issue at "
                      << issue.offset << ": " << issue.message << '\n';
        }
        for (std::size_t p = 0; p < set.packs.size(); ++p) {
            for (const nfsu::TextureIssue& issue : set.packs[p].issues) {
                std::cerr << "texture source " << (i + 1) << " pack " << p
                          << " issue at " << issue.offset << ": "
                          << issue.message << '\n';
            }
        }
    }
    if (geometry.valid_mesh_count() == 0) {
        std::cerr << "error: no valid meshes to render\n";
        return 2;
    }
    nfsu::RenderTextureSet textures;
    std::vector<std::uint32_t> wanted;
    {
        std::string error;
        bool decoded_ok = false;
        if (shared_paths.empty()) {
            decoded_ok = nfsu::decode_render_textures(
                bytes, pack, nfsu::RenderTextureLimits{}, textures, error);
        } else {
            // Shared sources decode only hashes that a selected mesh uses,
            // so unrelated textures do not count against the shared budget.
            // Without selectors these are all valid meshes.
            for (const std::size_t index : selected) {
                const nfsu::Mesh& mesh = geometry.meshes[index];
                for (const nfsu::MeshMaterial& material : mesh.materials) {
                    if (material.texture_slot < mesh.texture_hashes.size()) {
                        wanted.push_back(mesh.texture_hashes[material.texture_slot]);
                    }
                }
            }
            std::sort(wanted.begin(), wanted.end());
            wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

            std::vector<nfsu::RenderTextureSource> sources(1 + shared_paths.size());
            sources[0].bytes = &bytes;
            // A pack that could not be read has no records to offer.
            if (pack.parse_source == bytes.data() && pack.file_size == bytes.size()) {
                sources[0].packs.push_back(&pack);
            }
            sources[0].complete = pack.tables_parsed;
            for (std::size_t i = 0; i < shared_paths.size(); ++i) {
                nfsu::RenderTextureSource& source = sources[i + 1];
                source.bytes = &shared_bytes[i];
                for (const nfsu::TexturePack& shared_pack : shared_sets[i].packs) {
                    source.packs.push_back(&shared_pack);
                }
                source.complete = shared_sets[i].complete;
                source.wanted = &wanted;
            }
            decoded_ok = nfsu::decode_render_texture_sources(
                sources, nfsu::RenderTextureLimits{}, textures, error);
        }
        if (!decoded_ok) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
    }
    for (const nfsu::RenderTexture& texture : textures.textures) {
        if (!texture.valid && !texture.problem.empty()) {
            if (texture.source != 0) {
                std::cerr << "texture source " << texture.source << " pack "
                          << texture.pack << " texture " << texture.record << ' ';
            }
            std::cerr << "texture " << std::hex << texture.hash << std::dec
                      << " drawn with fallback: " << texture.problem << '\n';
        }
    }
    std::size_t decoded = 0;
    std::uint64_t decoded_bytes = 0;
    for (const nfsu::RenderTexture& texture : textures.textures) {
        if (texture.valid && texture.source == 0) {
            ++decoded;
            decoded_bytes += texture.rgba.size();
        }
    }

    // Material lookup happens here, once; the renderer reads the scene only.
    nfsu::PreparedCarScene scene;
    std::string error;
    if (!nfsu::prepare_car_scene(geometry, textures, selection, scene_limits, scene,
                                 error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    nfsu::Frame frame;
    nfsu::RenderStats stats;
    const nfsu::RenderLimits limits;
    if (!nfsu::render_scene(scene, options, limits, frame, stats, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    nfsu::FrameWriteResult write;
    if (!nfsu::write_frame_tga(frame, output_path, inputs, write)) {
        std::cerr << "error: " << write.error << '\n';
        return 2;
    }

    const nfsu::Vec3 eye = nfsu::orbit_eye(options.camera);
    std::cout << "geometry: bytes " << geometry.file_size << " meshes "
              << geometry.meshes.size() << " valid "
              << geometry.valid_mesh_count() << " issues "
              << geometry.issues.size() << '\n';
    std::cout << "textures: bytes " << pack.file_size << " records "
              << pack.textures.size() << " valid " << pack.valid_texture_count()
              << " decoded " << decoded << " decoded_bytes "
              << decoded_bytes << " issues " << pack.issues.size()
              << '\n';
    bool sources_ok = true;
    if (!shared_paths.empty()) {
        for (std::size_t i = 0; i < shared_sets.size(); ++i) {
            const nfsu::TexturePackSet& set = shared_sets[i];
            std::size_t records = 0;
            std::size_t valid = 0;
            std::size_t issues = set.issues.size();
            for (const nfsu::TexturePack& shared_pack : set.packs) {
                records += shared_pack.textures.size();
                valid += shared_pack.valid_texture_count();
                issues += shared_pack.issues.size();
            }
            std::size_t source_decoded = 0;
            std::uint64_t source_bytes = 0;
            for (const nfsu::RenderTexture& texture : textures.textures) {
                if (texture.valid && texture.source == i + 1) {
                    ++source_decoded;
                    source_bytes += texture.rgba.size();
                }
            }
            sources_ok = sources_ok && set.ok();
            std::cout << "texture source " << (i + 1) << ": bytes " << set.file_size
                      << " packs " << set.packs.size() << " records " << records
                      << " valid " << valid << " decoded " << source_decoded
                      << " decoded_bytes " << source_bytes << " issues " << issues
                      << " complete " << (set.complete ? 1 : 0) << '\n';
        }
        std::cout << "texture sources: decoded_bytes " << textures.decoded_bytes
                  << " wanted_hashes " << wanted.size() << " not_wanted "
                  << textures.not_wanted << " shadowed " << textures.shadowed.size()
                  << " blocked " << textures.blocked_sources.size() << '\n';
        for (const nfsu::ShadowedTexture& shadow : textures.shadowed) {
            std::cout << "shadowed: hash " << hex32(shadow.hash) << " source "
                      << shadow.source << " pack " << shadow.pack << " texture "
                      << shadow.record << " by source " << shadow.by_source << '\n';
        }
        for (const nfsu::UnusableClaim& claim : textures.unusable_claims) {
            std::cout << "unusable claim: hash " << hex32(claim.hash) << " source "
                      << claim.source << " pack " << claim.pack << " texture "
                      << claim.record << '\n';
        }
        for (const std::size_t blocked : textures.blocked_sources) {
            std::cout << "blocked: source " << blocked
                      << " (an earlier source is incomplete)\n";
        }
    }
    std::cout << "frame: " << frame.width() << 'x' << frame.height()
              << " background " << format_rgba(options.background) << '\n';
    std::cout << "camera: yaw " << format_double(options.camera.yaw_deg)
              << " pitch " << format_double(options.camera.pitch_deg)
              << " distance " << format_double(options.camera.distance)
              << " target " << format_double(options.camera.target.x) << ','
              << format_double(options.camera.target.y) << ','
              << format_double(options.camera.target.z) << " fov_y "
              << format_double(options.camera.fov_y_deg) << " near "
              << format_double(options.camera.near_plane) << " far "
              << format_double(options.camera.far_plane) << '\n';
    std::cout << "eye: " << format_double(eye.x) << ',' << format_double(eye.y)
              << ',' << format_double(eye.z) << '\n';
    std::cout << "options: flip_v " << (options.flip_v ? 1 : 0)
              << " alpha_cutoff " << static_cast<unsigned int>(options.alpha_cutoff)
              << " vertex_color " << (options.vertex_color ? 1 : 0) << " cull "
              << nfsu::cull_mode_name(options.cull) << " shading "
              << nfsu::shading_mode_name(options.shading) << '\n';
    // Printed only with the retail state, so the report of every other run
    // is byte for byte what it was before this option existed. The same
    // rule already applies to the selection block below.
    if (options.material_state == nfsu::MaterialStateMode::retail) {
        const nfsu::MaterialCoefficients& c = options.coefficients;
        std::cout << "material: state "
                  << nfsu::material_state_mode_name(options.material_state)
                  << " alpha_lane " << format_double(c.alpha_min) << ','
                  << format_double(c.alpha_range) << " environment "
                  << format_double(c.environment_min[0]) << ','
                  << format_double(c.environment_range[0]) << ','
                  << c.environment_power << " environment_color "
                  << static_cast<unsigned int>(c.environment_color[0]) << ','
                  << static_cast<unsigned int>(c.environment_color[1]) << ','
                  << static_cast<unsigned int>(c.environment_color[2])
                  << " environment_global " << (c.environment_enabled ? "on" : "off")
                  << '\n';
    }
    // The all-meshes report is unchanged; only an explicit subset is listed.
    if (scene.profile == nfsu::SelectionProfile::explicit_subset) {
        std::cout << "selection: " << nfsu::selection_profile_name(scene.profile)
                  << ", " << scene.meshes.size() << " of " << scene.source_mesh_count
                  << " meshes, " << scene.selectors.size() << " selectors\n";
        for (const nfsu::PreparedMesh& mesh : scene.meshes) {
            const nfsu::MeshSelector& selector = scene.selectors[mesh.selector];
            std::cout << "selected: mesh " << mesh.source_index << " name "
                      << quoted(mesh.name) << " by selector " << mesh.selector << " ("
                      << nfsu::mesh_selector_kind_name(selector.kind) << ")\n";
        }
    }
    print_stats(stats);
    if (options.material_state == nfsu::MaterialStateMode::retail) {
        // The rows the renderer itself assigned, so the report can never
        // describe an order other than the one that was drawn.
        std::cout << "draw rows:";
        for (int row = 0; row < nfsu::kRenderDrawLayerCount; ++row) {
            std::cout << ' ' << stats.draw_rows[row];
        }
        std::cout << '\n';
        std::cout << "material groups: alpha_tested " << stats.groups_alpha_tested
                  << " blended " << stats.groups_blended << '\n';
        std::cout << "blend: fragments " << stats.fragments_blended << " overlap "
                  << stats.fragments_blend_overlap << " pixels "
                  << stats.pixels_blended << '\n';
    }
    std::cout << "wrote: " << output_path.filename().u8string() << '\n';
    return geometry.ok() && pack.ok() && sources_ok ? 0 : 3;
}
