#include "nfsu/skin_inputs.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;
long g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

std::string hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

void check_u32(std::uint32_t got, std::uint32_t want, const std::string& message) {
    ++g_checks;
    if (got != want) {
        std::cerr << "test failure: " << message << ": got " << hex(got) << " want " << hex(want)
                  << '\n';
        ++g_failures;
    }
}

void check_text(const std::string& got, const std::string& want, const std::string& message) {
    ++g_checks;
    if (got != want) {
        std::cerr << "test failure: " << message << ": got \"" << got << "\" want \"" << want
                  << "\"\n";
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// Synthetic pack fixtures
//
// Only what these tests need: one JDLZ pack root per image, literal-only
// blobs, and the two formats the assembly layer accepts plus one it must
// refuse. The layout follows docs/FORMATS.md and matches the builder in
// tests/texture_tests.cpp.
// ---------------------------------------------------------------------------

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8U * i)) & 0xFFU));
    }
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

Bytes jdlz_header(std::uint32_t decompressed, std::uint32_t compressed) {
    Bytes bytes = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};
    append_u32(bytes, decompressed);
    append_u32(bytes, compressed);
    return bytes;
}

// Literal-only JDLZ stream: a flags2 byte first, then a zero flags1 byte
// before every group of eight literals.
Bytes jdlz_literals(const Bytes& data) {
    Bytes body;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i % 8 == 0) {
            body.push_back(0x00);
            if (i == 0) {
                body.push_back(0x00);
            }
        }
        body.push_back(data[i]);
    }
    Bytes blob = jdlz_header(static_cast<std::uint32_t>(data.size()),
                             static_cast<std::uint32_t>(body.size() + 16));
    blob.insert(blob.end(), body.begin(), body.end());
    return blob;
}

class Writer {
public:
    std::size_t size() const { return bytes_.size(); }
    Bytes& bytes() { return bytes_; }

    void begin(std::uint32_t id) {
        open_.push_back(bytes_.size());
        append_u32(bytes_, id);
        append_u32(bytes_, 0);
    }

    void end() {
        const std::size_t header = open_.back();
        open_.pop_back();
        put_u32(bytes_, header + 4, static_cast<std::uint32_t>(bytes_.size() - header - 8));
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append(payload);
        end();
    }

    void append(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

private:
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

struct TexSpec {
    std::string name;
    std::optional<std::uint32_t> hash;
    std::uint8_t format_code = 0x20;
    std::uint32_t d3d_format = 21;
    std::uint16_t width = 4;
    std::uint16_t height = 4;
    // The decompressed mip chain, one level only.
    Bytes pixels;
    // Appended after align_up(pixels, 128) for P8 records.
    std::optional<Bytes> palette;
    std::optional<Bytes> blob;
    std::optional<std::uint32_t> top_size;
};

unsigned char log2_of(std::uint32_t value) {
    unsigned char result = 0;
    while ((1U << result) < value) {
        ++result;
    }
    return result;
}

bool is_p8_code(std::uint8_t code) {
    return code == 0x08 || code == 0x80 || code == 0x81;
}

Bytes build_pack(const std::vector<TexSpec>& specs) {
    Writer w;
    w.begin(nfsu::kTexturePackRootId);
    w.begin(nfsu::kTextureInfoContainerId);

    Bytes header(124, 0);
    put_u32(header, 0, 4);
    std::memcpy(&header[4], "CARTEXTURES", 11);
    const std::string path = "Test\\Skin.tpk";
    std::memcpy(&header[32], path.data(), path.size());
    put_u32(header, 96, nfsu::bin_hash(path));
    w.leaf(nfsu::kTexturePackHeaderId, header);

    std::vector<std::uint32_t> hashes;
    for (const TexSpec& spec : specs) {
        hashes.push_back(spec.hash.value_or(nfsu::bin_hash(spec.name)));
    }

    Bytes hash_table;
    for (const std::uint32_t hash : hashes) {
        append_u32(hash_table, hash);
        append_u32(hash_table, 0);
    }
    w.leaf(nfsu::kTextureHashTableId, hash_table);

    w.begin(nfsu::kTextureBlobTableId);
    std::vector<std::size_t> blob_entry_offsets;
    for (const std::uint32_t hash : hashes) {
        blob_entry_offsets.push_back(w.size());
        Bytes entry;
        append_u32(entry, hash);
        append_u32(entry, 0);
        append_u32(entry, 0);
        append_u32(entry, 0x100);
        append_u32(entry, 0);
        w.append(entry);
    }
    w.end();

    Bytes info_table;
    std::uint32_t virtual_offset = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const TexSpec& spec = specs[i];
        Bytes record(124, 0);
        std::memcpy(&record[12], spec.name.data(),
                    std::min<std::size_t>(spec.name.size(), nfsu::kTextureNameSize));
        put_u32(record, 36, hashes[i]);
        put_u32(record, 40, 0x001A93CFU);
        const std::uint32_t size = static_cast<std::uint32_t>(spec.pixels.size());
        const bool p8 = is_p8_code(spec.format_code);
        put_u32(record, 48, virtual_offset);
        virtual_offset = (virtual_offset + size + 127U) & ~127U;
        put_u32(record, 52, virtual_offset);
        put_u32(record, 56, size);
        put_u32(record, 60, p8 ? static_cast<std::uint32_t>(nfsu::kP8PaletteSize) : 0U);
        if (p8) {
            virtual_offset += static_cast<std::uint32_t>(nfsu::kP8PaletteSize);
        }
        put_u32(record, 64, spec.top_size.value_or(size));
        put_u16(record, 68, spec.width);
        put_u16(record, 70, spec.height);
        record[72] = log2_of(spec.width);
        record[73] = log2_of(spec.height);
        record[74] = spec.format_code;
        record[78] = 1;
        put_u32(record, 84, 0x01000000U);
        info_table.insert(info_table.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureInfoTableId, info_table);

    Bytes format_table;
    for (const TexSpec& spec : specs) {
        Bytes record(32, 0);
        put_u32(record, 12, 5);
        put_u32(record, 16, 6);
        put_u32(record, 20, spec.d3d_format);
        format_table.insert(format_table.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureFormatTableId, format_table);
    w.end();

    w.begin(nfsu::kTextureDataContainerId);
    Bytes data_header(24, 0);
    put_u32(data_header, 8, 1);
    put_u32(data_header, 12, nfsu::bin_hash(path));
    w.leaf(nfsu::kTextureDataHeaderId, data_header);
    w.begin(nfsu::kTextureDataId);
    while (w.size() % 128 != 0) {
        w.bytes().push_back(0x11);
    }
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const TexSpec& spec = specs[i];
        Bytes stored = spec.pixels;
        if (spec.palette) {
            stored.resize((stored.size() + 127U) & ~static_cast<std::size_t>(127U), 0);
            stored.insert(stored.end(), spec.palette->begin(), spec.palette->end());
        }
        const Bytes blob = spec.blob.value_or(jdlz_literals(stored));
        put_u32(w.bytes(), blob_entry_offsets[i] + 4, static_cast<std::uint32_t>(w.size()));
        put_u32(w.bytes(), blob_entry_offsets[i] + 8, static_cast<std::uint32_t>(blob.size()));
        w.append(blob);
    }
    w.end();
    w.end();
    w.end();
    w.leaf(0x00000000U, Bytes(16, 0));
    return w.bytes();
}

// A8R8G8B8 level 0 whose pixel i is the little-endian dword seed + i.
TexSpec argb_spec(const std::string& name, std::uint32_t seed, std::uint16_t width = 4,
                  std::uint16_t height = 4) {
    TexSpec spec;
    spec.name = name;
    spec.format_code = 0x20;
    spec.d3d_format = 21;
    spec.width = width;
    spec.height = height;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < pixels; ++i) {
        append_u32(spec.pixels, seed + static_cast<std::uint32_t>(i));
    }
    return spec;
}

// P8 level 0 whose index i is `indices[i]`, with a palette whose entry e is
// the little-endian dword palette_seed + e.
TexSpec p8_spec(const std::string& name, std::uint8_t code, const Bytes& indices,
                std::uint32_t palette_seed, std::uint16_t width = 4,
                std::uint16_t height = 4) {
    TexSpec spec;
    spec.name = name;
    spec.format_code = code;
    spec.d3d_format = 41;
    spec.width = width;
    spec.height = height;
    spec.pixels = indices;
    Bytes palette;
    for (std::uint32_t entry = 0; entry < 256; ++entry) {
        append_u32(palette, palette_seed + entry);
    }
    spec.palette = palette;
    return spec;
}

Bytes ramp_indices(std::size_t count, unsigned char modulus) {
    Bytes out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<unsigned char>(i % modulus));
    }
    return out;
}

