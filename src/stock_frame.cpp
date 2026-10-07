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

// The stock --shared-textures files; tuned adds wheel textures after these.
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

// The viewer's hex32: "0x" and eight upper-case digits.
std::string hex32(std::uint32_t value) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(kDigits[(value >> static_cast<unsigned int>(shift)) & 0xFU]);
    }
    return text;
}

// The viewer's ascii_quoted: printable ASCII kept, a quote, a backslash and
// every other byte written as \xHH, all inside double quotes.
std::string ascii_quoted(const std::string& text) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20U && byte <= 0x7EU && ch != '"' && ch != '\\') {
            out += ch;
        } else {
            out += "\\x";
            out += kDigits[byte >> 4U];
            out += kDigits[byte & 0xFU];
        }
    }
    return out + "\"";
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
    return !tuned || (wheel_geometry.read && wheel_textures.read && vinyls.read);
}

std::string StockFrameInputs::read_error(const fs::path& root) const {
    std::string error;
    std::size_t missing = 0;
    const auto append = [&](const StockFrameInput& input) {
        if (input.read) return;
        ++missing;
        error += "NOT READ " + (root / input.path).generic_u8string() + ": " + input.error + "\n";
    };
    for (const auto& input : files) append(input);
    if (tuned) { append(wheel_geometry); append(wheel_textures); append(vinyls); }
    if (missing) error += std::to_string(missing) + " of " +
        std::to_string(kStockFrameFileCount + (tuned ? 3 : 0)) +
        " inputs could not be read. Copy each from the game's own installation to the same path under " +
        root.generic_u8string() + ".";
    return error;
}

bool read_stock_frame_inputs(const fs::path& root, StockFrameInputs& inputs, bool tuned) {
    inputs.tuned = tuned;
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
    const auto read_extra = [&](StockFrameInput& input, const char* path) {
        input.path = path;
        input.read = false;
        input.error.clear();
        input.bytes.clear();
        if (tuned) {
            try {
                input.read = read_whole_file(root / fs::path(path), input.bytes, input.error);
            } catch (const std::exception&) {
                input.error = "failed to read file";
            }
        }
    };
    read_extra(inputs.wheel_geometry, "Cars/WHEELS/GEOMETRY.BIN");
    read_extra(inputs.wheel_textures, "Cars/WHEELS/TEXTURES.BIN");
    read_extra(inputs.vinyls, "Cars/240SX/VINYLS.BIN");
    return inputs.all_read();
}

bool read_car_frame_inputs(const fs::path& root, const CarAssets& car,
                           StockFrameInputs& inputs, bool tuned, std::string& error) {    if (car.car_type == kNoCarType || car.geometry_path.empty() ||
        car.textures_path.empty() || car.vinyls_path.empty()) {
        error = "car inputs need a car resolved through resolve_car_assets";
        return false;
    }
    error.clear();
    inputs.tuned = tuned;
    // The same six slots, with the car's own two files in place of the
    // 240SX's, and the same three tuned extras with the car's own vinyls.
    static_assert(kStockFrameFileCount == 6, "the per-car reader fills six files");
    const std::string paths[kStockFrameFileCount] = {
        car.geometry_path,
        car.textures_path,
        kFilePaths[static_cast<std::size_t>(StockFrameFile::global_bun)],
        kFilePaths[static_cast<std::size_t>(StockFrameFile::shared_car_textures)],
        kFilePaths[static_cast<std::size_t>(StockFrameFile::brake_geometry)],
        kFilePaths[static_cast<std::size_t>(StockFrameFile::plate_geometry)]};
    for (std::size_t i = 0; i < kStockFrameFileCount; ++i) {
        StockFrameInput& input = inputs.files[i];
        input.path = paths[i];
        input.read = false;
        input.error.clear();
        try {
            input.read = read_whole_file(root / fs::path(paths[i]), input.bytes,
                                         input.error);
        } catch (const std::exception&) {
            input.bytes.clear();
            input.read = false;
            input.error = "failed to read file";
        }
    }
    const auto read_extra = [&](StockFrameInput& input, const std::string& path) {
        input.path = path;
        input.read = false;
        input.error.clear();
        input.bytes.clear();
        if (tuned) {
            try {
                input.read = read_whole_file(root / fs::path(path), input.bytes, input.error);
            } catch (const std::exception&) {
                input.error = "failed to read file";
            }
        }
    };
    read_extra(inputs.wheel_geometry, "Cars/WHEELS/GEOMETRY.BIN");
    read_extra(inputs.wheel_textures, "Cars/WHEELS/TEXTURES.BIN");
    read_extra(inputs.vinyls, car.vinyls_path);
    return inputs.all_read();
}

