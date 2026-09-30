#include "nfsu/car_parts.h"
#include "nfsu/arg_path.h"
#include "nfsu/geometry.h"
#include "nfsu/inspect.h"
#include "nfsu/inspect_window.h"
#include "nfsu/part_placement.h"
#include "nfsu/render.h"
#include "nfsu/runtime_skin.h"
#include "nfsu/stock_scene.h"
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
#include <optional>
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

// Geometry files a stock car may be assembled from beyond the car's own:
// the retail 240SX needs two, Cars/BRAKES and Cars/PLATES.
constexpr std::size_t kMaxStockGeometrySources = 8;

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
        << "Interactive inspection (docs/INSPECTOR.md). Without it nothing below\n"
        << "runs and the screenshot, the report and the exit code are unchanged:\n"
        << "  --interactive        after writing the screenshot and printing the\n"
        << "                       report, open a desktop window on the same\n"
        << "                       prepared scene: orbit, pan and zoom the camera,\n"
        << "                       click a material group to inspect it, and switch\n"
        << "                       the diagnostic view and overlay. The screenshot\n"
        << "                       is written first and is never affected.\n"
        << "Retail material state (docs/MATERIALS.md). Every option below\n"
        << "the first one needs --material-state retail:\n"
        << "  --material-state M   off or retail (default for --stock-car). retail applies the\n"
        << "  --car-shading M      on/off static-environment approximation (default off)\n"
        << "                       texture-info alpha test and blend selection,\n"
        << "                       the coefficient gates and the bounded\n"
        << "                       transparent draw order\n"
        << "Retail material probes below require --material-state retail:\n"
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
        << "Mesh selection (repeatable, may be mixed; default: all meshes):\n"
        << "  --mesh-index N       the mesh at source position N\n"
        << "  --mesh-name NAME     the one mesh whose stored name is exactly NAME\n"
        << "  --mesh-hash HEX      the one mesh with this verified directory hash\n"
        << "                       Selected meshes are drawn in source order. At\n"
        << "                       most 256 selectors and 16384 name bytes. A\n"
        << "                       selection is a diagnostic subset, not a decoded\n"
        << "                       game car assembly.\n"
        << "Stock car assembly (docs/CAR_SCENE.md). It replaces the mesh selection\n"
        << "above and cannot be combined with it; without --stock-car nothing below\n"
        << "is read and the report is unchanged:\n"
        << "  --stock-car NAME     draw the installed stock parts of car type NAME,\n"
        << "                       each where the recovered placement puts it\n"
        << "  --stock-parts F      GLOBAL/GLOBALB.BUN, which holds both the car part\n"
        << "                       database and the car type table. Required.\n"
        << "  --stock-geometry F   another geometry file the parts may come from,\n"
        << "                       searched after <GEOMETRY.BIN> in the order given;\n"
        << "                       the retail 240SX needs Cars/BRAKES/GEOMETRY.BIN\n"
        << "                       and Cars/PLATES/GEOMETRY.BIN. At most 8.\n"
        << "  --stock-lod L        A (default), B, C or D\n"
        << "  --stock-part SLOT NAME  override one installed part (repeatable);\n"
        << "                       SLOT is a number or one of the names below.\n"
        << "                       NAME is the internal part name, such as\n"
        << "                       240SX_KIT01_FRONT_BUMPER.\n"
        << "  --stock-pass P       world (default) or front-end; the brake model is\n"
        << "                       the only part whose LOD this changes\n"
        << "  --place-wheels       also draw the wheels and brakes, at the culling\n"
        << "                       placements. Those are a documented stand-in and\n"
        << "                       NOT the matrix the game draws them with: the\n"
        << "                       lateral component is a term this project did not\n"
        << "                       reproduce, and the spin and steering the draw\n"
        << "                       applies are not there at all. The left pair is\n"
        << "                       turned as the draw turns it: the wheels by half\n"
        << "                       a turn, the brakes reflected. Without\n"
        << "                       this option both slots are reported\n"
        << "                       'placement unreproduced' and draw nothing.\n"
        << "Runtime body skin (off unless --skin-car is given; without it nothing\n"
        << "below is read and the report is unchanged):\n"
        << "  --skin-car NAME      compose the body skin for car NAME and bind it.\n"
        << "                       Skin input packs, in priority order: the car's\n"
        << "                       <TEXTURES.BIN>, then --skin-vinyls, then each\n"
        << "                       --shared-textures in order. First claim wins.\n"
        << "  --skin-vinyls F      the car's VINYLS.BIN; counts against the same\n"
        << "                       512 MiB texture input budget\n"
        << "  --skin-target NAME   composite target name, default DUMMY_SKIN1\n"
        << "  --skin-target-hash H composite target hash instead of a name\n"
        << "  --skin-no-body-bind  do not bind <NAME>_SKIN1, only the target hash\n"
        << "  --skin-bind NAME     also bind this texture name (repeatable)\n"
        << "  --skin-bind-hash H   also bind this texture hash (repeatable)\n"
        << "                       At most 3 bound hashes in total, the cap a pack\n"
        << "                       record has.\n"
        << "  --skin-dump FILE     write the generated skin as a TGA; never\n"
        << "                       replaces an existing file\n"
        << "Customization, all explicit; nothing is defaulted that the game reads\n"
        << "from a part:\n"
        << "  --skin-hood V        hood-spoiler, hood or spoiler (source index 0)\n"
        << "  --skin-doorline V    plain, kit or widebody (source index 5)\n"
        << "  --skin-vinyl N TEXTURE NUMCOLOURS remap|no-remap\n"
        << "                       vinyl layer N in 0..3; NUMCOLOURS 1..256.\n"
        << "                       A layer with no NUMCOLOURS, or zero, abandons\n"
        << "                       the whole composite in the game, so it cannot\n"
        << "                       be selected here.\n"
        << "  --skin-vinyl-color N SLOT R G B GLOSS\n"
        << "                       remap color SLOT in 0..3 of vinyl layer N\n"
        << "  --skin-base R G B GLOSS        the BASE_PAINT color\n"
        << "  --skin-special SLOT R G B GLOSS\n"
        << "                       slot 0 engine, 1 spoiler, 2 brake, 3 exhaust;\n"
        << "                       an unset slot falls back to the base color\n"
        << "Part slots (id:name):\n";
    for (int slot = 0; slot < static_cast<int>(nfsu::kCarSlotCount); ++slot) {
        std::cout << "  " << slot << ':' << nfsu::car_slot_name(slot)
                  << (slot % 3 == 2 ? '\n' : ' ');
    }
    if (nfsu::kCarSlotCount % 3 != 0) {
        std::cout << '\n';
    }
}