// One parsed image, kept alive for as long as the sources that name it.
struct Source {
    Bytes bytes;
    nfsu::TexturePack pack;
};

std::unique_ptr<Source> make_source(const std::vector<TexSpec>& specs) {
    auto source = std::unique_ptr<Source>(new Source());
    source->bytes = build_pack(specs);
    source->pack = nfsu::parse_texture_pack(source->bytes.data(), source->bytes.size());
    return source;
}

nfsu::SkinInputSource source_of(const Source& source, nfsu::SkinSourceKind kind,
                                const std::string& path) {
    nfsu::SkinInputSource out;
    out.bytes = &source.bytes;
    out.packs.push_back(&source.pack);
    out.kind = kind;
    out.path = path;
    return out;
}

// ---------------------------------------------------------------------------
// Selections
// ---------------------------------------------------------------------------

nfsu::BodySkinSelection base_selection(nfsu::SkinTargetType target) {
    nfsu::BodySkinSelection selection;
    selection.car_name = "240SX";
    selection.target = target;
    selection.width = 4;
    selection.height = 4;
    selection.base_color = nfsu::skin_pack_paint(0x11, 0x22, 0x33, 0x44);
    return selection;
}

nfsu::SkinVinylLayerSelection vinyl(const std::string& texture, std::uint32_t entries) {
    nfsu::SkinVinylLayerSelection layer;
    layer.texture = texture;
    layer.palette_size = entries;
    return layer;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// The multiply-by-33 continuation the gatherer performs at 0x005AA009:
// it never builds the "_MASK" string, it extends the base hash.
std::uint32_t continue_hash(std::uint32_t hash, const std::string& text) {
    for (const char c : text) {
        hash = hash * 33U + static_cast<unsigned char>(c);
    }
    return hash;
}

void test_names_and_hashes() {
    check_text(nfsu::skin_vinyl_source_name("240SX", "TRIBAL5"), "240SX_TRIBAL5",
               "vinyl source name");
    check_text(nfsu::skin_doorline_source_name("240SX", nfsu::SkinDoorlineVariant::plain),
               "240SX_DOORLINE", "plain doorline name");
    check_text(nfsu::skin_doorline_source_name("240SX", nfsu::SkinDoorlineVariant::body_kit),
               "240SX_DOORLINE_KIT", "body-kit doorline name");
    check_text(nfsu::skin_doorline_source_name("240SX", nfsu::SkinDoorlineVariant::wide_body),
               "240SX_DOORLINE_WIDEBODY", "wide-body doorline name");
    check_text(
        nfsu::skin_hood_spoiler_source_name("240SX", nfsu::SkinHoodSpoilerVariant::hood_and_spoiler),
        "240SX_HOOD_SPOILER", "hood and spoiler name");
    check_text(nfsu::skin_hood_spoiler_source_name("240SX", nfsu::SkinHoodSpoilerVariant::hood),
               "240SX_HOOD", "hood name");
    check_text(nfsu::skin_hood_spoiler_source_name("240SX", nfsu::SkinHoodSpoilerVariant::spoiler),
               "240SX_SPOILER", "spoiler name");
    check_text(nfsu::skin_mask_name("240SX_TRIBAL5"), "240SX_TRIBAL5_MASK", "mask name");

    // 0x005AA009..0x005AA024 continues the base hash instead of hashing the
    // longer string. The two agree because the hash has no length term, and
    // the retail build depends on that agreement.
    const char* names[] = {"240SX_TRIBAL5", "240SX_DOORLINE", "240SX_DOORLINE_WIDEBODY",
                           "240SX_HOOD_SPOILER", "", "A"};
    for (const char* name : names) {
        const std::uint32_t base = nfsu::bin_hash(name);
        check_u32(nfsu::bin_hash(nfsu::skin_mask_name(name)),
                  continue_hash(base, nfsu::kSkinMaskSuffix),
                  std::string("mask hash continues the base hash for \"") + name + "\"");
    }

    // Hashes read out of the retail 240SX packs (docs/CAR_SCENE.md and
    // docs/FORMATS.md), so the name builders are pinned to real records.
    check_u32(nfsu::bin_hash("240SX_TRIBAL5"), 0x4573BA32U, "retail vinyl hash");
    check_u32(nfsu::bin_hash("240SX_TRIBAL5_MASK"), 0x005C59FDU, "retail vinyl mask hash");
    check_u32(nfsu::bin_hash("240SX_DOORLINE"), 0x580576DBU, "retail doorline hash");
    check_u32(nfsu::bin_hash("240SX_DOORLINE_MASK"), 0x699B6846U, "retail doorline mask hash");
    check_u32(nfsu::bin_hash("240SX_DOORLINE_KIT"), 0x3980CD22U, "retail kit doorline hash");
    check_u32(nfsu::bin_hash("240SX_DOORLINE_KIT_MASK"), 0xB13AC2EDU, "retail kit mask hash");
    check_u32(nfsu::bin_hash("240SX_DOORLINE_WIDEBODY"), 0x9D21E7D1U, "retail widebody hash");
    check_u32(nfsu::bin_hash("240SX_DOORLINE_WIDEBODY_MASK"), 0xBFD2E2FCU,
              "retail widebody mask hash");
    check_u32(nfsu::bin_hash("DUMMY_SKIN1"), 0x530B82B0U, "retail target hash");

    // 0x005AA134..0x005AA19F packs RED, GREEN, BLUE, GLOSS into lanes 0..3.
    check_u32(nfsu::skin_pack_paint(0x01, 0x02, 0x03, 0x04), 0x04030201U, "paint lane order");
    for (std::size_t k = 0; k < 4; ++k) {
        check_u32(nfsu::skin_default_remap_color(k), 0xFFU << (8U * k),
                  "default remap color " + std::to_string(k));
    }
    check(nfsu::kSkinFixedPaletteSize == 16, "the fixed NUMCOLOURS is 0x10");
}

void test_type32_assembly() {
    // A vinyl pair in one source and a doorline pair in another, exactly as
    // retail stores them: paletted vinyls, 32-bit doorlines.
    auto vinyls = make_source({
        p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 8), 0x01000000U),
        p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 4), 0x02000000U),
    });
    auto textures = make_source({
        argb_spec("240SX_DOORLINE", 0x10000000U),
        argb_spec("240SX_DOORLINE_MASK", 0x20000000U),
    });
    check(vinyls->pack.ok() && textures->pack.ok(), "fixture packs parse cleanly");

    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
    };

    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.vinyl_layers[0] = vinyl("TRIBAL5", 8);
    selection.doorline = nfsu::SkinDoorlineSelection{};
    selection.special_colors[1] = 0xAABBCCDDU;

    nfsu::AssembledSkinInputs assembled;
    std::string error;
    check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "type-32 assembly succeeds: " + error);
    check(assembled.layer_count() == 2, "two layers assembled");
    check(assembled.plan().usable_inputs == 2 && assembled.plan().skipped_inputs == 0,
          "both inputs are usable");
    check(assembled.plan().inputs.size() == 2, "two inputs were selected");
    check(assembled.plan().inputs[0].role == nfsu::SkinSourceRole::vinyl_layer0,
          "the vinyl layer comes first");
    check(assembled.plan().inputs[1].role == nfsu::SkinSourceRole::doorline,
          "the doorline comes last");
    // Four surfaces, each 16 pixels of 4 bytes plus the palette every
    // SkinLayerStorage carries inline whatever the target type is.
    check(assembled.owned_bytes() == 4U * (16U * 4U + nfsu::kP8PaletteSize),
          "owned bytes cover four surfaces and their inline palettes");

    // The special color the selection left empty falls back to the base.
    check_u32(assembled.request().special_colors[0], selection.base_color,
              "empty special slot falls back to the base color");
    check_u32(assembled.request().special_colors[1], 0xAABBCCDDU, "explicit special color");
    check(assembled.plan().special_color_defaulted[0], "slot 0 is reported as defaulted");
    check(!assembled.plan().special_color_defaulted[1], "slot 1 is reported as explicit");

    const nfsu::BodySkinLayer& vinyl_layer = assembled.request().layers[0];
    check(vinyl_layer.present, "the vinyl layer is present");
    check(vinyl_layer.source32.data != nullptr && vinyl_layer.source32.size == 16,
          "the vinyl source is a 32-bit view of the target size");
    check(vinyl_layer.source8.data == nullptr && vinyl_layer.source_palette == nullptr,
          "a type-32 layer leaves the 8-bit fields unset");
    check_u32(vinyl_layer.palette_size, 8, "NUMCOLOURS reaches record +0x1C");

    // 0x00407E70..0x00407E7B expands an index into the palette entry whole.
    for (std::size_t i = 0; i < 16; ++i) {
        check_u32(vinyl_layer.source32.data[i], 0x01000000U + static_cast<std::uint32_t>(i % 8),
                  "expanded vinyl pixel " + std::to_string(i));
        check_u32(vinyl_layer.mask32.data[i], 0x02000000U + static_cast<std::uint32_t>(i % 4),
                  "expanded mask pixel " + std::to_string(i));
    }
    check(assembled.plan().inputs[0].assembly.has_value(), "the vinyl input reports assembly");
    check(assembled.plan().inputs[0].assembly->source_expanded &&
              assembled.plan().inputs[0].assembly->mask_expanded,
          "both paletted vinyl surfaces were expanded");
    check_u32(assembled.plan().inputs[0].assembly->source_indices_used, 8,
              "the vinyl source uses eight palette entries");
    check(!assembled.plan().inputs[0].assembly->source_indices_exceed_palette,
          "eight entries fit NUMCOLOURS 8");

    // A8R8G8B8 dwords are used unchanged.
    const nfsu::BodySkinLayer& doorline = assembled.request().layers[1];
    for (std::size_t i = 0; i < 16; ++i) {
        check_u32(doorline.source32.data[i], 0x10000000U + static_cast<std::uint32_t>(i),
                  "doorline pixel " + std::to_string(i));
    }
    check(!assembled.plan().inputs[1].assembly->source_expanded,
          "a 32-bit doorline is not expanded");
    check_u32(assembled.plan().inputs[1].palette_entries, nfsu::kSkinFixedPaletteSize,
              "the doorline carries the fixed NUMCOLOURS");
    check(assembled.plan().inputs[1].palette_entries_defaulted,
          "the doorline NUMCOLOURS is reported as fixed");

    check(nfsu::validate_assembled_skin_inputs(assembled, error), "ownership validates: " + error);

    // The request drives the real compositor.
    nfsu::BodySkin32 out;
    nfsu::SkinMarkerCache markers;
    const std::vector<std::uint32_t> target(16, nfsu::kSkinMarkerSurface[0]);
    check(nfsu::composite_body_skin_32(assembled.request(), target, markers, out, error),
          "the assembled request composites: " + error);
    check(out.pixels.size() == 16, "the composite covers the target");
}

