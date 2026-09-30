#pragma once

#include "nfsu/car_parts.h"
#include "nfsu/geometry.h"
#include "nfsu/part_placement.h"
#include "nfsu/render.h"
#include "nfsu/runtime_skin.h"
#include "nfsu/sha256.h"
#include "nfsu/skin_inputs.h"
#include "nfsu/stock_scene.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// The stock 240SX frame of docs/RENDERER.md, made from six files held in
// memory by the calls the desktop viewer makes for its pinned command:
//
//   nfsu_viewer Cars/240SX/GEOMETRY.BIN Cars/240SX/TEXTURES.BIN <out.tga>
//       --shared-textures Global/GLOBALB.BUN --shared-textures Cars/TEXTURES.BIN
//       --stock-car 240SX --stock-parts Global/GLOBALB.BUN
//       --stock-geometry Cars/BRAKES/GEOMETRY.BIN
//       --stock-geometry Cars/PLATES/GEOMETRY.BIN --place-wheels
//
// with every other option at its default. On the desktop that command
// writes kStock240sxFrameBytes bytes whose SHA-256 is kStock240sxFrameSha256.
//
// The painted frame (issue #148) is the same command with the runtime body
// skin composed and bound as the viewer's --skin-car route does it, from
// the same six files:
//
//   ... --place-wheels --skin-car 240SX --skin-doorline plain
//       --skin-base 30 60 160 128
//
// On the desktop that command writes kStock240sxPaintedFrameBytes bytes
// whose SHA-256 is kStock240sxPaintedFrameSha256. stock_240sx_skin() is
// that selection and says where each part of it comes from.
//
// It exists for the Switch application (switch/car_main.cpp), which cannot
// run the viewer and writes no file, and for the desktop check that the two
// agree (tests/stock_frame_tests.cpp and tests/stock_frame_viewer_match.cmake).
// The viewer does not call it: src/stock_frame.cpp names, step by step,
// the lines of the viewer each call mirrors. The tuned preset also reads
// wheel geometry, wheel textures and the car's vinyl pack. docs/SWITCH_BUILD.md.
// The viewer and stock frame take the same window texture alias path.

// The six files, in the order the command above names them. GLOBALB.BUN is
// named twice there - the first shared texture source and the part
// database - and read once here; each use parses it separately, as the
// viewer's two reads do.
enum class StockFrameFile : std::size_t {
    car_geometry,
    car_textures,
    global_bun,
    shared_car_textures,
    brake_geometry,
    plate_geometry
};

constexpr std::size_t kStockFrameFileCount = 6;

// The file's path relative to a root laid out as the game lays out its own
// installation, with '/' separators: "Cars/240SX/GEOMETRY.BIN" and so on.
// Null for an index outside 0..kStockFrameFileCount-1.
const char* stock_frame_file_path(std::size_t index) noexcept;

// Issue #223 re-pinned both frames after retail window aliases and empty
// sponsor decal suppression. The previous hashes were 3DEB2147... and
// 8A5932A3... respectively.
constexpr std::uint64_t kStock240sxFrameBytes = 3686418;
constexpr const char* kStock240sxFrameSha256 =
    "8FBA20E893590A6B48EA1CE40B9A711C74690127811BB5CC0CA208337C63BE84";

// The painted desktop frame, the command above with stock_240sx_skin(), as
// docs/SWITCH_BUILD.md pins it.
constexpr std::uint64_t kStock240sxPaintedFrameBytes = 3686418;
constexpr const char* kStock240sxPaintedFrameSha256 =
    "05203E9CCDEB15B8ECF478B8501777577620DAB94C3FBA6A34F428BA772AF6E0";

// The explicit kit, wheels, paint and vinyl of tuned_240sx_request/skin.
// Re-pinned after Cars/WHEELS/TEXTURES.BIN resolved BBS tread and #225
// resolved stock window aliases, decals and per-copy brake materials.
// tests/tuned_frame_retail.cmake pins three views.
constexpr std::uint64_t kTuned240sxFrameBytes = 3686418;
constexpr const char* kTuned240sxFrameSha256 =
    "F1329A586C6CC352C6AF540981502E6C1E8EDA35C3806D0FED0881E65C3570DB";

// The largest file read: 256 MiB, the limit every parser these files go
// through applies to its input.
constexpr std::uint64_t kStockFrameMaxFileBytes = 256ULL * 1024ULL * 1024ULL;

