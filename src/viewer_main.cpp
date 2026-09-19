#include "nfsu/geometry.h"
#include "nfsu/render.h"
#include "nfsu/texture.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  nfsu_viewer <GEOMETRY.BIN> <TEXTURES.BIN> <out.tga> [options]\n"
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
        << "  --shading MODE       flat (default) or none\n";
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

// Returns 0 on success, otherwise the exit code after printing an error.
int parse_options(int argc, char** argv, nfsu::RenderOptions& options) {
    const nfsu::RenderLimits limits;
    std::set<std::string> seen;
    for (int i = 4; i < argc; ++i) {
        const std::string name = argv[i];
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
            "--shading"};
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
        }
        if (!ok) {
            std::cerr << "error: invalid value for " << name << ": " << value << '\n';
            return 1;
        }
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
    const int parse_result = parse_options(argc, argv, options);
    if (parse_result != 0) {
        return parse_result;
    }

    // Fail early; write_frame_tga repeats these checks before writing.
    {
        std::error_code error;
        fs::path partial = output_path;
        partial += ".partial";
        for (const fs::path& source : {geometry_path, texture_path}) {
            if (fs::equivalent(source, output_path, error) ||
                fs::equivalent(source, partial, error)) {
                std::cerr << "error: output path must differ from every input file\n";
                return 1;
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
    std::vector<unsigned char> bytes;
    const nfsu::TexturePack pack = nfsu::load_texture_pack(texture_path, bytes);
    for (const nfsu::TextureIssue& issue : pack.issues) {
        std::cerr << "texture issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    if (geometry.valid_mesh_count() == 0) {
        std::cerr << "error: no valid meshes to render\n";
        return 2;
    }
    nfsu::RenderTextureSet textures;
    {
        std::string error;
        if (!nfsu::decode_render_textures(bytes, pack, nfsu::RenderTextureLimits{},
                                          textures, error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
    }
    for (const nfsu::RenderTexture& texture : textures.textures) {
        if (!texture.valid && !texture.problem.empty()) {
            std::cerr << "texture " << std::hex << texture.hash << std::dec
                      << " drawn with fallback: " << texture.problem << '\n';
        }
    }
    std::size_t decoded = 0;
    for (const nfsu::RenderTexture& texture : textures.textures) {
        if (texture.valid) {
            ++decoded;
        }
    }

    nfsu::Frame frame;
    nfsu::RenderStats stats;
    const nfsu::RenderLimits limits;
    std::string error;
    if (!nfsu::render_scene(geometry, textures, options, limits, frame, stats,
                            error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    nfsu::FrameWriteResult write;
    if (!nfsu::write_frame_tga(frame, output_path, {geometry_path, texture_path},
                               write)) {
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
              << textures.decoded_bytes << " issues " << pack.issues.size()
              << '\n';
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
    print_stats(stats);
    std::cout << "wrote: " << output_path.filename().u8string() << '\n';
    return geometry.ok() && pack.ok() ? 0 : 3;
}