void test_type8_assembly() {
    auto vinyls = make_source({
        p8_spec("240SX_FLAME10", 0x80, ramp_indices(16, 4), 0x03000000U),
        p8_spec("240SX_FLAME10_MASK", 0x08, Bytes(16, 2), 0x04000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
    };
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type8);
    selection.vinyl_layers[2] = vinyl("FLAME10", 4);

    nfsu::AssembledSkinInputs assembled;
    std::string error;
    check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "type-8 assembly succeeds: " + error);
    check(assembled.layer_count() == 1, "one layer assembled");
    const nfsu::BodySkinLayer& layer = assembled.request().layers[0];
    check(layer.source8.data != nullptr && layer.source8.size == 16, "the source is an 8-bit view");
    check(layer.mask8.data != nullptr && layer.mask8.size == 16, "the mask is an 8-bit view");
    check(layer.source32.data == nullptr && layer.mask32.data == nullptr,
          "a type-8 layer leaves the 32-bit fields unset");
    check(layer.source_palette != nullptr && layer.mask_palette != nullptr,
          "a type-8 layer carries both palettes");
    // Index bytes pass through and the palette is a little-endian load.
    for (std::size_t i = 0; i < 16; ++i) {
        check(layer.source8.data[i] == static_cast<std::uint8_t>(i % 4),
              "index byte " + std::to_string(i) + " passes through");
    }
    for (std::uint32_t entry = 0; entry < 256; ++entry) {
        check_u32((*layer.source_palette)[entry], 0x03000000U + entry,
                  "palette entry " + std::to_string(entry));
    }
    check(assembled.plan().merged_palette_entries == 9,
          "the merged palette is five reserved entries plus NUMCOLOURS");
    check(assembled.plan().merged_palette_fits, "nine entries fit");
    check(nfsu::validate_assembled_skin_inputs(assembled, error), "ownership validates: " + error);

    nfsu::BodySkin8 out;
    nfsu::SkinMarkerCache markers;
    const std::vector<std::uint8_t> target(16, 0);
    nfsu::SkinPalette target_palette{};
    check(nfsu::composite_body_skin_8(assembled.request(), target, target_palette, markers, out,
                                      error),
          "the assembled request composites: " + error);
    check(out.indices.size() == 16, "the composite covers the target");
}

