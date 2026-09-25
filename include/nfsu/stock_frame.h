#pragma once

#include "nfsu/car_parts.h"
#include "nfsu/geometry.h"
#include "nfsu/part_placement.h"
#include "nfsu/render.h"
#include "nfsu/sha256.h"
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
// It exists for the Switch application (switch/car_main.cpp), which cannot
// run the viewer and writes no file, and for the desktop check that the two
// agree (tests/stock_frame_tests.cpp and tests/stock_frame_viewer_match.cmake).
// The viewer does not call it: src/viewer_main.cpp is unchanged, and
// src/stock_frame.cpp names, step by step, the lines of the viewer each
// call mirrors. docs/SWITCH_BUILD.md.

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

// The desktop frame of the command above, as docs/RENDERER.md pins it.
constexpr std::uint64_t kStock240sxFrameBytes = 3686418;
constexpr const char* kStock240sxFrameSha256 =
    "FD6727333A94803A97FD26282CD89DF6F3D3295E0231F4A871A1B14089D28CE9";

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

    const StockFrameInput& file(StockFrameFile which) const noexcept {
        return files[static_cast<std::size_t>(which)];
    }
    bool all_read() const noexcept;
};

// Reads the six files under root, each whole, with std::ifstream opened for
// reading only: nothing is created, written, renamed or removed. Every file
// is attempted whatever became of the others, so one call names every
// missing input. Returns all_read().
bool read_stock_frame_inputs(const std::filesystem::path& root, StockFrameInputs& inputs);

// The pinned command's stock request: car type 240SX, LOD A, the world
// pass, and the wheels and brakes at the culling placements.
StockSceneRequest stock_240sx_request();

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
    // GLOBALB.BUN, then Cars/TEXTURES.BIN.
    std::vector<TexturePackSet> shared_sets;
    // The texture hashes the stock meshes use, ascending, which is all a
    // shared source decodes.
    std::vector<std::uint32_t> wanted;
    RenderTextureSet textures;
    PreparedCarScene scene;
    StockSceneReport report;
    Frame frame;
    RenderStats stats;
    // Every problem a parser or the decode reported, each prefixed with the
    // path of the file it is about, in the order the viewer prints the same
    // problems to its standard error.
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
// of inputs can only ever differ from it.
bool prepare_stock_frame(
    const StockFrameInputs& inputs,
    const StockSceneRequest& request,
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

} // namespace nfsu