// The viewer's bound on texture input: the car's pack and every shared
// source together (kMaxTextureInputBytes in src/viewer_main.cpp).
constexpr std::uint64_t kStockFrameMaxTextureInputBytes = 512ULL * 1024ULL * 1024ULL;

struct StockFrameInput {
    // stock_frame_file_path of this file, for every message about it.
    std::string path;
    // True once the whole file is in bytes.
    bool read = false;
    // Why it was not read, when read is false: "failed to open file",
    // "failed to read file size", "file is N bytes; limit is M",
    // "out of memory while reading file" or "failed to read whole file".
    std::string error;
    std::vector<unsigned char> bytes;
};

struct StockFrameInputs {
    std::array<StockFrameInput, kStockFrameFileCount> files;
    bool tuned = false;
    StockFrameInput wheel_geometry;
    StockFrameInput wheel_textures;
    StockFrameInput vinyls;

    const StockFrameInput& file(StockFrameFile which) const noexcept {
        return files[static_cast<std::size_t>(which)];
    }
    bool all_read() const noexcept;
};

// Reads six stock files, plus wheel geometry, wheel textures and vinyls when
// tuned, with std::ifstream opened for reading only: nothing is created,
// written, renamed or removed. Every file
// is attempted whatever became of the others, so one call names every
// missing input. Returns all_read().
bool read_stock_frame_inputs(const std::filesystem::path& root, StockFrameInputs& inputs,
                             bool tuned = false);

// The pinned command's stock request: car type 240SX, LOD A, the world
// pass, and the wheels and brakes at the culling placements.
StockSceneRequest stock_240sx_request();
StockSceneRequest tuned_240sx_request();

// The viewer's runtime body skin options (src/viewer_main.cpp SkinOptions)
// that a stock frame can take: --skin-car, --skin-target, whether
// <car>_SKIN1 is bound, and the customization selection. There is no
// --skin-vinyls for the stock preset: VINYLS.BIN is read only for tuned
// inputs. There is no --skin-dump: nothing is written.
struct StockFrameSkin {
    // False: no skin, and the frame is the unpainted one of #141.
    bool enabled = false;
    // --skin-car NAME. The source names are built from it, and
    // <car>_SKIN1 is the hash the body's material groups name.
    std::string car;
    // --skin-target NAME, hashed with bin_hash.
    std::string target_name = "DUMMY_SKIN1";
    // Bind <car>_SKIN1 as well as the target; --skin-no-body-bind clears it.
    bool body_bind = true;
    // The customization, exactly as the viewer's options fill it. car_name,
    // target_hash and target are set by prepare_stock_frame - to car, to
    // bin_hash(target_name) and to the path the target record proves - as
    // the viewer sets them, so whatever they hold here is not used.
    BodySkinSelection selection;
};

// The painted frame's skin: --skin-car 240SX --skin-doorline plain
// --skin-base 30 60 160 128.
//
// - The doorline is the one the doorline helper at 0x005A96B0 picks for
//   this stock car: no WIDE_BODY part is installed and the FRONT_BUMPER,
//   REAR_BUMPER and BODY parts are all at upgrade level 0
//   (docs/CAR_SCENE.md), so the helper's rule in nfsu/skin_inputs.h gives
//   plain. Its source and mask are in the car's own TEXTURES.BIN.
// - No vinyl, hood or spoiler vinyl: a stock car installs none.
// - The base paint is a choice, not the game's. The front end installs a
//   BASE_PAINT record named by CarTypeInfo +0xC5C (docs/CAR_SCENE.md), and
//   that field is not decoded, so the stock colour is unknown. 30/60/160
//   with GLOSS 128 is the value docs/RENDERER.md already renders the retail
//   skin with. No special colour is set, so all four fall back to it.
// - The target is DUMMY_SKIN1, in the shared Cars/TEXTURES.BIN, and
//   240SX_SKIN1 is bound as well, as the viewer does by default.
StockFrameSkin stock_240sx_skin();
StockFrameSkin tuned_240sx_skin();

// What the frame is made of, kept so that a caller can report on it. It is
// not copyable: it holds several megabytes, and nothing needs a second one.
struct StockFrame {
    StockFrame() = default;
    StockFrame(const StockFrame&) = delete;
    StockFrame& operator=(const StockFrame&) = delete;