void test_missing_pairs() {
    auto vinyls = make_source({
        p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 8), 0x01000000U),
        p8_spec("240SX_ONLYMASK_MASK", 0x08, ramp_indices(16, 4), 0x02000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
    };

    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.vinyl_layers[0] = vinyl("TRIBAL5", 8);   // source without a mask
    selection.vinyl_layers[1] = vinyl("ONLYMASK", 8);  // mask without a source
    selection.vinyl_layers[2] = vinyl("ABSENT", 8);    // neither

    nfsu::BodySkinInputPlan plan;
    std::string error;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "planning missing pairs still succeeds: " + error);
    check(plan.usable_inputs == 0 && plan.skipped_inputs == 3, "all three inputs are skipped");
    check(plan.inputs[0].skip_reason == nfsu::SkinSkipReason::mask_not_found,
          "a source without a mask is skipped for the mask");
    check(plan.inputs[0].source.resolved && !plan.inputs[0].mask.resolved,
          "the source still reports its provenance");
    check(plan.inputs[1].skip_reason == nfsu::SkinSkipReason::source_not_found,
          "a mask without a source is skipped for the source");
    check(plan.inputs[2].skip_reason == nfsu::SkinSkipReason::source_not_found,
          "a name in no pack is skipped for the source");

    // The assembly agrees with the plan and produces no layers.
    nfsu::AssembledSkinInputs assembled;
    check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "assembling missing pairs still succeeds: " + error);
    check(assembled.layer_count() == 0, "nothing was assembled");
    for (std::size_t i = 0; i < 3; ++i) {
        check(assembled.plan().inputs[i].skip_reason == plan.inputs[i].skip_reason,
              "plan and assembly agree on input " + std::to_string(i));
    }
}

void test_dimension_mismatch() {
    auto textures = make_source({
        argb_spec("240SX_DOORLINE", 0x10000000U, 4, 4),
        argb_spec("240SX_DOORLINE_MASK", 0x20000000U, 2, 2),
        argb_spec("240SX_DOORLINE_KIT", 0x30000000U, 2, 4),
        argb_spec("240SX_DOORLINE_KIT_MASK", 0x40000000U, 4, 4),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };

    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.doorline = nfsu::SkinDoorlineSelection{};
    nfsu::BodySkinInputPlan plan;
    std::string error;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "planning a mismatched mask succeeds: " + error);
    check(plan.inputs[0].skip_reason == nfsu::SkinSkipReason::mask_dimension_mismatch,
          "a 2x2 mask under a 4x4 target is refused");

    selection.doorline->variant = nfsu::SkinDoorlineVariant::body_kit;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "planning a mismatched source succeeds: " + error);
    check(plan.inputs[0].skip_reason == nfsu::SkinSkipReason::source_dimension_mismatch,
          "a 2x4 source under a 4x4 target is refused");
    check(plan.inputs[0].source.width == 2 && plan.inputs[0].source.height == 4,
          "the refused source still reports its dimensions");
}

void test_format_refusals() {
    Bytes dxt;
    for (int i = 0; i < 8; ++i) {
        dxt.push_back(static_cast<unsigned char>(i));
    }
    TexSpec dxt_source;
    dxt_source.name = "240SX_DOORLINE";
    dxt_source.format_code = 0x22;
    dxt_source.d3d_format = 0x31545844U;
    dxt_source.width = 4;
    dxt_source.height = 4;
    dxt_source.pixels = dxt;

    auto textures = make_source({dxt_source, argb_spec("240SX_DOORLINE_MASK", 0x20000000U)});
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.doorline = nfsu::SkinDoorlineSelection{};

    nfsu::BodySkinInputPlan plan;
    std::string error;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "planning an unconvertible source succeeds: " + error);
    check(plan.inputs[0].skip_reason == nfsu::SkinSkipReason::source_format_unproven,
          "a DXT1 source has no proven conversion to a type-32 target");

    // A 32-bit surface is never reduced to indices for a type-8 target.
    auto argb = make_source(
        {argb_spec("240SX_DOORLINE", 0x10000000U), argb_spec("240SX_DOORLINE_MASK", 0x20000000U)});
    std::vector<nfsu::SkinInputSource> argb_sources = {
        source_of(*argb, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };
    nfsu::BodySkinSelection type8 = base_selection(nfsu::SkinTargetType::type8);
    type8.doorline = nfsu::SkinDoorlineSelection{};
    check(nfsu::plan_body_skin_inputs(type8, argb_sources, nfsu::SkinInputLimits{}, plan, error),
          "planning a 32-bit source for a type-8 target succeeds: " + error);
    check(plan.inputs[0].skip_reason == nfsu::SkinSkipReason::source_format_unproven,
          "no evidence reduces a 32-bit surface to indices");
}

Bytes p8_stored_image(const Bytes& indices) {
    Bytes stored = indices;
    stored.resize((stored.size() + 127U) & ~static_cast<std::size_t>(127U), 0);
    for (std::size_t i = 0; i < nfsu::kP8PaletteSize; ++i) {
        stored.push_back(0);
    }
    return stored;
}

void test_truncation() {
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.vinyl_layers[0] = vinyl("TRIBAL5", 8);
    std::string error;

    // A blob cut short of the length its own header declares. The parser
    // sees the inconsistency, so the record never becomes an input.
    {
        auto good = p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 8), 0x01000000U);
        auto broken = p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 4), 0x02000000U);
        Bytes blob = jdlz_literals(p8_stored_image(broken.pixels));
        blob.resize(blob.size() - 64);
        broken.blob = blob;

        auto vinyls = make_source({good, broken});
        check(!vinyls->pack.ok() && !vinyls->pack.textures[1].valid,
              "the parser rejects the truncated record");
        std::vector<nfsu::SkinInputSource> sources = {
            source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
        };
        nfsu::AssembledSkinInputs assembled;
        check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{},
                                              assembled, error),
              "a rejected record is a skip, not a failure: " + error);
        check(assembled.layer_count() == 0, "nothing was assembled");
        check(assembled.plan().inputs[0].skip_reason ==
                  nfsu::SkinSkipReason::mask_record_unusable,
              "the truncated mask is reported as unusable");
        check(!assembled.plan().inputs[0].mask.record_problem.empty(),
              "the parser's problem text is preserved");
    }

    // A blob whose sizes are all consistent but whose stream cannot be
    // decoded. The parser accepts the record, so the failure can only
    // surface when assembly reads it, and it aborts the whole assembly.
    {
        auto good = p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 8), 0x01000000U);
        auto broken = p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 4), 0x02000000U);
        Bytes blob = jdlz_literals(p8_stored_image(broken.pixels));
        // The first flags byte turns the first literal into a back
        // reference, which can only point before the output.
        blob[nfsu::kJdlzHeaderSize] = 0xFFU;
        broken.blob = blob;

        auto vinyls = make_source({good, broken});
        check(vinyls->pack.ok() && vinyls->pack.textures[1].valid,
              "the parser accepts the undecodable record");
        std::vector<nfsu::SkinInputSource> sources = {
            source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
        };
        nfsu::AssembledSkinInputs assembled;
        check(!nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{},
                                               assembled, error),
              "an undecodable blob fails the assembly");
        check(!error.empty(), "the failure names a reason");
        check(assembled.layer_count() == 0 && assembled.request().width == 0,
              "a failed assembly leaves the output empty");
    }

    // A pack image cut off before its record tables cannot be a source at
    // all: its hashes are unknown, so resolving against it would report
    // "not found" for a name the whole file might well contain.
    {
        Bytes cut = build_pack({argb_spec("240SX_DOORLINE", 0x10000000U)});
        cut.resize(cut.size() / 2);
        Source partial;
        partial.bytes = cut;
        partial.pack = nfsu::parse_texture_pack(partial.bytes.data(), partial.bytes.size());
        check(!partial.pack.tables_parsed, "the cut image has no record tables");
        std::vector<nfsu::SkinInputSource> bad = {
            source_of(partial, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
        };
        nfsu::BodySkinInputPlan plan;
        check(!nfsu::plan_body_skin_inputs(selection, bad, nfsu::SkinInputLimits{}, plan, error),
              "a source without record tables is refused outright");
        check(error.find("record tables") != std::string::npos,
              "the refusal says why: " + error);
        check(plan.inputs.empty(), "no input was resolved against it");
    }
}