// Printable ASCII; quote, backslash and other bytes as \xHH.
//
// Not named `quoted`: <iomanip> declares a non-const std::quoted overload
// that wins by argument-dependent lookup for a non-const std::string
// lvalue, which would silently print raw bytes instead of escaping them.
std::string ascii_quoted(const std::string& text) {
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

std::string hex8(std::uint8_t value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", static_cast<unsigned int>(value));
    return buffer;
}

// One resolved skin input or target record, located exactly.
std::string skin_provenance(const nfsu::SkinTextureProvenance& record) {
    if (!record.resolved) {
        return "unresolved";
    }
    std::string text =
        "source " + std::to_string(record.source_index) + " pack " +
        std::to_string(record.pack_index) + " texture " +
        std::to_string(record.record_index) + " " +
        nfsu::pixel_format_name(record.format) + " code " + hex8(record.format_code) +
        " d3d " + std::to_string(record.d3d_format) + " " + std::to_string(record.width) +
        "x" + std::to_string(record.height) + " mips " + std::to_string(record.mip_count);
    if (!record.record_valid) {
        text += " invalid";
    }
    if (!record.also_claimed_by.empty()) {
        text += " also_claimed_by " + std::to_string(record.also_claimed_by.size());
    }
    return text;
}

// Everything the runtime body skin needs, all of it stated on the command
// line. The viewer invents no customization state: a vinyl layer, a
// doorline variant, a hood/spoiler variant or a paint color that is not
// given here is simply not selected.
struct SkinOptions {
    bool enabled = false;
    // True once any --skin-* option was seen, so that the rest without
    // --skin-car is a usage error rather than silently ignored input.
    bool any_option = false;
    std::string car;
    fs::path vinyls;
    bool has_vinyls = false;
    std::string target_name = "DUMMY_SKIN1";
    // Set by --skin-target-hash; then target_name is not used.
    std::optional<std::uint32_t> target_hash;
    // Bind <car>_SKIN1, the formatted hash the retail render path replaces
    // with RideInfo+0x444 at 0x00580C37.
    bool body_bind = true;
    std::vector<std::uint32_t> extra_hashes;
    // Parallel to extra_hashes; empty for a hash given as a number.
    std::vector<std::string> extra_names;
    fs::path dump;
    bool has_dump = false;
    nfsu::BodySkinSelection selection;
};

// The stock-car profile: which car, which databases and geometry files it
// is assembled from, and the two choices the retail draw makes per frame.
// Nothing here is defaulted into existence: without --stock-car the viewer
// draws the mesh selection it always did.
struct StockOptions {
    bool enabled = false;
    // True once any --stock-* option or --place-wheels was seen, so that
    // the rest without --stock-car is a usage error rather than silently
    // ignored input.
    bool any_option = false;
    std::string car;
    fs::path parts;
    bool has_parts = false;
    // Extra geometry sources, after the car's own <GEOMETRY.BIN>.
    std::vector<fs::path> geometry;
    std::size_t lod = 0;
    nfsu::CarDrawPass pass = nfsu::CarDrawPass::world;
    bool place_wheels = false;
    nfsu::InstalledPartSelection installed_parts;
};

nfsu::StockSceneRequest stock_request(const StockOptions& stock) {
    nfsu::StockSceneRequest request;
    request.car_type_name = stock.car;
    request.lod = stock.lod;
    request.pass = stock.pass;
    request.place_wheels_from_culling_table = stock.place_wheels;
    request.installed_parts = stock.installed_parts;
    return request;
}

// Four 0..255 channel values in the lane order every BodySkinRequest field
// uses: RED, GREEN, BLUE, GLOSS.
bool parse_paint(char** values, std::uint32_t& color) {
    std::uint8_t lanes[4] = {0, 0, 0, 0};
    for (std::size_t i = 0; i < 4; ++i) {
        std::uint64_t value = 0;
        if (!parse_uint(values[i], 255, value)) {
            return false;
        }
        lanes[i] = static_cast<std::uint8_t>(value);
    }
    color = nfsu::skin_pack_paint(lanes[0], lanes[1], lanes[2], lanes[3]);
    return true;
}

bool parse_small(const char* text, std::uint64_t max, std::uint64_t& value) {
    return parse_uint(std::string(text), max, value);
}

// Handles one --skin-* option. Returns 0 when the option is not a skin
// option, 1 when it was consumed, and 2 after printing an error.
int parse_skin_option(
    int argc,
    char** argv,
    int& i,
    SkinOptions& skin,
    std::set<std::string>& seen_once) {
    const std::string name = argv[i];
    if (name.rfind("--skin", 0) != 0) {
        return 0;
    }
    skin.any_option = true;
    const int left = argc - i - 1;
    const auto need = [&](int count) {
        if (left < count) {
            std::cerr << "error: " << name << " needs " << count << " values\n";
            return false;
        }
        return true;
    };
    const auto once = [&]() {
        if (!seen_once.insert(name).second) {
            std::cerr << "error: option given twice: " << name << '\n';
            return false;
        }
        return true;
    };
    const auto bad = [&]() {
        std::cerr << "error: invalid value for " << name << '\n';
        return 2;
    };

    if (name == "--skin-car") {
        if (!once() || !need(1)) {
            return 2;
        }
        skin.enabled = true;
        skin.car = argv[i + 1];
        if (skin.car.empty()) {
            return bad();
        }
        skin.selection.car_name = skin.car;
        i += 2;
        return 1;
    }
    if (name == "--skin-vinyls") {
        if (!once() || !need(1)) {
            return 2;
        }
        if (argv[i + 1][0] == '\0') {
            return bad();
        }
        skin.vinyls = nfsu::argument_path(argv[i + 1]);
        skin.has_vinyls = true;
        i += 2;
        return 1;
    }
    if (name == "--skin-dump") {
        if (!once() || !need(1)) {
            return 2;
        }
        if (argv[i + 1][0] == '\0') {
            return bad();
        }
        skin.dump = nfsu::argument_path(argv[i + 1]);
        skin.has_dump = true;
        i += 2;
        return 1;
    }
    if (name == "--skin-target") {
        if (!once() || !need(1)) {
            return 2;
        }
        skin.target_name = argv[i + 1];
        if (skin.target_name.empty()) {
            return bad();
        }
        i += 2;
        return 1;
    }
    if (name == "--skin-target-hash") {
        if (!once() || !need(1)) {
            return 2;
        }
        std::uint32_t hash = 0;
        if (!parse_hex32(argv[i + 1], hash)) {
            return bad();
        }
        skin.target_hash = hash;
        i += 2;
        return 1;
    }
    if (name == "--skin-no-body-bind") {
        if (!once()) {
            return 2;
        }
        skin.body_bind = false;
        i += 1;
        return 1;
    }
    if (name == "--skin-bind" || name == "--skin-bind-hash") {
        if (!need(1)) {
            return 2;
        }
        std::uint32_t hash = 0;
        std::string label;
        if (name == "--skin-bind") {
            label = argv[i + 1];
            if (label.empty()) {
                return bad();
            }
            hash = nfsu::bin_hash(label);
        } else if (!parse_hex32(argv[i + 1], hash)) {
            return bad();
        }
        if (skin.extra_hashes.size() >= nfsu::kMaxRuntimeSkinHashes) {
            std::cerr << "error: at most " << nfsu::kMaxRuntimeSkinHashes
                      << " hashes can be bound to the generated skin\n";
            return 2;
        }
        skin.extra_hashes.push_back(hash);
        skin.extra_names.push_back(label);
        i += 2;
        return 1;
    }
    if (name == "--skin-hood") {
        if (!once() || !need(1)) {
            return 2;
        }
        nfsu::SkinHoodSpoilerSelection hood;
        const std::string variant = argv[i + 1];
        if (variant == "hood-spoiler") {
            hood.variant = nfsu::SkinHoodSpoilerVariant::hood_and_spoiler;
        } else if (variant == "hood") {
            hood.variant = nfsu::SkinHoodSpoilerVariant::hood;
        } else if (variant == "spoiler") {
            hood.variant = nfsu::SkinHoodSpoilerVariant::spoiler;
        } else {
            return bad();
        }
        skin.selection.hood_spoiler_vinyl = hood;
        i += 2;
        return 1;
    }
    if (name == "--skin-doorline") {
        if (!once() || !need(1)) {
            return 2;
        }
        nfsu::SkinDoorlineSelection doorline;
        const std::string variant = argv[i + 1];
        if (variant == "plain") {
            doorline.variant = nfsu::SkinDoorlineVariant::plain;
        } else if (variant == "kit") {
            doorline.variant = nfsu::SkinDoorlineVariant::body_kit;
        } else if (variant == "widebody") {
            doorline.variant = nfsu::SkinDoorlineVariant::wide_body;
        } else {
            return bad();
        }
        skin.selection.doorline = doorline;
        i += 2;
        return 1;
    }
    if (name == "--skin-base") {
        if (!once() || !need(4)) {
            return 2;
        }
        if (!parse_paint(argv + i + 1, skin.selection.base_color)) {
            return bad();
        }
        i += 5;
        return 1;
    }
    if (name == "--skin-special") {
        if (!need(5)) {
            return 2;
        }
        std::uint64_t slot = 0;
        std::uint32_t color = 0;
        if (!parse_small(argv[i + 1], 3, slot) || !parse_paint(argv + i + 2, color)) {
            return bad();
        }
        if (skin.selection.special_colors[slot]) {
            std::cerr << "error: " << name << " names slot " << slot
                      << " a second time\n";
            return 2;
        }
        skin.selection.special_colors[slot] = color;
        i += 6;
        return 1;
    }
    if (name == "--skin-vinyl") {
        if (!need(4)) {
            return 2;
        }
        std::uint64_t layer = 0;
        if (!parse_small(argv[i + 1], 3, layer)) {
            return bad();
        }
        if (skin.selection.vinyl_layers[layer]) {
            // Replacing it would silently drop any --skin-vinyl-color
            // already given for that layer.
            std::cerr << "error: " << name << " names layer " << layer
                      << " a second time\n";
            return 2;
        }
        nfsu::SkinVinylLayerSelection selected;
        selected.texture = argv[i + 2];
        if (selected.texture.empty()) {
            return bad();
        }
        // 1..256 only. A vinyl layer whose part carries no NUMCOLOURS, or
        // carries zero, abandons the whole composite through the epilogue
        // at 0x005A9F9A, so neither is a selection the viewer can make.
        std::uint64_t count = 0;
        if (!parse_small(argv[i + 3], 256, count) || count == 0) {
            return bad();
        }
        selected.palette_size = static_cast<std::uint32_t>(count);
        const std::string remap = argv[i + 4];
        if (remap == "remap") {
            selected.remap = true;
        } else if (remap != "no-remap") {
            return bad();
        }
        skin.selection.vinyl_layers[layer] = selected;
        i += 5;
        return 1;
    }
    if (name == "--skin-vinyl-color") {
        if (!need(6)) {
            return 2;
        }
        std::uint64_t layer = 0;
        std::uint64_t slot = 0;
        std::uint32_t color = 0;
        if (!parse_small(argv[i + 1], 3, layer) || !parse_small(argv[i + 2], 3, slot) ||
            !parse_paint(argv + i + 3, color)) {
            return bad();
        }
        if (!skin.selection.vinyl_layers[layer]) {
            std::cerr << "error: " << name << " names layer " << layer
                      << ", which is not selected\n";
            return 2;
        }
        if (skin.selection.vinyl_layers[layer]->remap_colors[slot]) {
            std::cerr << "error: " << name << " names layer " << layer << " slot " << slot
                      << " a second time\n";
            return 2;
        }
        skin.selection.vinyl_layers[layer]->remap_colors[slot] = color;
        i += 7;
        return 1;
    }
    std::cerr << "error: unknown option " << name << '\n';
    return 2;
}

// Returns 0 on success, otherwise the exit code after printing an error.
int parse_options(
    int argc,
    char** argv,
    nfsu::RenderOptions& options,
    std::vector<fs::path>& shared,
    nfsu::MeshSelection& selection,
    SkinOptions& skin,
    StockOptions& stock,
    bool& interactive) {
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
        {
            // parse_skin_option consumes the whole option, so the loop's
            // own increment is undone before continuing.
            int next = i;
            const int handled = parse_skin_option(argc, argv, next, skin, seen);
            if (handled == 2) {
                return 1;
            }
            if (handled == 1) {
                i = next - 1;
                continue;
            }
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
            shared.push_back(nfsu::argument_path(argv[++i]));
            continue;
        }
        if (name == "--stock-geometry") {
            stock.any_option = true;
            if (i + 1 >= argc) {
                std::cerr << "error: missing value for " << name << '\n';
                return 1;
            }
            if (stock.geometry.size() >= kMaxStockGeometrySources) {
                std::cerr << "error: at most " << kMaxStockGeometrySources
                          << " --stock-geometry files are allowed\n";
                return 1;
            }
            if (argv[i + 1][0] == '\0') {
                std::cerr << "error: invalid value for " << name << '\n';
                return 1;
            }
            stock.geometry.push_back(nfsu::argument_path(argv[++i]));
            continue;
        }
        if (name == "--stock-part") {
            stock.any_option = true;
            if (i + 2 >= argc) {
                std::cerr << "error: --stock-part needs SLOT NAME\n";
                return 1;
            }
            const std::string label = argv[++i];
            std::size_t slot = nfsu::kCarSlotCount;
            std::uint64_t number = 0;
            if (parse_uint(label, nfsu::kCarSlotCount - 1, number)) {
                slot = static_cast<std::size_t>(number);
            } else {
                for (std::size_t candidate = 0; candidate < nfsu::kCarSlotCount; ++candidate) {
                    if (label == nfsu::car_slot_name(static_cast<int>(candidate))) {
                        slot = candidate;
                        break;
                    }
                }
            }
            const std::string part = argv[++i];
            if (slot == nfsu::kCarSlotCount || part.empty()) {
                std::cerr << "error: invalid SLOT or NAME for --stock-part\n";
                return 1;
            }
            if (stock.installed_parts.names[slot]) {
                std::cerr << "error: --stock-part names slot " << slot << ' '
                          << nfsu::car_slot_name(static_cast<int>(slot))
                          << " a second time\n";
                return 1;
            }
            stock.installed_parts.names[slot] = part;
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
        if (name == "--place-wheels") {
            stock.any_option = true;
            stock.place_wheels = true;
            continue;
        }
        if (name == "--interactive") {
            // Refused here, before any file is opened, on a build with no
            // window backend: an option that silently did nothing would be
            // worse than a usage error.
            if (!nfsu::inspection_window_supported()) {
                std::cerr << "error: --interactive needs a desktop window backend "
                             "and this build has none\n";
                return 1;
            }
            interactive = true;
            continue;
        }
        static const std::set<std::string> valued = {
            "--width", "--height", "--alpha-cutoff", "--yaw", "--pitch",
            "--distance", "--fov", "--near", "--far", "--target", "--cull",
            "--shading", "--car-shading", "--material-state", "--material-alpha", "--environment",
            "--environment-color", "--environment-global", "--stock-car",
            "--stock-parts", "--stock-lod", "--stock-pass"};
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
        } else if (name == "--car-shading") {
            if (value!="on" && value!="off") { std::cerr << "error: --car-shading needs on or off\n"; return 2; }
            options.car_shading=value=="on";
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
        } else if (name == "--stock-car") {
            stock.any_option = true;
            stock.enabled = !value.empty();
            stock.car = value;
            ok = stock.enabled;
        } else if (name == "--stock-parts") {
            stock.any_option = true;
            ok = !value.empty();
            stock.parts = nfsu::argument_path(value.c_str());
            stock.has_parts = ok;
        } else if (name == "--stock-lod") {
            stock.any_option = true;
            ok = value.size() == 1 && value[0] >= 'A' &&
                 value[0] < 'A' + static_cast<char>(nfsu::kCarPartLodCount);
            if (ok) {
                stock.lod = static_cast<std::size_t>(value[0] - 'A');
            }
        } else if (name == "--stock-pass") {
            stock.any_option = true;
            if (value == "world") {
                stock.pass = nfsu::CarDrawPass::world;
            } else if (value == "front-end") {
                stock.pass = nfsu::CarDrawPass::front_end;
            } else {
                ok = false;
            }
        }
        if (!ok) {
            std::cerr << "error: invalid value for " << name << ": " << value << '\n';
            return 1;
        }
    }
    // A stock assembly uses the same material state as the retail car draw.
    // Explicit --material-state off remains available for diagnostics.
    if (stock.enabled && seen.count("--material-state") == 0)
        options.material_state = nfsu::MaterialStateMode::retail;
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
    if (skin.any_option && !skin.enabled) {
        std::cerr << "error: the --skin-* options need --skin-car NAME\n";
        return 1;
    }
    if (stock.any_option && !stock.enabled) {
        std::cerr << "error: the --stock-* options and --place-wheels need "
                     "--stock-car NAME\n";
        return 1;
    }
    if (stock.enabled && !stock.has_parts) {
        std::cerr << "error: --stock-car needs --stock-parts <GLOBALB.BUN>, which "
                     "holds the part database and the car type table\n";
        return 1;
    }
    // Two profiles at once would mean drawing a decoded assembly and a
    // hand-named subset in one frame, and only one of them can be the
    // scene.
    if (stock.enabled && !selection.selectors.empty()) {
        std::cerr << "error: --stock-car draws the decoded stock assembly, so it "
                     "cannot be combined with --mesh-index, --mesh-name or "
                     "--mesh-hash\n";
        return 1;
    }
    // The target hash, the optional <car>_SKIN1 hash and every extra one.
    const std::size_t bound =
        1 + (skin.body_bind ? 1U : 0U) + skin.extra_hashes.size();
    if (skin.enabled && bound > nfsu::kMaxRuntimeSkinHashes) {
        std::cerr << "error: " << bound << " hashes would be bound to the generated "
                  << "skin, at most " << nfsu::kMaxRuntimeSkinHashes << " are allowed\n";
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
    // Before anything is parsed, opened or written. Every argument goes
    // through the same check, not only the three below and the path-valued
    // options: the rest are option names, numbers and retail texture, mesh
    // and car names, and none of those has a spelling this refuses.
    // docs/PATH_ARGUMENTS.md says what is accepted and what is not.
    {
        std::string argument_error;
        if (!nfsu::check_arguments(argc, argv, 1, argument_error)) {
            std::cerr << "error: " << argument_error << '\n';
            return 1;
        }
    }
    const fs::path geometry_path = nfsu::argument_path(argv[1]);
    const fs::path texture_path = nfsu::argument_path(argv[2]);
    const fs::path output_path = nfsu::argument_path(argv[3]);

    nfsu::RenderOptions options;
    std::vector<fs::path> shared_paths;
    nfsu::MeshSelection selection;
    SkinOptions skin;
    StockOptions stock;
    bool interactive = false;
    const int parse_result = parse_options(argc, argv, options, shared_paths, selection,
                                           skin, stock, interactive);
    if (parse_result != 0) {
        return parse_result;
    }
    std::vector<fs::path> inputs = {geometry_path, texture_path};
    inputs.insert(inputs.end(), shared_paths.begin(), shared_paths.end());
    // Appended after the texture sources so that the texture-source
    // indexing below is unchanged; it is an input for the path checks and
    // for the writers, not a material texture source. Its own position is
    // remembered rather than assumed to be the last: the stock car appends
    // more inputs after it, and the check below would otherwise compare it
    // with itself and refuse every run that names both.
    const std::size_t vinyl_input = inputs.size();
    if (skin.has_vinyls) {
        inputs.push_back(skin.vinyls);
    }
    // The stock-car inputs come after those, for the same reason. They are
    // deliberately not cross-checked against the texture sources: GLOBALB.BUN
    // is normally both --stock-parts and a --shared-textures source, and
    // naming one file twice among read-only inputs costs a second read and
    // nothing else. What is checked below is that no geometry source is
    // named twice, because that one does change the answer.
    if (stock.enabled) {
        inputs.push_back(stock.parts);
        inputs.insert(inputs.end(), stock.geometry.begin(), stock.geometry.end());
    }

    // Fail early; write_frame_tga repeats these checks before writing.
    //
    // Every comparison here is nfsu::same_file_path, which answers for
    // paths that do not exist yet: the output, its ".partial" and the dump
    // are all names of files that are not there. Comparing only the ones
    // that do exist would leave exactly the pairs this block exists to
    // refuse uncompared.
    {
        std::error_code error;
        fs::path partial = output_path;
        partial += ".partial";
        for (const fs::path& source : inputs) {
            if (nfsu::same_file_path(source, output_path) ||
                nfsu::same_file_path(source, partial)) {
                std::cerr << "error: output path must differ from every input file\n";
                return 1;
            }
        }
        // A texture source named twice would only shadow itself.
        for (std::size_t i = 0; i < shared_paths.size(); ++i) {
            for (std::size_t j = 1; j < i + 2; ++j) {
                if (nfsu::same_file_path(shared_paths[i], inputs[j])) {
                    std::cerr << "error: shared texture source " << (i + 1)
                              << " names the same file as texture source "
                              << (j - 1) << '\n';
                    return 1;
                }
            }
        }
        if (skin.has_vinyls) {
            for (std::size_t j = 0; j < inputs.size(); ++j) {
                if (j == vinyl_input) {
                    continue;
                }
                if (nfsu::same_file_path(skin.vinyls, inputs[j])) {
                    std::cerr << "error: --skin-vinyls names the same file as another "
                                 "input\n";
                    return 1;
                }
            }
            // The output does not exist yet, so it is compared by name too.
            if (nfsu::same_file_path(skin.vinyls, output_path) ||
                nfsu::same_file_path(skin.vinyls, partial)) {
                std::cerr << "error: output path must differ from every input file\n";
                return 1;
            }
        }
        if (fs::exists(fs::symlink_status(output_path, error))) {
            std::cerr << "error: refusing to overwrite the output file\n";
            return 1;
        }
        if (skin.has_dump) {
            // Neither the dump nor the render output exists yet, so
            // fs::equivalent cannot compare them and nfsu::same_file_path
            // compares the names. What it sees through, and what it does
            // not, is listed at its declaration; a spelling it lets through
            // matters here more than anywhere else, because the dump is
            // written after the composite and before the render, so the
            // generated texture would be published at the screenshot's path
            // and only then the render would fail. write_rgba_tga still
            // refuses to replace anything, which is why nothing is ever
            // lost, but the file the caller gets would be the wrong one.
            fs::path dump_partial = skin.dump;
            dump_partial += ".partial";
            for (const fs::path& source : inputs) {
                if (nfsu::same_file_path(source, skin.dump) ||
                    nfsu::same_file_path(source, dump_partial)) {
                    std::cerr << "error: --skin-dump must differ from every input file\n";
                    return 1;
                }
            }
            if (nfsu::same_file_path(skin.dump, output_path) ||
                nfsu::same_file_path(skin.dump, partial) ||
                nfsu::same_file_path(dump_partial, output_path)) {
                std::cerr << "error: --skin-dump must differ from the output file\n";
                return 1;
            }
            if (fs::exists(fs::symlink_status(skin.dump, error))) {
                std::cerr << "error: refusing to overwrite the --skin-dump file\n";
                return 1;
            }
        }
    }

    // A stock part may live in any of the geometry files the game has
    // loaded, and the hash lookup takes the first source that holds it, so
    // one file named twice would report every one of its hashes as
    // ambiguous and change which source the report blames.
    if (stock.enabled) {
        for (std::size_t i = 0; i < stock.geometry.size(); ++i) {
            if (nfsu::same_file_path(stock.geometry[i], geometry_path)) {
                std::cerr << "error: --stock-geometry " << (i + 1)
                          << " names the same file as <GEOMETRY.BIN>\n";
                return 1;
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (nfsu::same_file_path(stock.geometry[i], stock.geometry[j])) {
                    std::cerr << "error: --stock-geometry " << (i + 1)
                              << " names the same file as --stock-geometry "
                              << (j + 1) << '\n';
                    return 1;
                }
            }
        }
    }

    nfsu::GeometryReport geometry = nfsu::load_geometry_file(geometry_path);
    if (stock.enabled) {
        nfsu::alias_stock_window_textures(geometry);
    }
    for (const nfsu::GeometryIssue& issue : geometry.issues) {
        std::cerr << "geometry issue at " << issue.offset << ": "
                  << issue.message << '\n';
    }
    // Geometry source 0 is always the car's own file; each --stock-geometry
    // follows in the order given, which is the order the solid lookup
    // searches them in.
    std::vector<nfsu::GeometryReport> stock_geometry;
    std::vector<const nfsu::GeometryReport*> stock_sources;
    nfsu::CarPartDatabase stock_parts;
    nfsu::CarTypeTable stock_types;
    if (stock.enabled) {
        stock_geometry.reserve(stock.geometry.size());
        for (std::size_t i = 0; i < stock.geometry.size(); ++i) {
            stock_geometry.push_back(nfsu::load_geometry_file(stock.geometry[i]));
            nfsu::alias_stock_window_textures(stock_geometry.back());
            for (const nfsu::GeometryIssue& issue : stock_geometry.back().issues) {
                std::cerr << "stock geometry " << (i + 1) << " issue at "
                          << issue.offset << ": " << issue.message << '\n';
            }
        }
        stock_sources.push_back(&geometry);
        for (const nfsu::GeometryReport& source : stock_geometry) {
            stock_sources.push_back(&source);
        }
        stock_parts = nfsu::load_car_part_database(stock.parts);
        for (const nfsu::CarPartIssue& issue : stock_parts.issues) {
            std::cerr << "car part issue at " << issue.offset << ": " << issue.message
                      << '\n';
        }
        stock_types = nfsu::load_car_type_table(stock.parts);
        for (const nfsu::CarTypeIssue& issue : stock_types.issues) {
            std::cerr << "car type issue at " << issue.offset << ": " << issue.message
                      << '\n';
        }
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
    // Each file is refused before it is read if it would take the bytes
    // read so far past kMaxTextureInputBytes. The car's pack is at most
    // 256 MiB, so read_bytes never exceeds the limit.
    std::uint64_t read_bytes = bytes.size();
    // The car's VINYLS.BIN is a skin input source only: it never becomes a
    // material texture source and is read only when --skin-vinyls is given.
    // It is read here because it is searched right after the car's pack, so
    // it takes its share of the budget in that order.
    std::vector<unsigned char> vinyl_bytes;
    nfsu::TexturePack vinyl_pack;
    if (skin.has_vinyls) {
        nfsu::TextureLimits vinyl_limits;
        vinyl_limits.max_file_size =
            std::min(vinyl_limits.max_file_size, kMaxTextureInputBytes - read_bytes);
        vinyl_pack = nfsu::load_texture_pack(skin.vinyls, vinyl_bytes, vinyl_limits);
        read_bytes += vinyl_bytes.size();
        for (const nfsu::TextureIssue& issue : vinyl_pack.issues) {
            std::cerr << "skin vinyls issue at " << issue.offset << ": " << issue.message
                      << '\n';
        }
    }
    // Shared source k is texture source k; the car's pack is source 0.
    // Both vectors are sized once, so the pointers below stay valid.
    std::vector<std::vector<unsigned char>> shared_bytes(shared_paths.size());
    std::vector<nfsu::TexturePackSet> shared_sets(shared_paths.size());
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
    // A stock car may draw a part out of any of its geometry sources, so
    // what has to hold meshes is the set of them and not the car's own
    // file. The message is unchanged, and so is the check for every other
    // profile, which reads one source.
    std::size_t valid_meshes = geometry.valid_mesh_count();
    for (const nfsu::GeometryReport& source : stock_geometry) {
        valid_meshes += source.valid_mesh_count();
    }
    if (valid_meshes == 0) {
        std::cerr << "error: no valid meshes to render\n";
        return 2;
    }
    // Which meshes a stock car draws comes from the part database, not from
    // the command line, so the hashes worth decoding out of a shared source
    // are known only once the assembly has been resolved. It is resolved
    // twice: here against no textures at all, and below against the decoded
    // set. Both resolutions select the same meshes, because which part a
    // slot installs and which mesh hash it names are decided before any
    // material is bound; only the bindings of the second one are used.
    std::vector<nfsu::SceneMeshRef> stock_mesh_refs;
    bool wants_left_brake = false;
    if (stock.enabled) {
        nfsu::PreparedCarScene unbound;
        nfsu::StockSceneReport unbound_report;
        std::string error;
        if (!nfsu::prepare_stock_car_scene(stock_sources, nfsu::RenderTextureSet{},
                                           stock_parts, stock_types,
                                           stock_request(stock), scene_limits, unbound,
                                           unbound_report, error)) {
            std::cerr << "error: stock car: " << error << '\n';
            return 2;
        }
        for (const nfsu::PreparedMesh& mesh : unbound.meshes) {
            stock_mesh_refs.push_back(nfsu::SceneMeshRef{mesh.source, mesh.source_index});
        }
        wants_left_brake = std::any_of(unbound.instances.begin(), unbound.instances.end(),
            [](const nfsu::PreparedInstance& instance) {
                return instance.part_slot == nfsu::kSlotBrake &&
                       (instance.copy == 0 || instance.copy == 3);
            });
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
            // Without selectors these are all valid meshes; for a stock car
            // they are the meshes its installed parts resolved to, in any
            // of its geometry sources.
            const auto want = [&wanted](const nfsu::Mesh& mesh) {
                for (const nfsu::MeshMaterial& material : mesh.materials) {
                    if (material.texture_slot < mesh.texture_hashes.size()) {
                        wanted.push_back(mesh.texture_hashes[material.texture_slot]);
                    }
                }
            };
            if (stock.enabled) {
                for (const nfsu::SceneMeshRef& ref : stock_mesh_refs) {
                    want(stock_sources[ref.source]->meshes[ref.index]);
                }
                if (wants_left_brake) {
                    wanted.push_back(nfsu::kBrakeGlobalLeftHash); // BRAKE_GLOBAL_LEFT
                }
            } else {
                for (const std::size_t index : selected) {
                    want(geometry.meshes[index]);
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
    // What each shared source contributed, counted here so that the report
    // describes the decode even when the runtime skin displaces some of it.
    std::vector<std::size_t> shared_decoded(shared_paths.size(), 0);
    std::vector<std::uint64_t> shared_decoded_bytes(shared_paths.size(), 0);
    for (const nfsu::RenderTexture& texture : textures.textures) {
        if (texture.valid && texture.source >= 1 &&
            texture.source <= shared_paths.size()) {
            ++shared_decoded[texture.source - 1];
            shared_decoded_bytes[texture.source - 1] += texture.rgba.size();
        }
    }
    const std::uint64_t decode_total_bytes = textures.decoded_bytes;

    // Runtime body skin. Nothing below this point runs, reads a file or
    // prints a line unless --skin-car was given.
    nfsu::AssembledSkinInputs skin_inputs;
    nfsu::RuntimeSkin skin_texture;
    nfsu::RuntimeSkinBinding skin_binding;
    nfsu::SkinTargetChoice skin_choice;
    std::vector<std::string> skin_bound_labels;
    // The label of every skin input source actually searched, in order, so
    // that the source indexes in the report are never ambiguous.
    std::vector<std::string> skin_source_labels;
    bool skin_ok = true;
    if (skin.enabled) {
        // Skin input sources, in the order they are searched: the car's
        // pack, then --skin-vinyls, then each --shared-textures in order.
        // Every one of these packs was parsed once already; nothing is
        // parsed a second time here. The labels are fixed ASCII so that the
        // report does not depend on the file names.
        std::vector<nfsu::SkinInputSource> sources;
        const auto add_source = [&](const std::vector<unsigned char>& source_bytes,
                                    nfsu::SkinSourceKind kind, const std::string& label,
                                    const std::vector<const nfsu::TexturePack*>& packs) {
            nfsu::SkinInputSource source;
            source.bytes = &source_bytes;
            source.kind = kind;
            source.path = label;
            for (const nfsu::TexturePack* candidate : packs) {
                // A pack the assembly layer would refuse offers no records
                // instead, so one unreadable file cannot fail the rest.
                if (candidate != nullptr && candidate->tables_parsed &&
                    candidate->parse_source == source_bytes.data() &&
                    candidate->file_size == source_bytes.size()) {
                    source.packs.push_back(candidate);
                } else {
                    std::cerr << "skin source " << label
                              << ": a pack has no parsed record tables and offers no "
                                 "records\n";
                    skin_ok = false;
                }
            }
            // A source with no usable pack is left out rather than searched,
            // because the assembly layer refuses an empty one outright.
            if (source.packs.empty()) {
                std::cerr << "skin source " << label
                          << ": no usable pack, not searched\n";
                skin_ok = false;
                return;
            }
            skin_source_labels.push_back(label);
            sources.push_back(std::move(source));
        };

        add_source(bytes, nfsu::SkinSourceKind::car_textures, "car-textures", {&pack});
        if (skin.has_vinyls) {
            add_source(vinyl_bytes, nfsu::SkinSourceKind::car_vinyls, "car-vinyls",
                       {&vinyl_pack});
            skin_ok = skin_ok && vinyl_pack.ok();
        }
        for (std::size_t i = 0; i < shared_paths.size(); ++i) {
            std::vector<const nfsu::TexturePack*> packs;
            for (const nfsu::TexturePack& shared_pack : shared_sets[i].packs) {
                packs.push_back(&shared_pack);
            }
            add_source(shared_bytes[i], nfsu::SkinSourceKind::other,
                       "shared-" + std::to_string(i + 1), packs);
        }

        nfsu::BodySkinSelection selected = skin.selection;
        selected.target_hash = skin.target_hash ? *skin.target_hash
                                                : nfsu::bin_hash(skin.target_name);
        // width and height stay zero: they are taken from the target record.
        const nfsu::SkinInputLimits skin_limits;
        std::string skin_error;
        nfsu::BodySkinInputPlan plan;
        if (!nfsu::plan_body_skin_inputs(selected, sources, skin_limits, plan,
                                         skin_error)) {
            std::cerr << "error: skin inputs: " << skin_error << '\n';
            return 2;
        }
        // The compositor path comes from the real target record, never from
        // an option. The plan above only had to resolve that record; a
        // source whose format the path cannot take is a per-input skip, not
        // a plan failure, so one plan is enough to reach it. The assembly
        // below then checks every conversion against the chosen path.
        if (!nfsu::choose_skin_target_path(plan.target_texture, skin_choice)) {
            std::cerr << "error: skin target: " << skin_choice.reason << '\n';
            return 2;
        }
        selected.target = skin_choice.path;
        if (!nfsu::assemble_body_skin_inputs(selected, sources, skin_limits, skin_inputs,
                                             skin_error)) {
            std::cerr << "error: skin inputs: " << skin_error << '\n';
            return 2;
        }
        if (!nfsu::validate_assembled_skin_inputs(skin_inputs, skin_error)) {
            std::cerr << "error: skin inputs failed validation: " << skin_error << '\n';
            return 2;
        }
        nfsu::SkinTargetSurface target_surface;
        if (!nfsu::read_skin_target_surface(sources, skin_inputs.plan().target_texture,
                                            skin_limits, target_surface, skin_error)) {
            std::cerr << "error: skin target: " << skin_error << '\n';
            return 2;
        }
        nfsu::SkinMarkerCache markers;
        if (!nfsu::compose_runtime_skin(sources, skin_inputs, target_surface, markers,
                                        skin_texture, skin_error)) {
            std::cerr << "error: skin composite: " << skin_error << '\n';
            return 2;
        }

        // Which hashes the generated texture answers to. The target hash is
        // always one of them; <car>_SKIN1 is the hash the retail render
        // path replaces with RideInfo+0x444 at 0x00580C37, so it is bound
        // unless --skin-no-body-bind says otherwise.
        std::vector<std::uint32_t> bound = {skin_texture.target_hash};
        skin_bound_labels.push_back(skin.target_hash ? std::string() : skin.target_name);
        if (skin.body_bind) {
            const std::string body = skin.car + "_SKIN1";
            bound.push_back(nfsu::bin_hash(body));
            skin_bound_labels.push_back(body);
        }
        for (std::size_t i = 0; i < skin.extra_hashes.size(); ++i) {
            bound.push_back(skin.extra_hashes[i]);
            skin_bound_labels.push_back(skin.extra_names[i]);
        }
        if (!nfsu::bind_runtime_skin(skin_texture, bound, shared_paths.size() + 1, textures,
                                     skin_binding, skin_error)) {
            std::cerr << "error: skin binding: " << skin_error << '\n';
            return 2;
        }
        skin_ok = skin_ok && skin_inputs.plan().skipped_inputs == 0;

        if (skin.has_dump) {
            nfsu::FrameWriteResult dump;
            if (!nfsu::write_rgba_tga(skin_texture.width, skin_texture.height,
                                      skin_texture.rgba, skin.dump, inputs, dump)) {
                std::cerr << "error: " << dump.error << '\n';
                return 2;
            }
        }
    }

    // Material lookup happens here, once; the renderer reads the scene only.
    nfsu::PreparedCarScene scene;
    nfsu::StockSceneReport stock_report;
    std::string error;
    if (stock.enabled) {
        if (!nfsu::prepare_stock_car_scene(stock_sources, textures, stock_parts,
                                           stock_types, stock_request(stock),
                                           scene_limits, scene, stock_report, error)) {
            std::cerr << "error: stock car: " << error << '\n';
            return 2;
        }
    } else if (!nfsu::prepare_car_scene(geometry, textures, selection, scene_limits,
                                        scene, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    if (options.car_shading) {
        auto installed=stock.enabled ? stock_report.slots[30].record : nfsu::kNoCarPart;
        if (stock.enabled && installed==nfsu::kNoCarPart) {
            const auto name=stock_types.records[stock_report.car_type].base_paint_part_hash;
            installed=nfsu::find_car_part(stock_parts,nfsu::kAnyCarType,30,name,
                                           nfsu::kNoCarPart,nfsu::kAnyUpgradeLevel);
        }
        bool found=false;
        for (const auto& bytes:shared_bytes) {
            std::vector<nfsu::CarMaterialRecord> records;
            if (nfsu::parse_car_materials(bytes.data(),bytes.size(),records,error) && !records.empty()) {
                const nfsu::CarPartDatabase* parts=&stock_parts;
                nfsu::CarPartDatabase skin_parts;
                if (!stock.enabled && skin.enabled) {
                    // The color-only skin still needs the GLOBALB swatches.
                    // Use the same shared source that supplied the materials.
                    skin_parts=nfsu::parse_car_part_database(bytes.data(),bytes.size());
                    if (!skin_parts.ok()) {
                        std::cerr << "error: car shading needs GLOBALB.BUN part database\n";
                        return 2;
                    }
                    parts=&skin_parts;
                }
                const auto paint=nfsu::select_car_paint_material_hash(*parts,
                    installed,
                    skin.enabled ? std::optional<std::uint32_t>(skin.selection.base_color)
                                 : std::nullopt);
                if (!nfsu::apply_car_materials(scene,bytes.data(),bytes.size(),paint,error)) {
                    std::cerr << "error: " << error << '\n'; return 2;
                }
                found=true; break;
            }
        }
        if (!found) { std::cerr << "error: car shading needs GLOBALB.BUN material records\n"; return 2; }
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
            sources_ok = sources_ok && set.ok();
            std::cout << "texture source " << (i + 1) << ": bytes " << set.file_size
                      << " packs " << set.packs.size() << " records " << records
                      << " valid " << valid << " decoded " << shared_decoded[i]
                      << " decoded_bytes " << shared_decoded_bytes[i] << " issues "
                      << issues << " complete " << (set.complete ? 1 : 0) << '\n';
        }
        std::cout << "texture sources: decoded_bytes " << decode_total_bytes
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
    if (skin.enabled) {
        const nfsu::BodySkinInputPlan& plan = skin_inputs.plan();
        for (std::size_t i = 0; i < skin_source_labels.size(); ++i) {
            std::cout << "skin source " << i << ": " << skin_source_labels[i] << '\n';
        }
        std::cout << "skin: car " << ascii_quoted(skin.car) << " path "
                  << nfsu::skin_target_type_name(skin_texture.path) << ' '
                  << skin_texture.width << 'x' << skin_texture.height << " layers "
                  << skin_texture.layers << " owned_bytes " << skin_inputs.owned_bytes()
                  << " skipped_inputs " << plan.skipped_inputs << '\n';
        std::cout << "skin target: hash " << hex32(skin_texture.target_hash) << ' '
                  << skin_provenance(plan.target_texture) << " object_type_byte "
                  << hex8(skin_choice.object_type_byte) << " expanded "
                  << (skin_choice.expanded_on_upload ? 1 : 0) << '\n';
        std::cout << "skin paint: base " << hex32(plan.base_color) << " special";
        for (std::size_t i = 0; i < plan.special_colors.size(); ++i) {
            std::cout << (i == 0 ? ' ' : ',') << hex32(plan.special_colors[i]);
        }
        std::cout << " defaulted";
        for (std::size_t i = 0; i < plan.special_color_defaulted.size(); ++i) {
            std::cout << (i == 0 ? ' ' : ',') << (plan.special_color_defaulted[i] ? 1 : 0);
        }
        std::cout << '\n';
        for (const nfsu::SkinInputLayerInfo& input : plan.inputs) {
            std::cout << "skin input " << nfsu::skin_source_role_name(input.role)
                      << ": source " << ascii_quoted(input.source_name) << ' '
                      << hex32(input.source_hash) << " [" << skin_provenance(input.source)
                      << "] mask " << ascii_quoted(input.mask_name) << ' '
                      << hex32(input.mask_hash) << " [" << skin_provenance(input.mask)
                      << "] palette " << input.palette_entries << " remap "
                      << (input.remap ? 1 : 0);
            if (input.usable) {
                std::cout << " layer " << input.layer_index;
                if (input.assembly) {
                    std::cout << " expanded "
                              << (input.assembly->source_expanded ? 1 : 0) << ','
                              << (input.assembly->mask_expanded ? 1 : 0) << " owned_bytes "
                              << input.assembly->owned_bytes;
                }
            } else {
                std::cout << " skipped "
                          << nfsu::skin_skip_reason_name(input.skip_reason);
                if (!input.skip_detail.empty()) {
                    std::cout << " (" << input.skip_detail << ')';
                }
            }
            std::cout << '\n';
        }
        for (std::size_t i = 0; i < skin_binding.hashes.size(); ++i) {
            std::cout << "skin bound: hash " << hex32(skin_binding.hashes[i]);
            if (i < skin_bound_labels.size() && !skin_bound_labels[i].empty()) {
                std::cout << ' ' << ascii_quoted(skin_bound_labels[i]);
            }
            std::cout << '\n';
        }
        for (const nfsu::DisplacedTexture& displaced : skin_binding.displaced) {
            std::cout << "skin displaced: hash " << hex32(displaced.hash) << " source "
                      << displaced.source << " pack " << displaced.pack << " texture "
                      << displaced.record << " usable " << (displaced.was_usable ? 1 : 0)
                      << '\n';
        }
        for (const nfsu::UnusableClaim& claim : skin_binding.displaced_claims) {
            std::cout << "skin displaced claim: hash " << hex32(claim.hash) << " source "
                      << claim.source << " pack " << claim.pack << " texture "
                      << claim.record << '\n';
        }
        // A displaced record may have claimed hashes this run never bound.
        // Those are kept as unusable claims, so they are reported too.
        for (const nfsu::UnusableClaim& claim : skin_binding.preserved_claims) {
            std::cout << "skin preserved claim: hash " << hex32(claim.hash) << " source "
                      << claim.source << " pack " << claim.pack << " texture "
                      << claim.record << '\n';
        }
        std::cout << "skin generated: texture " << skin_binding.texture_index << " source "
                  << skin_binding.source << " bytes " << skin_binding.bytes << '\n';
        if (skin.has_dump) {
            std::cout << "skin dump: " << skin.dump.filename().u8string() << '\n';
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
    // Printed only for the stock car, so every other report is byte for
    // byte what it was before this profile existed.
    if (stock.enabled) {
        for (std::size_t i = 0; i < stock_sources.size(); ++i) {
            std::cout << "stock geometry " << i << ": meshes "
                      << stock_sources[i]->meshes.size() << " valid "
                      << stock_sources[i]->valid_mesh_count() << " directory "
                      << (stock_sources[i]->mesh_directory_valid ? "verified"
                                                                 : "unavailable")
                      << '\n';
        }
        std::cout << "stock: car " << ascii_quoted(stock_report.car_type_name)
                  << " hash " << hex32(stock_report.car_type_hash) << " record "
                  << stock_report.car_type << " lod "
                  << static_cast<char>('A' + stock_report.lod) << " pass "
                  << nfsu::car_draw_pass_name(stock_report.pass) << '\n';
        for (const nfsu::StockSlotReport& slot : stock_report.slots) {
            if (!slot.overridden && slot.record == nfsu::kNoCarPart) {
                continue;
            }
            std::cout << "stock slot " << slot.slot << ' '
                      << nfsu::car_slot_name(slot.slot) << ' ';
            if (slot.overridden) {
                std::cout << "overridden ";
            }
            std::cout << (slot.enabled ? "enabled" : "disabled") << " record "
                      << (slot.record == nfsu::kNoCarPart ? "none" :
                          std::to_string(slot.record)) << ' '
                      << nfsu::stock_slot_outcome_name(slot.outcome);
            if (slot.outcome == nfsu::StockSlotOutcome::drawn) {
                std::cout << " lod " << static_cast<char>('A' + slot.lod)
                          << " geometry " << slot.geometry_source << " mesh "
                          << slot.source_mesh << ' '
                          << ascii_quoted(stock_sources[slot.geometry_source]
                                              ->meshes[slot.source_mesh]
                                              .name)
                          << ' ' << nfsu::stock_placement_name(slot.placement)
                          << " instances " << slot.instance_count;
            }
            std::cout << '\n';
        }
        std::cout << "stock slots: installed " << stock_report.installed
                  << " with_model " << stock_report.with_model << " drawn "
                  << stock_report.drawn << " by_fallback " << stock_report.fell_back
                  << " markers " << stock_report.markers.size();
        if (stock_report.overridden != 0) {
            std::cout << " overridden " << stock_report.overridden;
        }
        std::cout << '\n';
        // The stock lateral branch is approximate and a static frame has
        // no spin, steering or suspension travel.
        std::cout << "stock wheels: "
                  << (stock.place_wheels
                          ? "drawn with stock lateral scale and offset; "
                            "dynamic angles absent, alternate branches approximate"
                          : "not drawn; the drawn wheel and brake matrices are not "
                            "reproduced")
                  << '\n';
        if (stock_report.degenerate_wheel_plane) {
            std::cout << "stock wheel plane: degenerate\n";
        }
        for (const std::uint32_t hash : stock_report.absent_hashes) {
            std::cout << "stock absent hash: " << hex32(hash) << '\n';
        }
        for (const std::uint32_t hash : stock_report.ambiguous_hashes) {
            std::cout << "stock ambiguous hash: " << hex32(hash) << '\n';
        }
    }
    // The all-meshes report is unchanged; only an explicit subset is listed.
    if (scene.profile == nfsu::SelectionProfile::explicit_subset) {
        std::cout << "selection: " << nfsu::selection_profile_name(scene.profile)
                  << ", " << scene.meshes.size() << " of "
                  << scene.source_mesh_counts.front() << " meshes, "
                  << scene.selectors.size() << " selectors\n";
        for (const nfsu::PreparedMesh& mesh : scene.meshes) {
            const nfsu::MeshSelector& selector = scene.selectors[mesh.selector];
            std::cout << "selected: mesh " << mesh.source_index << " name "
                      << ascii_quoted(mesh.name) << " by selector " << mesh.selector << " ("
                      << nfsu::mesh_selector_kind_name(selector.kind) << ")\n";
        }
    }
    print_stats(stats);
    // Drawn copies, next to the mesh count print_stats reports, so that 30
    // instances of 24 meshes cannot be read as 30 meshes. A profile with no
    // instances draws each mesh once, so the two counts are equal there and
    // the line is left out to keep those reports unchanged.
    if (scene.profile == nfsu::SelectionProfile::stock_car) {
        std::cout << "instances: " << stats.instances << " exact "
                  << stats.instances_exact << " stand_in " << stats.instances_stand_in
                  << '\n';
    }
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
    // Everything above is what a headless run does, in the order it has
    // always done it: the screenshot is on disk and the whole report is on
    // stdout before the window opens. Without --interactive nothing below
    // runs, so the TGA, stdout, stderr and the exit code of every other
    // invocation are byte for byte what they were.
    if (interactive) {
        std::cout << "interactive: backend "
                  << nfsu::inspection_window_backend_name() << '\n';
        // The window renders the same prepared scene with the same options
        // the screenshot was rendered with; it reads no file and writes
        // none, so the inputs stay untouched while it is open.
        nfsu::Inspector inspector;
        std::string inspect_error;
        if (!inspector.reset(scene, options, limits, inspect_error)) {
            std::cerr << "error: inspector: " << inspect_error << '\n';
            return 2;
        }
        nfsu::InspectionWindowOptions window;
        window.title = "nfsu car inspector";
        if (!nfsu::run_inspection_window(inspector, window, inspect_error)) {
            std::cerr << "error: inspection window: " << inspect_error << '\n';
            return 2;
        }
    }
    bool stock_ok = true;
    if (stock.enabled) {
        stock_ok = stock_parts.ok() && stock_types.ok();
        for (const nfsu::GeometryReport& source : stock_geometry) {
            stock_ok = stock_ok && source.ok();
        }
    }
    return geometry.ok() && pack.ok() && sources_ok && skin_ok && stock_ok ? 0 : 3;
}