    // Geometry source 0 is the car's own file, then BRAKES, then PLATES.
    GeometryReport car_geometry;
    std::vector<GeometryReport> stock_geometry;
    CarPartDatabase parts;
    CarTypeTable car_types;
    TexturePack car_pack;
    TexturePack vinyl_pack;
    // GLOBALB.BUN, then Cars/TEXTURES.BIN, then wheel textures if tuned.
    std::vector<TexturePackSet> shared_sets;
    // The texture hashes the stock meshes use, ascending, which is all a
    // shared source decodes.
    std::vector<std::uint32_t> wanted;
    RenderTextureSet textures;
    PreparedCarScene scene;
    StockSceneReport report;
    Frame frame;
    RenderStats stats;
    // The runtime body skin, set only when prepare_stock_frame was given an
    // enabled StockFrameSkin and composed and bound it. skin_sources are the
    // labels of the skin input sources actually searched, in order, as the
    // viewer prints them ("car-textures", "shared-1", "shared-2", and
    // "shared-3" for tuned wheel textures);
    // skin_bound_names the name each bound hash was made from, in binding
    // order.
    bool skinned = false;
    std::vector<std::string> skin_sources;
    BodySkinInputPlan skin_plan;
    std::uint64_t skin_owned_bytes = 0;
    RuntimeSkin skin;
    RuntimeSkinBinding skin_binding;
    std::vector<std::string> skin_bound_names;
    // Every problem a parser, the decode or the skin reported, each prefixed
    // with the path of the file it is about - "skin:" for a selected skin
    // input that was skipped, which is about no one file - in the order the
    // viewer prints the same problems.
    std::vector<std::string> issues;
    // The viewer's exit status 3 condition, negated: true when no input
    // reported any issue.
    bool inputs_ok = false;
};

// Parses the six files, decodes the textures the stock meshes use and
// prepares the stock scene with them. False, with error set, wherever the
// viewer stops with exit status 2 for the same inputs: no valid mesh in any
// geometry source, a stock assembly the part database or the car type table
// cannot give, or a decode that exceeds its budget. Also false if any input
// was not read: this frame is compared with a pinned one, and a partial set
// of inputs can only ever differ from it. This is the unpainted frame; it
// is the overload below with a StockFrameSkin that is not enabled.
bool prepare_stock_frame(
    const StockFrameInputs& inputs,
    const StockSceneRequest& request,
    StockFrame& frame,
    std::string& error);

// The same, with the runtime body skin when skin.enabled: after the decode
// and before the bound stock scene is prepared, exactly where the viewer
// runs its --skin-car block, the skin input sources are the car's pack and
// then each shared source in order, the inputs are planned, the target path
// is chosen from the target record, the inputs are assembled and checked,
// the target surface is read, the skin is composed and it is bound under
// the target hash and, with body_bind, <car>_SKIN1, as texture source
// 1 + the number of shared sources. Also false, with error set, wherever
// the viewer's skin block stops with exit status 2; a selected input that
// was skipped is an issue and clears inputs_ok, as it makes the viewer
// exit 3.
bool prepare_stock_frame(
    const StockFrameInputs& inputs,
    const StockSceneRequest& request,
    const StockFrameSkin& skin,
    StockFrame& frame,
    std::string& error);

// render_scene over the prepared scene, into frame.frame and frame.stats.
bool render_stock_frame(StockFrame& frame, const RenderOptions& options, std::string& error);

// The frame encoded exactly as write_frame_tga encodes it before writing:
// nfsu::write_tga into a std::ostringstream opened in binary mode, which is
// what src/render.cpp's write_rgba_tga does. Nothing is written anywhere.
struct StockFrameImage {
    std::string tga;
    Sha256Digest sha256{};
};

bool encode_stock_frame(const Frame& frame, StockFrameImage& image, std::string& error);

// The lines the viewer prints about a stock frame's render, character for
// character: its print_stats block, then its "instances:" line.
std::vector<std::string> stock_frame_stats_lines(const RenderStats& stats);

// The viewer's "stock slots:" line for the same report.
std::string stock_frame_slots_line(const StockSceneReport& report);

// The viewer's "skin:", "skin bound:" and "skin generated:" lines for a
// skinned frame, character for character, in the order it prints them.
// Empty for a frame that is not skinned.
std::vector<std::string> stock_frame_skin_lines(const StockFrame& frame);

} // namespace nfsu