void test_mixed_skipped_and_usable() {
    // Skipped inputs come before, between and after usable ones, so the
    // compaction from six selected sources to two layers is exercised.
    auto textures = make_source({
        argb_spec("240SX_DOORLINE", 0x10000000U),
        argb_spec("240SX_DOORLINE_MASK", 0x20000000U),
        argb_spec("240SX_GOOD", 0x30000000U),
        argb_spec("240SX_GOOD_MASK", 0x40000000U),
        argb_spec("240SX_LONELY", 0x50000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };

    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.hood_spoiler_vinyl = nfsu::SkinHoodSpoilerSelection{}; // input 0, absent
    selection.vinyl_layers[0] = vinyl("MISSING", 8);                 // input 1, absent
    selection.vinyl_layers[1] = vinyl("GOOD", 8);                    // input 2, usable
    selection.vinyl_layers[2] = vinyl("LONELY", 8);                  // input 3, no mask
    selection.vinyl_layers[3] = vinyl("ALSOMISSING", 8);             // input 4, absent
    selection.doorline = nfsu::SkinDoorlineSelection{};              // input 5, usable

    nfsu::AssembledSkinInputs assembled;
    std::string error;
    check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "a mixed selection assembles: " + error);
    check(assembled.plan().inputs.size() == 6, "six sources were selected");
    check(assembled.plan().usable_inputs == 2 && assembled.plan().skipped_inputs == 4,
          "two of six are usable");
    check(assembled.layer_count() == 2, "the layer list is compacted");
    check(assembled.plan().inputs[2].layer_index == 0 &&
              assembled.plan().inputs[5].layer_index == 1,
          "the usable inputs name their compacted positions");
    check(assembled.input_of_layer(0) == 2 && assembled.input_of_layer(1) == 5,
          "each layer names the input it came from");
    check(assembled.plan().inputs[3].skip_reason == nfsu::SkinSkipReason::mask_not_found,
          "the unpaired source is skipped for its mask");
    // The layers carry the pixels of their own records, not a neighbour's.
    check_u32(assembled.request().layers[0].source32.data[0], 0x30000000U,
              "layer 0 holds the GOOD pixels");
    check_u32(assembled.request().layers[1].source32.data[0], 0x10000000U,
              "layer 1 holds the doorline pixels");
    check(assembled.layer_storage(0)->source32.data() ==
              assembled.request().layers[0].source32.data,
          "layer 0 points into its own storage");
    check(assembled.layer_storage(1)->source32.data() ==
              assembled.request().layers[1].source32.data,
          "layer 1 points into its own storage");
    check(nfsu::validate_assembled_skin_inputs(assembled, error), "ownership validates: " + error);

    // A plan predicts exactly the same compaction without reading pixels.
    nfsu::BodySkinInputPlan plan;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "the plan agrees: " + error);
    for (std::size_t i = 0; i < plan.inputs.size(); ++i) {
        check(plan.inputs[i].usable == assembled.plan().inputs[i].usable &&
                  plan.inputs[i].layer_index == assembled.plan().inputs[i].layer_index &&
                  plan.inputs[i].skip_reason == assembled.plan().inputs[i].skip_reason,
              "plan and assembly agree on input " + std::to_string(i));
        check(!plan.inputs[i].assembly.has_value(), "a plan reports no assembly detail");
    }
}

void test_failed_assembly_empties_output() {
    auto textures = make_source(
        {argb_spec("240SX_DOORLINE", 0x10000000U), argb_spec("240SX_DOORLINE_MASK", 0x20000000U)});
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };
    nfsu::BodySkinSelection good = base_selection(nfsu::SkinTargetType::type32);
    good.doorline = nfsu::SkinDoorlineSelection{};

    nfsu::AssembledSkinInputs assembled;
    std::string error;
    check(nfsu::assemble_body_skin_inputs(good, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "the first assembly succeeds: " + error);
    check(assembled.layer_count() == 1, "it produced a layer");

    // Reusing the same object for a call that fails must not leave the old
    // request, or a half-built one, behind.
    nfsu::BodySkinSelection bad = good;
    bad.car_name.clear();
    check(!nfsu::assemble_body_skin_inputs(bad, sources, nfsu::SkinInputLimits{}, assembled,
                                           error),
          "a malformed selection fails");
    check(assembled.empty() && assembled.layer_count() == 0,
          "the failure emptied the previously populated output");
    check(assembled.owned_bytes() == 0, "it owns nothing");
    check(assembled.request().width == 0 && assembled.request().height == 0,
          "it has no dimensions");
    check(assembled.plan().inputs.empty(), "it has no plan inputs");

    // The same holds for a failure that happens after resolution, while
    // copying pixels.
    check(nfsu::assemble_body_skin_inputs(good, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "the object is reusable after a failure: " + error);
    nfsu::SkinInputLimits tight;
    tight.max_total_bytes = 8;
    check(!nfsu::assemble_body_skin_inputs(good, sources, tight, assembled, error),
          "a budget failure fails");
    check(assembled.empty() && assembled.owned_bytes() == 0,
          "a mid-copy failure also empties the output");
}

void test_unusable_record_wins() {
    // The first source claims the hash with a record the parser rejected;
    // a later source holds a good one. The broken record still wins, so a
    // caller is told what is wrong instead of being handed a substitute.
    TexSpec broken = argb_spec("240SX_DOORLINE", 0x10000000U);
    broken.top_size = 1; // contradicts the level size
    auto first = make_source({broken, argb_spec("240SX_DOORLINE_MASK", 0x20000000U)});
    auto second = make_source(
        {argb_spec("240SX_DOORLINE", 0x30000000U), argb_spec("240SX_DOORLINE_MASK", 0x40000000U)});
    check(!first->pack.ok(), "the first pack reports its broken record");

    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*first, nfsu::SkinSourceKind::car_textures, "first.BIN"),
        source_of(*second, nfsu::SkinSourceKind::other, "second.BIN"),
    };
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.doorline = nfsu::SkinDoorlineSelection{};

    nfsu::BodySkinInputPlan plan;
    std::string error;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "planning over a broken first source succeeds: " + error);
    check(plan.inputs[0].skip_reason == nfsu::SkinSkipReason::source_record_unusable,
          "the unusable record is reported rather than replaced");
    check(plan.inputs[0].source.source_index == 0, "the first source still wins");
    check(!plan.inputs[0].source.record_problem.empty(), "the record problem is preserved");
    check(plan.inputs[0].source.also_claimed_by.size() == 1, "the later claimant is recorded");
    check(plan.inputs[0].source.also_claimed_by[0].source == 1,
          "the later claimant names its source");
    check(plan.inputs[0].mask.also_claimed_by.size() == 1, "the later mask claimant is recorded");
}

void test_priority_and_provenance() {
    auto first = make_source(
        {argb_spec("240SX_DOORLINE", 0x10000000U), argb_spec("240SX_DOORLINE_MASK", 0x20000000U)});
    auto second = make_source(
        {argb_spec("240SX_DOORLINE", 0x30000000U), argb_spec("240SX_DOORLINE_MASK", 0x40000000U)});
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.doorline = nfsu::SkinDoorlineSelection{};
    std::string error;

    {
        std::vector<nfsu::SkinInputSource> sources = {
            source_of(*first, nfsu::SkinSourceKind::car_textures, "first.BIN"),
            source_of(*second, nfsu::SkinSourceKind::other, "second.BIN"),
        };
        nfsu::AssembledSkinInputs assembled;
        check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{},
                                              assembled, error),
              "assembly with two claimants succeeds: " + error);
        check_u32(assembled.request().layers[0].source32.data[0], 0x10000000U,
                  "the first source supplies the pixels");
        check_text(assembled.plan().inputs[0].source.source_path, "first.BIN",
                   "the provenance names the winning source");
    }
    {
        // The same packs in the other order resolve to the other pixels,
        // which is the whole content of the ordering rule.
        std::vector<nfsu::SkinInputSource> sources = {
            source_of(*second, nfsu::SkinSourceKind::other, "second.BIN"),
            source_of(*first, nfsu::SkinSourceKind::car_textures, "first.BIN"),
        };
        nfsu::AssembledSkinInputs assembled;
        check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{},
                                              assembled, error),
              "assembly in the other order succeeds: " + error);
        check_u32(assembled.request().layers[0].source32.data[0], 0x30000000U,
                  "source order decides which record wins");
    }
}

