#include "nfsu/stock_frame.h"

#include "nfsu/image.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <new>
#include <sstream>
#include <utility>

namespace nfsu {

namespace {

namespace fs = std::filesystem;

constexpr const char* kFilePaths[kStockFrameFileCount] = {
    "Cars/240SX/GEOMETRY.BIN", "Cars/240SX/TEXTURES.BIN", "Global/GLOBALB.BUN",
    "Cars/TEXTURES.BIN",       "Cars/BRAKES/GEOMETRY.BIN", "Cars/PLATES/GEOMETRY.BIN"};

// The two --shared-textures files and the two --stock-geometry files, in
// the order the command names them.
constexpr StockFrameFile kSharedTextureFiles[] = {StockFrameFile::global_bun,
                                                  StockFrameFile::shared_car_textures};
constexpr StockFrameFile kStockGeometryFiles[] = {StockFrameFile::brake_geometry,
                                                  StockFrameFile::plate_geometry};

// Reads one whole file. The messages are the ones the parsers' own loaders
// use for the same failures.
bool read_whole_file(const fs::path& path, std::vector<unsigned char>& bytes,
                     std::string& error) {
    bytes.clear();
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream) {
        error = "failed to open file";
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = static_cast<std::streamoff>(stream.tellg());
    if (!stream || end < 0) {
        error = "failed to read file size";
        return false;
    }
    const std::uint64_t size = static_cast<std::uint64_t>(end);
    if (size > kStockFrameMaxFileBytes ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        error = "file is " + std::to_string(size) + " bytes; limit is " +
                std::to_string(kStockFrameMaxFileBytes);
        return false;
    }
    stream.seekg(0, std::ios::beg);
    if (!stream) {
        error = "failed to read file size";
        return false;
    }
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (const std::exception&) {
        bytes.clear();
        error = "out of memory while reading file";
        return false;
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        error = "failed to read whole file";
        return false;
    }
    return true;
}

std::string at(const std::string& path, std::uint64_t offset, const std::string& message) {
    return path + ": issue at " + std::to_string(offset) + ": " + message;
}

std::string hex(std::uint32_t value) {
    static const char kDigits[] = "0123456789abcdef";
    std::string text;
    bool started = false;
    for (int shift = 28; shift >= 0; shift -= 4) {
        const unsigned int digit = (value >> static_cast<unsigned int>(shift)) & 0xFU;
        if (digit != 0 || started || shift == 0) {
            text.push_back(kDigits[digit]);
            started = true;
        }
    }
    return text;
}

} // namespace

const char* stock_frame_file_path(std::size_t index) noexcept {
    return index < kStockFrameFileCount ? kFilePaths[index] : nullptr;
}

bool StockFrameInputs::all_read() const noexcept {
    for (const StockFrameInput& input : files) {
        if (!input.read) {
            return false;
        }
    }
    return true;
}

bool read_stock_frame_inputs(const fs::path& root, StockFrameInputs& inputs) {
    for (std::size_t i = 0; i < kStockFrameFileCount; ++i) {
        StockFrameInput& input = inputs.files[i];
        input.path = kFilePaths[i];
        input.read = false;
        input.error.clear();
        try {
            input.read = read_whole_file(root / fs::path(kFilePaths[i]), input.bytes,
                                         input.error);
        } catch (const std::exception&) {
            input.bytes.clear();
            input.read = false;
            input.error = "failed to read file";
        }
    }
    return inputs.all_read();
}

StockSceneRequest stock_240sx_request() {
    StockSceneRequest request;
    request.car_type_name = "240SX";
    request.lod = 0;
    request.pass = CarDrawPass::world;
    request.place_wheels_from_culling_table = true;
    return request;
}

// Each step names the lines of src/viewer_main.cpp's main() it mirrors. The
// viewer reads a file inside each load_* call and then parses the bytes
// with the matching parse_* call and its default limits; here the bytes are
// already in memory, so the parse_* call is made directly. For a file that
// was read, the two are the same parse of the same bytes.
bool prepare_stock_frame(
    const StockFrameInputs& inputs,
    const StockSceneRequest& request,
    StockFrame& frame,
    std::string& error) {
    frame.car_geometry = GeometryReport{};
    frame.stock_geometry.clear();
    frame.parts = CarPartDatabase{};
    frame.car_types = CarTypeTable{};
    frame.car_pack = TexturePack{};
    frame.shared_sets.clear();
    frame.wanted.clear();
    frame.textures = RenderTextureSet{};
    frame.scene = PreparedCarScene{};
    frame.report = StockSceneReport{};
    frame.frame = Frame{};
    frame.stats = RenderStats{};
    frame.issues.clear();
    frame.inputs_ok = false;
    error.clear();

    if (!inputs.all_read()) {
        std::size_t missing = 0;
        for (const StockFrameInput& input : inputs.files) {
            missing += input.read ? 0 : 1;
        }
        error = std::to_string(missing) + " of " + std::to_string(kStockFrameFileCount) +
                " input files could not be read";
        return false;
    }

    try {
        const StockFrameInput& car_geometry = inputs.file(StockFrameFile::car_geometry);
        const StockFrameInput& car_textures = inputs.file(StockFrameFile::car_textures);
        const StockFrameInput& parts_file = inputs.file(StockFrameFile::global_bun);

        // load_geometry_file(<GEOMETRY.BIN>) and its "geometry issue" lines.
        frame.car_geometry =
            parse_geometry(car_geometry.bytes.data(), car_geometry.bytes.size());
        for (const GeometryIssue& issue : frame.car_geometry.issues) {
            frame.issues.push_back(at(car_geometry.path, issue.offset, issue.message));
        }
        // Each --stock-geometry in order, then the source list with the
        // car's own file first; the vector is sized once, so the pointers
        // stay valid.
        frame.stock_geometry.reserve(std::size(kStockGeometryFiles));
        for (const StockFrameFile which : kStockGeometryFiles) {
            const StockFrameInput& input = inputs.file(which);
            frame.stock_geometry.push_back(
                parse_geometry(input.bytes.data(), input.bytes.size()));
            for (const GeometryIssue& issue : frame.stock_geometry.back().issues) {
                frame.issues.push_back(at(input.path, issue.offset, issue.message));
            }
        }
        std::vector<const GeometryReport*> sources;
        sources.push_back(&frame.car_geometry);
        for (const GeometryReport& source : frame.stock_geometry) {
            sources.push_back(&source);
        }
        // load_car_part_database and load_car_type_table of --stock-parts.
        frame.parts =
            parse_car_part_database(parts_file.bytes.data(), parts_file.bytes.size());
        for (const CarPartIssue& issue : frame.parts.issues) {
            frame.issues.push_back(at(parts_file.path, issue.offset, issue.message));
        }
        frame.car_types =
            parse_car_type_table(parts_file.bytes.data(), parts_file.bytes.size());
        for (const CarTypeIssue& issue : frame.car_types.issues) {
            frame.issues.push_back(at(parts_file.path, issue.offset, issue.message));
        }
        const SceneLimits scene_limits;

        // load_texture_pack(<TEXTURES.BIN>) with the default limits.
        frame.car_pack =
            parse_texture_pack(car_textures.bytes.data(), car_textures.bytes.size());
        for (const TextureIssue& issue : frame.car_pack.issues) {
            frame.issues.push_back(at(car_textures.path, issue.offset, issue.message));
        }
        std::uint64_t read_bytes = car_textures.bytes.size();

        // Each --shared-textures in order, each refused before it is read
        // if it would take the texture input past the viewer's total. A
        // refused source contributes no bytes, as the viewer's empty buffer
        // does.
        static const std::vector<unsigned char> kNoBytes;
        std::vector<const std::vector<unsigned char>*> shared_bytes;
        frame.shared_sets.reserve(std::size(kSharedTextureFiles));
        for (const StockFrameFile which : kSharedTextureFiles) {
            const StockFrameInput& input = inputs.file(which);
            TextureLimits limits;
            const std::uint64_t remaining = kStockFrameMaxTextureInputBytes - read_bytes;
            limits.max_file_size = std::min(limits.max_file_size, remaining);
            if (input.bytes.size() > limits.max_file_size) {
                TexturePackSet refused;
                refused.file_size = input.bytes.size();
                refused.issues.push_back(TextureIssue{
                    0, "file is " + std::to_string(input.bytes.size()) +
                           " bytes; limit is " + std::to_string(limits.max_file_size)});
                frame.shared_sets.push_back(std::move(refused));
                shared_bytes.push_back(&kNoBytes);
                frame.issues.push_back(
                    input.path + ": not read: " + std::to_string(read_bytes) +
                    " texture input bytes already read, total limit is " +
                    std::to_string(kStockFrameMaxTextureInputBytes));
            } else {
                frame.shared_sets.push_back(
                    parse_texture_pack_set(input.bytes.data(), input.bytes.size(), limits));
                shared_bytes.push_back(&input.bytes);
                read_bytes += input.bytes.size();
            }
            const TexturePackSet& set = frame.shared_sets.back();
            for (const TextureIssue& issue : set.issues) {
                frame.issues.push_back(at(input.path, issue.offset, issue.message));
            }
            for (std::size_t p = 0; p < set.packs.size(); ++p) {
                for (const TextureIssue& issue : set.packs[p].issues) {
                    frame.issues.push_back(input.path + ": pack " + std::to_string(p) +
                                           ": issue at " + std::to_string(issue.offset) +
                                           ": " + issue.message);
                }
            }
        }

        // "error: no valid meshes to render", exit 2.
        std::size_t valid_meshes = frame.car_geometry.valid_mesh_count();
        for (const GeometryReport& source : frame.stock_geometry) {
            valid_meshes += source.valid_mesh_count();
        }
        if (valid_meshes == 0) {
            error = "no valid meshes to render";
            return false;
        }

        // The stock assembly resolved once against no textures, for the
        // meshes whose texture hashes a shared source is asked to decode.
        std::vector<SceneMeshRef> mesh_refs;
        {
            PreparedCarScene unbound;
            StockSceneReport unbound_report;
            std::string stock_error;
            if (!prepare_stock_car_scene(sources, RenderTextureSet{}, frame.parts,
                                         frame.car_types, request, scene_limits, unbound,
                                         unbound_report, stock_error)) {
                error = "stock car: " + stock_error;
                return false;
            }
            for (const PreparedMesh& mesh : unbound.meshes) {
                mesh_refs.push_back(SceneMeshRef{mesh.source, mesh.source_index});
            }
        }

        // The decode, shared sources present: the car's pack is source 0 and
        // every shared source decodes only the wanted hashes.
        for (const SceneMeshRef& ref : mesh_refs) {
            const Mesh& mesh = sources[ref.source]->meshes[ref.index];
            for (const MeshMaterial& material : mesh.materials) {
                if (material.texture_slot < mesh.texture_hashes.size()) {
                    frame.wanted.push_back(mesh.texture_hashes[material.texture_slot]);
                }
            }
        }
        std::sort(frame.wanted.begin(), frame.wanted.end());
        frame.wanted.erase(std::unique(frame.wanted.begin(), frame.wanted.end()),
                           frame.wanted.end());

        std::vector<RenderTextureSource> texture_sources(1 + frame.shared_sets.size());
        texture_sources[0].bytes = &car_textures.bytes;
        // A pack that could not be read has no records to offer.
        if (frame.car_pack.parse_source == car_textures.bytes.data() &&
            frame.car_pack.file_size == car_textures.bytes.size()) {
            texture_sources[0].packs.push_back(&frame.car_pack);
        }
        texture_sources[0].complete = frame.car_pack.tables_parsed;
        for (std::size_t i = 0; i < frame.shared_sets.size(); ++i) {
            RenderTextureSource& source = texture_sources[i + 1];
            source.bytes = shared_bytes[i];
            for (const TexturePack& shared_pack : frame.shared_sets[i].packs) {
                source.packs.push_back(&shared_pack);
            }
            source.complete = frame.shared_sets[i].complete;
            source.wanted = &frame.wanted;
        }
        std::string decode_error;
        if (!decode_render_texture_sources(texture_sources, RenderTextureLimits{},
                                           frame.textures, decode_error)) {
            error = decode_error;
            return false;
        }
        for (const RenderTexture& texture : frame.textures.textures) {
            if (!texture.valid && !texture.problem.empty()) {
                std::string text;
                if (texture.source != 0) {
                    const std::size_t shared = texture.source - 1;
                    text = (shared < std::size(kSharedTextureFiles)
                                ? inputs.file(kSharedTextureFiles[shared]).path
                                : std::string("texture source ") +
                                      std::to_string(texture.source)) +
                           ": pack " + std::to_string(texture.pack) + " texture " +
                           std::to_string(texture.record) + ": ";
                } else {
                    text = car_textures.path + ": ";
                }
                frame.issues.push_back(text + "texture " + hex(texture.hash) +
                                       " drawn with fallback: " + texture.problem);
            }
        }

        // The stock scene again, now bound to the decoded textures.
        std::string stock_error;
        if (!prepare_stock_car_scene(sources, frame.textures, frame.parts, frame.car_types,
                                     request, scene_limits, frame.scene, frame.report,
                                     stock_error)) {
            error = "stock car: " + stock_error;
            return false;
        }

        // The viewer's final "geometry.ok() && pack.ok() && sources_ok &&
        // skin_ok && stock_ok"; there is no skin here.
        bool ok = frame.car_geometry.ok() && frame.car_pack.ok();
        for (const TexturePackSet& set : frame.shared_sets) {
            ok = ok && set.ok();
        }
        ok = ok && frame.parts.ok() && frame.car_types.ok();
        for (const GeometryReport& source : frame.stock_geometry) {
            ok = ok && source.ok();
        }
        frame.inputs_ok = ok;
        return true;
    } catch (const std::bad_alloc&) {
        error = "out of memory while preparing the stock frame";
    } catch (const std::exception& caught) {
        error = std::string("failed while preparing the stock frame: ") + caught.what();
    }
    return false;
}

bool render_stock_frame(StockFrame& frame, const RenderOptions& options, std::string& error) {
    error.clear();
    try {
        const RenderLimits limits;
        return render_scene(frame.scene, options, limits, frame.frame, frame.stats, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory while rendering";
    } catch (const std::exception& caught) {
        error = std::string("failed while rendering: ") + caught.what();
    }
    return false;
}

bool encode_stock_frame(const Frame& frame, StockFrameImage& image, std::string& error) {
    image.tga.clear();
    image.sha256 = Sha256Digest{};
    error.clear();
    // write_frame_tga's own first check.
    if (frame.width() == 0 || frame.height() == 0) {
        error = "frame is empty";
        return false;
    }
    try {
        std::ostringstream stream(std::ios::binary);
        if (!write_tga(stream, frame.width(), frame.height(), frame.rgba(), error)) {
            return false;
        }
        image.tga = std::move(stream).str();
    } catch (const std::bad_alloc&) {
        image.tga.clear();
        error = "out of memory while encoding the TGA";
        return false;
    }
    image.sha256 = sha256(image.tga.data(), image.tga.size());
    return true;
}

std::vector<std::string> stock_frame_stats_lines(const RenderStats& s) {
    std::vector<std::string> lines;
    const auto line = [&lines](const std::ostringstream& text) { lines.push_back(text.str()); };
    {
        std::ostringstream text;
        text << "meshes: " << s.meshes << " rendered " << s.meshes_rendered
             << " skipped_invalid " << s.meshes_skipped_invalid;
        line(text);
    }
    {
        std::ostringstream text;
        text << "material groups: " << s.material_groups << " resolved " << s.groups_resolved
             << " unresolved " << s.groups_unresolved << " unusable " << s.groups_unusable
             << " invalid " << s.groups_invalid;
        line(text);
    }
    {
        std::ostringstream text;
        text << "unresolved hashes: " << s.unresolved_hashes;
        line(text);
    }
    {
        std::ostringstream text;
        text << "triangles submitted: " << s.triangles_submitted << " textured "
             << s.triangles_resolved << " fallback " << s.triangles_fallback;
        line(text);
    }
    {
        std::ostringstream text;
        text << "triangles bad_index: " << s.triangles_bad_index
             << " degenerate_source: " << s.triangles_degenerate_source;
        line(text);
    }
    {
        std::ostringstream text;
        text << "triangles outside: " << s.triangles_outside << " clipped: "
             << s.triangles_clipped << " degenerate_screen: " << s.triangles_degenerate_screen
             << " culled: " << s.triangles_culled;
        line(text);
    }
    {
        std::ostringstream text;
        text << "triangles front: " << s.triangles_front << " back: " << s.triangles_back
             << " rasterized: " << s.triangles_rasterized
             << " raster_pieces: " << s.raster_triangles;
        line(text);
    }
    {
        std::ostringstream text;
        text << "pixel tests: " << s.pixel_tests;
        line(text);
    }
    {
        std::ostringstream text;
        text << "fragments: " << s.fragments << " alpha_discarded "
             << s.fragments_alpha_discarded << " far_discarded " << s.fragments_far_discarded
             << " depth_failed " << s.fragments_depth_failed << " written "
             << s.fragments_written;
        line(text);
    }
    {
        std::ostringstream text;
        text << "pixels drawn: " << s.pixels_drawn << " fallback " << s.pixels_fallback;
        line(text);
    }
    {
        std::ostringstream text;
        if (s.pixels_drawn > 0) {
            text << "drawn bounds: x " << s.min_x << ".." << s.max_x << " y " << s.min_y
                 << ".." << s.max_y;
        } else {
            text << "drawn bounds: none";
        }
        line(text);
    }
    {
        std::ostringstream text;
        text << "instances: " << s.instances << " exact " << s.instances_exact
             << " stand_in " << s.instances_stand_in;
        line(text);
    }
    return lines;
}

std::string stock_frame_slots_line(const StockSceneReport& report) {
    std::ostringstream text;
    text << "stock slots: installed " << report.installed << " with_model "
         << report.with_model << " drawn " << report.drawn << " by_fallback "
         << report.fell_back << " markers " << report.markers.size();
    return text.str();
}

} // namespace nfsu