bool read_exhaust_family_inputs(const fs::path& root, StockFrameInputs& inputs) {
    const auto read_family = [&root](StockFrameInput& input, const char* path) {
        input.path = path;
        input.read = false;
        input.error.clear();
        input.bytes.clear();
        try {
            input.read = read_whole_file(root / fs::path(path), input.bytes, input.error);
        } catch (const std::exception&) {
            input.bytes.clear();
            input.read = false;
            input.error = "failed to read file";
        }
    };
    read_family(inputs.exhaust_geometry, "Cars/EXHAUST/GEOMETRY.BIN");
    read_family(inputs.exhaust_textures, "Cars/EXHAUST/TEXTURES.BIN");
    return inputs.exhaust_geometry.read && inputs.exhaust_textures.read;
}

std::string car_frame_inputs_error(const StockFrameInputs& inputs,
                                   const std::string& error,
                                   const fs::path& root) {
    // A caller-misuse refusal already carries its sentence; a file failure
    // left `error` empty on purpose, and the inputs' own report names every
    // unread pack.
    return error.empty() ? inputs.read_error(root) : error;
}

StockSceneRequest stock_240sx_request() {
    return stock_car_request("240SX");
}

StockSceneRequest stock_car_request(const std::string& car_type_name) {
    StockSceneRequest request;
    request.car_type_name = car_type_name;
    request.lod = 0;
    request.pass = CarDrawPass::world;
    request.place_wheels_from_culling_table = true;
    return request;
}

StockSceneRequest tuned_240sx_request() {
    StockSceneRequest request = stock_240sx_request();
    request.installed_parts.names[kSlotFrontBumper] = "240SX_KIT01_FRONT_BUMPER";
    request.installed_parts.names[kSlotRearBumper] = "240SX_KIT01_REAR_BUMPER";
    request.installed_parts.names[kSlotSkirt] = "240SX_KIT01_SKIRT";
    request.installed_parts.names[kSlotWheel] = "BBS_STYLE01_15_23";
    return request;
}

StockFrameSkin stock_240sx_skin() {
    return stock_car_skin("240SX");
}

StockFrameSkin stock_car_skin(const std::string& car_type_name) {
    StockFrameSkin skin;
    skin.enabled = true;
    skin.car = car_type_name;
    skin.selection.car_name = skin.car;
    skin.selection.doorline = SkinDoorlineSelection{SkinDoorlineVariant::plain};
    skin.selection.base_color = skin_pack_paint(30, 60, 160, 128);
    return skin;
}

StockFrameSkin tuned_240sx_skin() {
    StockFrameSkin skin = stock_240sx_skin();
    skin.selection.doorline = SkinDoorlineSelection{SkinDoorlineVariant::body_kit};
    skin.selection.base_color = skin_pack_paint(200, 30, 30, 128);
    SkinVinylLayerSelection vinyl;
    vinyl.texture = "TRIBAL5";
    vinyl.palette_size = 16;
    vinyl.remap = false;
    skin.selection.vinyl_layers[0] = vinyl;
    return skin;
}

bool prepare_stock_frame(
    const StockFrameInputs& inputs,
    const StockSceneRequest& request,
    StockFrame& frame,
    std::string& error) {
    return prepare_stock_frame(inputs, request, StockFrameSkin{}, frame, error);
}