void test_names_are_not_keys() {
    // Retail record 80 of the 240SX pack stores a name whose 24-byte field
    // truncated it, so two records carry identical text with different
    // hashes. Resolution is by hash, and the mismatch is reported.
    TexSpec source = argb_spec("240SX_DOORLINE_WIDEBODY", 0x10000000U);
    TexSpec mask = argb_spec("240SX_DOORLINE_WIDEBODY", 0x20000000U);
    mask.hash = nfsu::bin_hash("240SX_DOORLINE_WIDEBODY_MASK");
    auto textures = make_source({source, mask});

    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.doorline = nfsu::SkinDoorlineSelection{};
    selection.doorline->variant = nfsu::SkinDoorlineVariant::wide_body;

    nfsu::BodySkinInputPlan plan;
    std::string error;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "planning a truncated-name pair succeeds: " + error);
    check(plan.inputs[0].usable, "the pair resolves by hash");
    check(plan.inputs[0].source.stored_name_matches, "the source name hashes to its record");
    check(!plan.inputs[0].mask.stored_name_matches,
          "the truncated mask name does not hash to its record");
    check_text(plan.inputs[0].mask.stored_name, "240SX_DOORLINE_WIDEBODY",
               "the stored text is preserved unchanged");
    check_u32(plan.inputs[0].mask.record_hash, 0xBFD2E2FCU, "the raw record hash is preserved");
}

void test_palette_bounds() {
    auto vinyls = make_source({
        p8_spec("240SX_A", 0x81, ramp_indices(16, 16), 0x01000000U),
        p8_spec("240SX_A_MASK", 0x08, ramp_indices(16, 4), 0x02000000U),
        p8_spec("240SX_B", 0x81, ramp_indices(16, 8), 0x03000000U),
        p8_spec("240SX_B_MASK", 0x08, ramp_indices(16, 4), 0x04000000U),
        p8_spec("240SX_C", 0x81, ramp_indices(16, 8), 0x05000000U),
        p8_spec("240SX_C_MASK", 0x08, ramp_indices(16, 4), 0x06000000U),
        p8_spec("240SX_D", 0x81, ramp_indices(16, 8), 0x07000000U),
        p8_spec("240SX_D_MASK", 0x08, ramp_indices(16, 4), 0x08000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
    };
    std::string error;

    // Four CUSTOM-sized layers need 5 + 4 * 64 = 261 entries, which
    // composite_body_skin_8 refuses. A plan reports it; an assembly refuses
    // before copying a pixel.
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type8);
    selection.vinyl_layers[0] = vinyl("A", 64);
    selection.vinyl_layers[1] = vinyl("B", 64);
    selection.vinyl_layers[2] = vinyl("C", 64);
    selection.vinyl_layers[3] = vinyl("D", 64);

    nfsu::BodySkinInputPlan plan;
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "planning an overfull palette succeeds: " + error);
    check(plan.merged_palette_entries == 261, "the plan reports 261 merged entries");
    check(!plan.merged_palette_fits, "the plan reports that they do not fit");

    nfsu::AssembledSkinInputs assembled;
    check(!nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                           error),
          "assembling an overfull palette is refused");
    check(error.find("261") != std::string::npos, "the refusal names the sum: " + error);
    check(assembled.layer_count() == 0, "nothing was allocated");

    // Three fit exactly at 5 + 192.
    selection.vinyl_layers[3].reset();
    check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "three layers fit: " + error);
    check(assembled.plan().merged_palette_entries == 197, "three layers need 197 entries");

    // A selection may not declare more entries than a palette holds.
    nfsu::BodySkinSelection oversize = base_selection(nfsu::SkinTargetType::type8);
    oversize.vinyl_layers[0] = vinyl("A", 257);
    check(!nfsu::plan_body_skin_inputs(oversize, sources, nfsu::SkinInputLimits{}, plan, error),
          "more than 256 entries is a malformed selection");

    // Source data may address entries the layer does not declare. The game
    // does not check that, so it is reported rather than refused.
    nfsu::BodySkinSelection narrow = base_selection(nfsu::SkinTargetType::type8);
    narrow.vinyl_layers[0] = vinyl("A", 4);
    check(nfsu::assemble_body_skin_inputs(narrow, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "a narrow NUMCOLOURS still assembles: " + error);
    check_u32(assembled.plan().inputs[0].assembly->source_indices_used, 16,
              "the source uses sixteen entries");
    check(assembled.plan().inputs[0].assembly->source_indices_exceed_palette,
          "exceeding NUMCOLOURS is reported");
}

void test_numcolours_aborts() {
    auto vinyls = make_source({
        p8_spec("240SX_A", 0x80, ramp_indices(16, 8), 0x01000000U),
        p8_spec("240SX_A_MASK", 0x08, ramp_indices(16, 4), 0x02000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
    };
    std::string error;
    nfsu::BodySkinInputPlan plan;

    // 0x005A9F98 leaves through the function epilogue, so a vinyl layer
    // without a usable NUMCOLOURS abandons the whole composite.
    nfsu::BodySkinSelection absent = base_selection(nfsu::SkinTargetType::type32);
    absent.vinyl_layers[0] = nfsu::SkinVinylLayerSelection{};
    absent.vinyl_layers[0]->texture = "A";
    check(!nfsu::plan_body_skin_inputs(absent, sources, nfsu::SkinInputLimits{}, plan, error),
          "a vinyl layer without NUMCOLOURS aborts");
    check(error.find("NUMCOLOURS") != std::string::npos, "the error names NUMCOLOURS: " + error);

    nfsu::BodySkinSelection zero = base_selection(nfsu::SkinTargetType::type32);
    zero.vinyl_layers[0] = vinyl("A", 0);
    check(!nfsu::plan_body_skin_inputs(zero, sources, nfsu::SkinInputLimits{}, plan, error),
          "a zero NUMCOLOURS aborts the same way");

    nfsu::BodySkinSelection empty_name = base_selection(nfsu::SkinTargetType::type32);
    empty_name.vinyl_layers[0] = vinyl("", 8);
    check(!nfsu::plan_body_skin_inputs(empty_name, sources, nfsu::SkinInputLimits{}, plan, error),
          "an empty TEXTURE value is malformed");
}

void test_remap_defaults() {
    auto vinyls = make_source({
        p8_spec("240SX_A", 0x80, ramp_indices(16, 8), 0x01000000U),
        p8_spec("240SX_A_MASK", 0x08, ramp_indices(16, 4), 0x02000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
    };
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.vinyl_layers[0] = vinyl("A", 8);
    selection.vinyl_layers[0]->remap = true;
    selection.vinyl_layers[0]->remap_colors[1] = 0x00445566U;

    nfsu::AssembledSkinInputs assembled;
    std::string error;
    check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "a remapping layer assembles: " + error);
    const nfsu::SkinInputLayerInfo& info = assembled.plan().inputs[0];
    check(info.remap, "the layer remaps");
    check_u32(info.remap_colors[0], nfsu::skin_default_remap_color(0), "slot 0 takes the default");
    check_u32(info.remap_colors[1], 0x00445566U, "slot 1 keeps the explicit color");
    check(info.remap_color_defaulted[0] && !info.remap_color_defaulted[1],
          "the defaulted slots are reported");
    check_u32(assembled.request().layers[0].remap_colors[2],
              nfsu::skin_default_remap_color(2), "the request carries the substituted color");

    // 0x005AA10F leaves before the colour loop when REMAP is zero, so a
    // layer that does not remap carries four zeroed colours even when the
    // caller supplied some, and no substitution is reported.
    nfsu::BodySkinSelection quiet = base_selection(nfsu::SkinTargetType::type32);
    quiet.vinyl_layers[0] = vinyl("A", 8);
    quiet.vinyl_layers[0]->remap = false;
    quiet.vinyl_layers[0]->remap_colors[1] = 0x00445566U;
    check(nfsu::assemble_body_skin_inputs(quiet, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "a non-remapping layer assembles: " + error);
    const nfsu::SkinInputLayerInfo& quiet_info = assembled.plan().inputs[0];
    check(!quiet_info.remap, "the layer does not remap");
    for (std::size_t k = 0; k < 4; ++k) {
        check_u32(quiet_info.remap_colors[k], 0,
                  "a non-remapping layer keeps color " + std::to_string(k) + " zeroed");
        check(!quiet_info.remap_color_defaulted[k],
              "no substitution is reported for color " + std::to_string(k));
        check_u32(assembled.request().layers[0].remap_colors[k], 0,
                  "the request carries the zeroed color " + std::to_string(k));
    }
}

void test_hood_and_doorline_never_remap() {
    auto textures = make_source({
        argb_spec("240SX_HOOD_SPOILER", 0x10000000U),
        argb_spec("240SX_HOOD_SPOILER_MASK", 0x20000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.hood_spoiler_vinyl = nfsu::SkinHoodSpoilerSelection{};

    nfsu::AssembledSkinInputs assembled;
    std::string error;
    check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, assembled,
                                          error),
          "the hood and spoiler source assembles: " + error);
    const nfsu::SkinInputLayerInfo& info = assembled.plan().inputs[0];
    check(info.role == nfsu::SkinSourceRole::hood_spoiler_vinyl, "the role is source index 0");
    check(!info.remap, "source index 0 never remaps");
    check_u32(info.palette_entries, nfsu::kSkinFixedPaletteSize, "its NUMCOLOURS is fixed");
    for (std::size_t k = 0; k < 4; ++k) {
        check_u32(info.remap_colors[k], 0, "its remap color " + std::to_string(k) + " stays zero");
        check(!info.remap_color_defaulted[k], "no substitution is reported for it");
    }
}

void test_target_texture() {
    auto textures = make_source({
        argb_spec("240SX_DOORLINE", 0x10000000U),
        argb_spec("240SX_DOORLINE_MASK", 0x20000000U),
        argb_spec("DUMMY_SKIN1", 0x30000000U),
    });
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };
    std::string error;
    nfsu::BodySkinInputPlan plan;

    // With no dimensions given, the target record supplies them.
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.width = 0;
    selection.height = 0;
    selection.target_hash = nfsu::bin_hash("DUMMY_SKIN1");
    selection.doorline = nfsu::SkinDoorlineSelection{};
    check(nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "a named target supplies the dimensions: " + error);
    check(plan.width == 4 && plan.height == 4, "the dimensions come from the target record");
    check(plan.target_texture.resolved, "the target provenance is reported");
    check_u32(plan.target_texture.record_hash, nfsu::bin_hash("DUMMY_SKIN1"),
              "the target record hash is preserved");
    check(plan.inputs[0].usable, "the doorline still resolves");

    // Dimensions that contradict the target are a malformed selection.
    selection.width = 8;
    selection.height = 8;
    check(!nfsu::plan_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{}, plan, error),
          "dimensions that disagree with the target are refused");

    // A zero-dimension target with no named texture is malformed too.
    nfsu::BodySkinSelection zero = base_selection(nfsu::SkinTargetType::type32);
    zero.width = 0;
    zero.height = 0;
    check(!nfsu::plan_body_skin_inputs(zero, sources, nfsu::SkinInputLimits{}, plan, error),
          "a zero dimension is refused");
}

void test_limits() {
    auto textures = make_source(
        {argb_spec("240SX_DOORLINE", 0x10000000U), argb_spec("240SX_DOORLINE_MASK", 0x20000000U)});
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN"),
    };
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.doorline = nfsu::SkinDoorlineSelection{};
    std::string error;
    nfsu::BodySkinInputPlan plan;

    nfsu::SkinInputLimits tight_pixels;
    tight_pixels.max_pixels = 8;
    check(!nfsu::plan_body_skin_inputs(selection, sources, tight_pixels, plan, error),
          "a target over max_pixels is refused");

    // The type-8 cap applies even when max_pixels is left at its default.
    nfsu::BodySkinSelection big = base_selection(nfsu::SkinTargetType::type8);
    big.width = 512;
    big.height = 513;
    check(!nfsu::plan_body_skin_inputs(big, sources, nfsu::SkinInputLimits{}, plan, error),
          "the type-8 cap is applied even when the limit is larger");

    // A payload over the limit is a per-layer skip in both entry points.
    nfsu::SkinInputLimits tight_payload;
    tight_payload.max_payload_bytes = 8;
    check(nfsu::plan_body_skin_inputs(selection, sources, tight_payload, plan, error),
          "an oversized payload is not a hard failure: " + error);
    check(plan.inputs[0].skip_reason == nfsu::SkinSkipReason::source_payload_too_large,
          "the oversized payload is a skip");
    nfsu::AssembledSkinInputs assembled;
    check(nfsu::assemble_body_skin_inputs(selection, sources, tight_payload, assembled, error),
          "the assembly agrees: " + error);
    check(assembled.layer_count() == 0, "nothing was assembled");

    nfsu::SkinInputLimits tight_total;
    tight_total.max_total_bytes = 8;
    check(!nfsu::assemble_body_skin_inputs(selection, sources, tight_total, assembled, error),
          "a total byte budget below one surface is refused");
    check(assembled.layer_count() == 0, "a refused assembly owns nothing");
}

void test_source_validation() {
    auto textures = make_source(
        {argb_spec("240SX_DOORLINE", 0x10000000U), argb_spec("240SX_DOORLINE_MASK", 0x20000000U)});
    nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
    selection.doorline = nfsu::SkinDoorlineSelection{};
    std::string error;
    nfsu::BodySkinInputPlan plan;

    check(!nfsu::plan_body_skin_inputs(selection, {}, nfsu::SkinInputLimits{}, plan, error),
          "an empty source list is refused");

    std::vector<nfsu::SkinInputSource> no_bytes = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN")};
    no_bytes[0].bytes = nullptr;
    check(!nfsu::plan_body_skin_inputs(selection, no_bytes, nfsu::SkinInputLimits{}, plan, error),
          "a source without bytes is refused");

    std::vector<nfsu::SkinInputSource> no_pack = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN")};
    no_pack[0].packs.clear();
    check(!nfsu::plan_body_skin_inputs(selection, no_pack, nfsu::SkinInputLimits{}, plan, error),
          "a source without a pack is refused");

    // A pack parsed from different bytes than the source names.
    auto other = make_source({argb_spec("240SX_DOORLINE", 0x50000000U)});
    std::vector<nfsu::SkinInputSource> mismatched = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN")};
    mismatched[0].packs[0] = &other->pack;
    check(!nfsu::plan_body_skin_inputs(selection, mismatched, nfsu::SkinInputLimits{}, plan, error),
          "a pack not parsed from its source bytes is refused");

    nfsu::BodySkinSelection nameless = selection;
    nameless.car_name.clear();
    std::vector<nfsu::SkinInputSource> sources = {
        source_of(*textures, nfsu::SkinSourceKind::car_textures, "TEXTURES.BIN")};
    check(!nfsu::plan_body_skin_inputs(nameless, sources, nfsu::SkinInputLimits{}, plan, error),
          "a selection without a car name is refused");
}

void test_ownership_lifetime() {
    nfsu::AssembledSkinInputs assembled;
    std::string error;
    std::uintptr_t pack_begin = 0;
    std::uintptr_t pack_end = 0;
    {
        // The packs and their bytes live only inside this scope.
        auto vinyls = make_source({
            p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 8), 0x01000000U),
            p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 4), 0x02000000U),
        });
        pack_begin = reinterpret_cast<std::uintptr_t>(vinyls->bytes.data());
        pack_end = pack_begin + vinyls->bytes.size();
        std::vector<nfsu::SkinInputSource> sources = {
            source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "VINYLS.BIN"),
        };
        nfsu::BodySkinSelection selection = base_selection(nfsu::SkinTargetType::type32);
        selection.vinyl_layers[0] = vinyl("TRIBAL5", 8);
        check(nfsu::assemble_body_skin_inputs(selection, sources, nfsu::SkinInputLimits{},
                                              assembled, error),
              "assembly inside the scope succeeds: " + error);
        // Copied, not borrowed: no view may point into the pack image, even
        // while that image is still alive.
        const std::uintptr_t view =
            reinterpret_cast<std::uintptr_t>(assembled.request().layers[0].source32.data);
        check(view < pack_begin || view >= pack_end,
              "the assembled view does not point into the pack image");
    }
    // Churn the heap so that reading a freed buffer would not quietly
    // return its old contents.
    {
        std::vector<std::vector<unsigned char>> churn;
        for (int i = 0; i < 64; ++i) {
            churn.emplace_back(4096, static_cast<unsigned char>(0xCD));
        }
    }
    // Nothing borrowed from the packs survived, so the request still works.
    check(nfsu::validate_assembled_skin_inputs(assembled, error),
          "the request outlives its sources: " + error);
    check_u32(assembled.request().layers[0].source32.data[0], 0x01000000U,
              "the owned pixels are still readable");
    for (std::size_t i = 0; i < 16; ++i) {
        check_u32(assembled.request().layers[0].source32.data[i],
                  0x01000000U + static_cast<std::uint32_t>(i % 8),
                  "surviving pixel " + std::to_string(i));
    }

    const std::uint32_t* before = assembled.request().layers[0].source32.data;
    const std::uint64_t owned = assembled.owned_bytes();

    // Moving keeps every view valid, because the storage is on the heap.
    nfsu::AssembledSkinInputs moved = std::move(assembled);
    check(moved.request().layers[0].source32.data == before,
          "a move does not relocate the pixels");
    check(moved.owned_bytes() == owned, "a move carries the byte count");
    check(nfsu::validate_assembled_skin_inputs(moved, error), "the moved request validates: " +
                                                                  error);
    check_u32(moved.request().layers[0].source32.data[0], 0x01000000U,
              "the moved request still reads");

    // A moved-from object is empty rather than a request with no layers but
    // a live size, which would composite into a plain base-paint image.
    check(assembled.layer_count() == 0, "the moved-from object has no layers");
    check(assembled.request().width == 0 && assembled.request().height == 0,
          "the moved-from object has no dimensions");
    check(assembled.empty(), "the moved-from object reports itself empty");

    // Move assignment behaves the same way.
    nfsu::AssembledSkinInputs target;
    target = std::move(moved);
    check(target.request().layers[0].source32.data == before,
          "move assignment does not relocate the pixels");
    check(moved.layer_count() == 0, "the move-assigned-from object is empty");

    // Each layer's views point into that layer's own storage.
    check(target.layer_storage(0) != nullptr, "the layer names its storage");
    check(target.request().layers[0].source32.data == target.layer_storage(0)->source32.data(),
          "the view points into that storage");
    check(target.layer_storage(1) == nullptr, "an out-of-range layer has no storage");
    check(target.input_of_layer(0) == 0, "the layer names its plan input");
    check(target.input_of_layer(9) == target.plan().inputs.size(),
          "an out-of-range layer maps past the inputs");

    // The compositor accepts the surviving request.
    nfsu::BodySkin32 out;
    nfsu::SkinMarkerCache markers;
    const std::vector<std::uint32_t> pixels(16, 0);
    check(nfsu::composite_body_skin_32(target.request(), pixels, markers, out, error),
          "the surviving request composites: " + error);

    target.clear();
    check(target.empty() && target.owned_bytes() == 0, "clear releases everything");
}