// Each step names the lines of src/viewer_main.cpp's main() it mirrors. The
// viewer reads a file inside each load_* call and then parses the bytes
// with the matching parse_* call and its default limits; here the bytes are
// already in memory, so the parse_* call is made directly. For a file that
// was read, the two are the same parse of the same bytes.
static bool prepare_stock_frame_impl(
    const StockFrameInputs& inputs,
    const StockSceneRequest& request,
    const StockFrameSkin& skin,
    StockFrame& frame,
    std::string& error, StockFrameInputs* consumed, const fs::path& root,
    bool car_shading) {
    frame.car_geometry = GeometryReport{};
    frame.stock_geometry.clear();
    frame.parts = CarPartDatabase{};
    frame.car_types = CarTypeTable{};
    frame.car_pack = TexturePack{};
    frame.vinyl_pack = TexturePack{};
    frame.shared_sets.clear();
    frame.wanted.clear();
    frame.textures = RenderTextureSet{};
    frame.scene = PreparedCarScene{};
    frame.report = StockSceneReport{};
    frame.frame = Frame{};
    frame.stats = RenderStats{};
    frame.skinned = false;
    frame.skin_sources.clear();
    frame.skin_plan = BodySkinInputPlan{};
    frame.skin_owned_bytes = 0;
    frame.skin = RuntimeSkin{};
    frame.skin_binding = RuntimeSkinBinding{};
    frame.skin_bound_names.clear();
    frame.issues.clear();
    frame.inputs_ok = false;
    error.clear();

    if (!inputs.all_read()) {
        std::size_t missing = 0;
        for (const StockFrameInput& input : inputs.files) {
            missing += input.read ? 0 : 1;
        }
        if (inputs.tuned) {
            missing += inputs.wheel_geometry.read ? 0 : 1;
            missing += inputs.wheel_textures.read ? 0 : 1;
            missing += inputs.vinyls.read ? 0 : 1;
        }
        error = std::to_string(missing) + " of " +
                std::to_string(kStockFrameFileCount + (inputs.tuned ? 3 : 0)) +
                " input files could not be read";
        return false;
    }

    try {
        const StockFrameInput& car_geometry = inputs.file(StockFrameFile::car_geometry);
        const StockFrameInput& car_textures = inputs.file(StockFrameFile::car_textures);
        const StockFrameInput& parts_file = inputs.file(StockFrameFile::global_bun);

        const auto parse_part_tables = [&] {
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
        };
        std::vector<std::uint32_t> retained_hashes;
        if (consumed) {
            parse_part_tables();
            StockParts selected;
            if (!select_installed_parts(frame.parts, request.car_type_name,
                                        request.installed_parts, selected, error)) return false;
            for (const auto& slot : selected.slots) if (slot.record != kNoCarPart) {
                const auto& hashes = frame.parts.records.at(slot.record).lod_mesh_hashes;
                retained_hashes.insert(retained_hashes.end(), hashes.begin(), hashes.end());
            }
        }
        // load_geometry_file(<GEOMETRY.BIN>) and its "geometry issue" lines.
        frame.car_geometry =
            parse_geometry(car_geometry.bytes.data(), car_geometry.bytes.size());
        if (consumed) std::vector<unsigned char>().swap(
            consumed->files[static_cast<std::size_t>(StockFrameFile::car_geometry)].bytes);
        alias_stock_window_textures(frame.car_geometry);
        for (const GeometryIssue& issue : frame.car_geometry.issues) {
            frame.issues.push_back(at(car_geometry.path, issue.offset, issue.message));
        }
        // Each --stock-geometry in order, then the source list with the
        // car's own file first; the vector is sized once, so the pointers
        // stay valid.
        frame.stock_geometry.reserve(std::size(kStockGeometryFiles) + (inputs.tuned ? 1U : 0U));
        for (const StockFrameFile which : kStockGeometryFiles) {
            const StockFrameInput& input = inputs.file(which);
            frame.stock_geometry.push_back(
                parse_geometry(input.bytes.data(), input.bytes.size()));
            if (consumed) std::vector<unsigned char>().swap(consumed->files[static_cast<std::size_t>(which)].bytes);
            alias_stock_window_textures(frame.stock_geometry.back());
            for (const GeometryIssue& issue : frame.stock_geometry.back().issues) {
                frame.issues.push_back(at(input.path, issue.offset, issue.message));
            }
        }
        if (inputs.tuned) {
            const StockFrameInput& input = inputs.wheel_geometry;
            GeometryLimits geometry_limits;
            if (consumed) geometry_limits.retain_mesh = [&](const Mesh& mesh) {
                const auto hash = bin_hash(mesh.name);
                return std::find(retained_hashes.begin(), retained_hashes.end(), hash) != retained_hashes.end();
            };
            frame.stock_geometry.push_back(
                parse_geometry(input.bytes.data(), input.bytes.size(), geometry_limits));
            if (consumed) std::vector<unsigned char>().swap(consumed->wheel_geometry.bytes);
            for (const GeometryIssue& issue : frame.stock_geometry.back().issues) {
                frame.issues.push_back(at(input.path, issue.offset, issue.message));
            }
        }
        // The opt-in EXHAUST family source, after the wheels: same parse,
        // same retained-mesh filter on the race path, no effect at all for a
        // caller that read no family pair.
        if (inputs.exhaust_geometry.read) {
            const StockFrameInput& input = inputs.exhaust_geometry;
            GeometryLimits geometry_limits;
            if (consumed) geometry_limits.retain_mesh = [&](const Mesh& mesh) {
                const auto hash = bin_hash(mesh.name);
                return std::find(retained_hashes.begin(), retained_hashes.end(), hash) != retained_hashes.end();
            };
            frame.stock_geometry.push_back(
                parse_geometry(input.bytes.data(), input.bytes.size(), geometry_limits));
            if (consumed) std::vector<unsigned char>().swap(consumed->exhaust_geometry.bytes);
            for (const GeometryIssue& issue : frame.stock_geometry.back().issues) {
                frame.issues.push_back(at(input.path, issue.offset, issue.message));
            }
        }
        if (!consumed) parse_part_tables();
        std::vector<const GeometryReport*> sources;
        sources.push_back(&frame.car_geometry);
        for (const GeometryReport& source : frame.stock_geometry) {
            sources.push_back(&source);
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
        std::vector<const StockFrameInput*> shared_inputs;
        for (const StockFrameFile which : kSharedTextureFiles) {
            shared_inputs.push_back(&inputs.file(which));
        }
        if (inputs.tuned) {
            shared_inputs.push_back(&inputs.wheel_textures);
        }
        if (inputs.exhaust_textures.read) {
            shared_inputs.push_back(&inputs.exhaust_textures);
        }
        frame.shared_sets.reserve(shared_inputs.size());
        for (const StockFrameInput* shared_input : shared_inputs) {
            const StockFrameInput& input = *shared_input;
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

        // The runtime lamp-name resolution (issue #315): every loaded
        // texture source is parsed by now, so the resting-state rewrite
        // below sees exactly the hashes the decode will find.
        {
            std::set<std::uint32_t> present;
            for (const TextureRecord& texture : frame.car_pack.textures) {
                present.insert(texture.hash);
            }
            for (const TexturePackSet& set : frame.shared_sets) {
                for (const TexturePack& pack : set.packs) {
                    for (const TextureRecord& texture : pack.textures) {
                        present.insert(texture.hash);
                    }
                }
            }
            frame.lamp_texture_aliases =
                alias_stock_lamp_textures(present, request.car_type_name,
                                          frame.car_geometry);
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
        bool wants_left_brake = false;
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
            wants_left_brake = std::any_of(unbound.instances.begin(), unbound.instances.end(),
                [](const PreparedInstance& instance) {
                    return instance.part_slot == kSlotBrake &&
                           (instance.copy == 0 || instance.copy == 3);
                });
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
        // The left brake substitution is per copy, not a mesh material.
        if (wants_left_brake) {
            frame.wanted.push_back(kBrakeGlobalLeftHash); // BRAKE_GLOBAL_LEFT
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
                    text = (shared < shared_inputs.size()
                                ? shared_inputs[shared]->path
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

        // The runtime body skin: the viewer's "if (skin.enabled)" block,
        // step by step. Nothing here runs without it.
        bool skin_ok = true;
        if (skin.enabled) {
            // Skin input sources in the order they are searched: the car's
            // pack, then each shared source in order (there is no
            // --skin-vinyls source). A pack the assembly layer would refuse
            // offers no records instead, and a source left with no usable
            // pack is not searched; either is an issue, as the viewer's
            // "skin source" lines on its standard error are.
            std::vector<SkinInputSource> skin_sources;
            const auto add_source = [&](const std::vector<unsigned char>& source_bytes,
                                        SkinSourceKind kind, const std::string& label,
                                        const std::string& path,
                                        const std::vector<const TexturePack*>& packs) {
                SkinInputSource source;
                source.bytes = &source_bytes;
                source.kind = kind;
                source.path = label;
                for (const TexturePack* candidate : packs) {
                    if (candidate != nullptr && candidate->tables_parsed &&
                        candidate->parse_source == source_bytes.data() &&
                        candidate->file_size == source_bytes.size()) {
                        source.packs.push_back(candidate);
                    } else {
                        frame.issues.push_back(path + ": skin source " + label +
                                               ": a pack has no parsed record tables and "
                                               "offers no records");
                        skin_ok = false;
                    }
                }
                if (source.packs.empty()) {
                    frame.issues.push_back(path + ": skin source " + label +
                                           ": no usable pack, not searched");
                    skin_ok = false;
                    return;
                }
                frame.skin_sources.push_back(label);
                skin_sources.push_back(std::move(source));
            };
            add_source(car_textures.bytes, SkinSourceKind::car_textures, "car-textures",
                       car_textures.path, {&frame.car_pack});
            if (inputs.tuned) {
                if (consumed) {
                    auto& vinyls = consumed->vinyls;
                    vinyls.read = read_whole_file(root / vinyls.path, vinyls.bytes, vinyls.error);
                    if (!vinyls.read) { error = consumed->read_error(root); return false; }
                }
                const StockFrameInput& input = inputs.vinyls;
                frame.vinyl_pack = parse_texture_pack(input.bytes.data(), input.bytes.size());
                for (const TextureIssue& issue : frame.vinyl_pack.issues) {
                    frame.issues.push_back(at(input.path, issue.offset, issue.message));
                }
                add_source(input.bytes, SkinSourceKind::car_vinyls, "car-vinyls",
                           input.path, {&frame.vinyl_pack});
            }
            for (std::size_t i = 0; i < frame.shared_sets.size(); ++i) {
                std::vector<const TexturePack*> packs;
                for (const TexturePack& shared_pack : frame.shared_sets[i].packs) {
                    packs.push_back(&shared_pack);
                }
                add_source(*shared_bytes[i], SkinSourceKind::other,
                           "shared-" + std::to_string(i + 1),
                           shared_inputs[i]->path, packs);
            }

            BodySkinSelection selected = skin.selection;
            selected.car_name = skin.car;
            selected.target_hash = bin_hash(skin.target_name);
            // Width and height stay as given, zero: they are taken from the
            // target record.
            const SkinInputLimits skin_limits;
            std::string skin_error;
            BodySkinInputPlan plan;
            if (!plan_body_skin_inputs(selected, skin_sources, skin_limits, plan, skin_error)) {
                error = "skin inputs: " + skin_error;
                return false;
            }
            SkinTargetChoice choice;
            if (!choose_skin_target_path(plan.target_texture, choice)) {
                error = "skin target: " + choice.reason;
                return false;
            }
            selected.target = choice.path;
            AssembledSkinInputs assembled;
            if (!assemble_body_skin_inputs(selected, skin_sources, skin_limits, assembled,
                                           skin_error)) {
                error = "skin inputs: " + skin_error;
                return false;
            }
            if (!validate_assembled_skin_inputs(assembled, skin_error)) {
                error = "skin inputs failed validation: " + skin_error;
                return false;
            }
            SkinTargetSurface target_surface;
            if (!read_skin_target_surface(skin_sources, assembled.plan().target_texture,
                                          skin_limits, target_surface, skin_error)) {
                error = "skin target: " + skin_error;
                return false;
            }
            SkinMarkerCache markers;
            if (!compose_runtime_skin(skin_sources, assembled, target_surface, markers,
                                      frame.skin, skin_error)) {
                error = "skin composite: " + skin_error;
                return false;
            }

            // The target hash first, then <car>_SKIN1 unless body_bind is
            // cleared. The generated texture carries the source index one
            // past the real texture sources, as the viewer gives it.
            std::vector<std::uint32_t> bound = {frame.skin.target_hash};
            frame.skin_bound_names.push_back(skin.target_name);
            if (skin.body_bind) {
                const std::string body = skin.car + "_SKIN1";
                bound.push_back(bin_hash(body));
                frame.skin_bound_names.push_back(body);
            }
            if (!bind_runtime_skin(frame.skin, bound, frame.shared_sets.size() + 1,
                                   frame.textures, frame.skin_binding, skin_error)) {
                error = "skin binding: " + skin_error;
                return false;
            }
            for (const SkinInputLayerInfo& input : assembled.plan().inputs) {
                if (!input.usable) {
                    frame.issues.push_back(
                        std::string("skin: input ") + skin_source_role_name(input.role) +
                        " skipped " + skin_skip_reason_name(input.skip_reason) +
                        (input.skip_detail.empty() ? std::string()
                                                   : " (" + input.skip_detail + ")"));
                }
            }
            skin_ok = skin_ok && assembled.plan().skipped_inputs == 0;
            frame.skin_plan = assembled.plan();
            frame.skin_owned_bytes = assembled.owned_bytes();
            frame.skinned = true;
        }

        // The stock scene again, now bound to the decoded textures and,
        // with a skin, to the generated one.
        std::string stock_error;
        if (!prepare_stock_car_scene(sources, frame.textures, frame.parts, frame.car_types,
                                     request, scene_limits, frame.scene, frame.report,
                                     stock_error)) {
            error = "stock car: " + stock_error;
            return false;
        }

        // The viewer's final "geometry.ok() && pack.ok() && sources_ok &&
        // skin_ok && stock_ok"; skin_ok is true when there is no skin.
        bool ok = frame.car_geometry.ok() && frame.car_pack.ok();
        for (const TexturePackSet& set : frame.shared_sets) {
            ok = ok && set.ok();
        }
        ok = ok && skin_ok && frame.parts.ok() && frame.car_types.ok();
        for (const GeometryReport& source : frame.stock_geometry) {
            ok = ok && source.ok();
        }
        frame.inputs_ok = ok;
        if (car_shading) {
            auto installed=frame.report.slots[30].record;
            if (installed==kNoCarPart) {
                const auto name=frame.car_types.records[frame.report.car_type].base_paint_part_hash;
                installed=find_car_part(frame.parts,kAnyCarType,30,name,kNoCarPart,kAnyUpgradeLevel);
            }
            const auto paint=select_car_paint_material_hash(frame.parts,
                installed,
                skin.enabled ? std::optional<std::uint32_t>(skin.selection.base_color)
                             : std::nullopt);
            // 0x56B577..0x56B5AB: an installed WINDOW_TINT part selects the
            // material that replaces the production glass; none installed
            // keeps the geometry's own WINDSHIELD material.
            const auto tint=car_part_material_hash(frame.parts,
                frame.report.slots[115].record);
            if (!apply_car_materials(frame.scene, parts_file.bytes.data(),
                                     parts_file.bytes.size(), paint, tint, error)) return false;
        }
        return true;
    } catch (const std::bad_alloc&) {
        error = "out of memory while preparing the stock frame";
    } catch (const std::exception& caught) {
        error = std::string("failed while preparing the stock frame: ") + caught.what();
    }
    return false;
}

bool prepare_stock_frame(const StockFrameInputs& inputs, const StockSceneRequest& request,
                         const StockFrameSkin& skin, StockFrame& frame, std::string& error,
                         bool car_shading) {
    return prepare_stock_frame_impl(inputs, request, skin, frame, error, nullptr, {}, car_shading);
}

bool prepare_race_car_frame(StockFrameInputs& inputs, const fs::path& root,
                           const StockSceneRequest& request, const StockFrameSkin& skin,
                           StockFrame& frame, std::string& error, bool car_shading) {
    if (!inputs.tuned) return prepare_stock_frame(inputs, request, skin, frame, error, car_shading);
    // All nine inputs were checked by the shared reader. The large vinyl pack
    // need not overlap geometry decoding; re-read it at skin composition.
    std::vector<unsigned char>().swap(inputs.vinyls.bytes);
    return prepare_stock_frame_impl(inputs, request, skin, frame, error, &inputs, root, car_shading);
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
    if (report.overridden != 0) {
        text << " overridden " << report.overridden;
    }
    return text.str();
}

std::vector<std::string> stock_frame_skin_lines(const StockFrame& frame) {
    std::vector<std::string> lines;
    if (!frame.skinned) {
        return lines;
    }
    {
        std::ostringstream text;
        text << "skin: car " << ascii_quoted(frame.skin_plan.car_name) << " path "
             << skin_target_type_name(frame.skin.path) << ' ' << frame.skin.width << 'x'
             << frame.skin.height << " layers " << frame.skin.layers << " owned_bytes "
             << frame.skin_owned_bytes << " skipped_inputs " << frame.skin_plan.skipped_inputs;
        lines.push_back(text.str());
    }
    for (std::size_t i = 0; i < frame.skin_binding.hashes.size(); ++i) {
        std::string line = "skin bound: hash " + hex32(frame.skin_binding.hashes[i]);
        if (i < frame.skin_bound_names.size() && !frame.skin_bound_names[i].empty()) {
            line += ' ' + ascii_quoted(frame.skin_bound_names[i]);
        }
        lines.push_back(line);
    }
    {
        std::ostringstream text;
        text << "skin generated: texture " << frame.skin_binding.texture_index << " source "
             << frame.skin_binding.source << " bytes " << frame.skin_binding.bytes;
        lines.push_back(text.str());
    }
    return lines;
}

} // namespace nfsu