void test_enum_names() {
    check_text(nfsu::skin_target_type_name(nfsu::SkinTargetType::type8), "type8", "target name");
    check_text(nfsu::skin_source_role_name(nfsu::SkinSourceRole::vinyl_layer3), "vinyl_layer3",
               "role name");
    check_text(nfsu::skin_doorline_variant_name(nfsu::SkinDoorlineVariant::wide_body), "wide_body",
               "doorline variant name");
    check_text(nfsu::skin_hood_spoiler_variant_name(nfsu::SkinHoodSpoilerVariant::spoiler),
               "spoiler", "hood variant name");
    check_text(nfsu::skin_source_kind_name(nfsu::SkinSourceKind::car_vinyls), "car_vinyls",
               "source kind name");
    check_text(nfsu::skin_skip_reason_name(nfsu::SkinSkipReason::mask_not_found), "mask_not_found",
               "skip reason name");
    // The enumerator values are the gatherer's source indexes.
    check(static_cast<std::uint32_t>(nfsu::SkinSourceRole::hood_spoiler_vinyl) == 0U &&
              static_cast<std::uint32_t>(nfsu::SkinSourceRole::doorline) == 5U,
          "the roles carry their source indexes");
}

void test_palette_load() {
    nfsu::P8Palette stored{};
    for (std::size_t i = 0; i < stored.size(); ++i) {
        stored[i] = static_cast<unsigned char>(i & 0xFFU);
    }
    const nfsu::SkinPalette loaded = nfsu::skin_palette_from_p8(stored);
    check_u32(loaded[0], 0x03020100U, "the first entry is a little-endian dword");
    check_u32(loaded[1], 0x07060504U, "the second entry follows it");
    check_u32(loaded[255], 0xFFFEFDFCU, "the last entry is in range");
}

} // namespace

int main() {
    test_names_and_hashes();
    test_enum_names();
    test_palette_load();
    test_type32_assembly();
    test_type8_assembly();
    test_missing_pairs();
    test_mixed_skipped_and_usable();
    test_failed_assembly_empties_output();
    test_dimension_mismatch();
    test_format_refusals();
    test_truncation();
    test_unusable_record_wins();
    test_priority_and_provenance();
    test_names_are_not_keys();
    test_palette_bounds();
    test_numcolours_aborts();
    test_remap_defaults();
    test_hood_and_doorline_never_remap();
    test_target_texture();
    test_limits();
    test_source_validation();
    test_ownership_lifetime();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "skin input tests passed, " << g_checks << " checks\n";
    return 0;
}
