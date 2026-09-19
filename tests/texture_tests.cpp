#include "nfsu/image.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8U * i)) & 0xFFU));
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

std::uint32_t get_u32(const Bytes& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

Bytes jdlz_header(std::uint32_t decompressed, std::uint32_t compressed) {
    Bytes bytes = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};
    append_u32(bytes, decompressed);
    append_u32(bytes, compressed);
    return bytes;
}

// Literal-only JDLZ stream: flags1 = 0 before every 8 literals and one
// flags2 byte at the start, as the decoder loads both registers first.
Bytes jdlz_literals(const Bytes& data, std::size_t trailing = 0) {
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
    body.insert(body.end(), trailing, 0xFFU);
    Bytes blob = jdlz_header(static_cast<std::uint32_t>(data.size()),
                             static_cast<std::uint32_t>(body.size() + 16));
    blob.insert(blob.end(), body.begin(), body.end());
    return blob;
}

Bytes finish_jdlz(std::uint32_t decompressed, const Bytes& body) {
    Bytes blob = jdlz_header(decompressed, static_cast<std::uint32_t>(body.size() + 16));
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
        put_u32(bytes_, header + 4,
                static_cast<std::uint32_t>(bytes_.size() - header - 8));
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
    std::string name = "TEX";
    std::optional<std::uint32_t> hash;
    std::uint8_t format_code = 0x22;
    std::uint32_t d3d_format = 0x31545844U; // "DXT1"
    std::uint16_t width = 4;
    std::uint16_t height = 4;
    std::optional<std::uint8_t> width_log2;
    std::optional<std::uint8_t> height_log2;
    std::uint8_t mips = 1;
    std::uint8_t alpha_test_mode = 0;
    std::uint8_t alpha_blend_mode = 0;
    Bytes pixels;                       // decompressed chain
    std::optional<std::uint32_t> data_size;
    std::optional<std::uint32_t> top_size;
    std::optional<Bytes> blob;          // replaces the literal JDLZ blob
    std::optional<std::uint32_t> table_hash;
    std::optional<std::uint32_t> virtual_offset;  // raw packs: info +48
};

struct PackOptions {
    std::size_t leading_pad = 0;        // padding chunk before the root
    bool corrupt_prefix = false;
    std::optional<std::uint32_t> hash_table_size;
    std::optional<std::uint32_t> info_table_size;
    // Raw storage as in GLOBALB.BUN: no blob table, chains stored
    // uncompressed at data_start + info +48, which starts at raw_base.
    bool raw = false;
    std::uint32_t raw_base = 0;
};

struct PackLayout {
    Bytes bytes;
    std::vector<std::size_t> info_offsets;
    std::vector<std::size_t> blob_entry_offsets;
    std::vector<std::size_t> blob_offsets;
    std::size_t data_payload_offset = 0;
    std::size_t data_prefix = 0;
};

unsigned char log2_of(std::uint32_t value) {
    unsigned char result = 0;
    while ((1U << result) < value) {
        ++result;
    }
    return result;
}

// Appends one pack root to w; offsets in layout are absolute in w.
void write_pack(Writer& w, const std::vector<TexSpec>& specs, const PackOptions& options,
                PackLayout& layout) {
    if (options.leading_pad != 0) {
        w.leaf(0x00000000U, Bytes(options.leading_pad - 8, 0));
    }
    w.begin(nfsu::kTexturePackRootId);
    w.begin(nfsu::kTextureInfoContainerId);

    Bytes header(124, 0);
    put_u32(header, 0, 4);
    std::memcpy(&header[4], "CARTEXTURES", 11);
    const std::string path = "Test\\Pack.tpk";
    std::memcpy(&header[32], path.data(), path.size());
    put_u32(header, 96, nfsu::bin_hash(path));
    w.leaf(nfsu::kTexturePackHeaderId, header);

    std::vector<std::uint32_t> hashes;
    for (const TexSpec& spec : specs) {
        hashes.push_back(spec.hash.value_or(nfsu::bin_hash(spec.name)));
    }

    Bytes hash_table;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        append_u32(hash_table, specs[i].table_hash.value_or(hashes[i]));
        append_u32(hash_table, 0);
    }
    if (options.hash_table_size) {
        hash_table.resize(*options.hash_table_size, 0);
    }
    w.leaf(nfsu::kTextureHashTableId, hash_table);

    if (!options.raw) {
        w.begin(nfsu::kTextureBlobTableId);
        for (std::size_t i = 0; i < specs.size(); ++i) {
            layout.blob_entry_offsets.push_back(w.size());
            Bytes entry;
            append_u32(entry, hashes[i]);
            append_u32(entry, 0); // patched below
            append_u32(entry, 0);
            append_u32(entry, 0x100);
            append_u32(entry, 0);
            w.append(entry);
        }
        w.end();
    }

    Bytes info_table;
    std::uint32_t raw_palette_bytes = 0;
    if (options.raw) {
        for (const TexSpec& spec : specs) {
            if (spec.format_code == 0x08 || spec.format_code == 0x80 ||
                spec.format_code == 0x81) {
                raw_palette_bytes +=
                    static_cast<std::uint32_t>(nfsu::kP8PaletteSize);
            }
        }
    }
    std::uint32_t virtual_offset =
        options.raw ? std::max(options.raw_base, raw_palette_bytes) : 0;
    std::uint32_t raw_palette_offset = 0;
    std::vector<std::uint32_t> virtual_offsets;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const TexSpec& spec = specs[i];
        Bytes record(124, 0);
        std::memcpy(&record[12], spec.name.data(), std::min<std::size_t>(spec.name.size(), 24));
        put_u32(record, 36, hashes[i]);
        put_u32(record, 40, 0x001A93CFU);
        const std::uint32_t size =
            spec.data_size.value_or(static_cast<std::uint32_t>(spec.pixels.size()));
        const std::uint32_t at = spec.virtual_offset.value_or(virtual_offset);
        const bool p8 = spec.format_code == 0x08 || spec.format_code == 0x80 ||
                        spec.format_code == 0x81;
        virtual_offsets.push_back(at);
        put_u32(record, 48, at);
        virtual_offset = (at + size + 127U) & ~127U;
        // Raw P8 packs use +52 as a palette offset from data_start. For
        // other raw formats the field is deliberately arbitrary and ignored.
        put_u32(record, 52, options.raw ? (p8 ? raw_palette_offset : 0x00ABCDEFU)
                                        : virtual_offset);
        put_u32(record, 56, size);
        put_u32(record, 60, p8 ? static_cast<std::uint32_t>(nfsu::kP8PaletteSize) : 0U);
        if (p8) {
            if (options.raw) {
                raw_palette_offset +=
                    static_cast<std::uint32_t>(nfsu::kP8PaletteSize);
            } else {
                virtual_offset += static_cast<std::uint32_t>(nfsu::kP8PaletteSize);
            }
        }
        std::uint64_t top = 0;
        nfsu::PixelFormat format = nfsu::PixelFormat::dxt1;
        if (spec.format_code == 0x24) {
            format = nfsu::PixelFormat::dxt3;
        } else if (spec.format_code == 0x20) {
            format = nfsu::PixelFormat::a8r8g8b8;
        } else if (spec.format_code == 0x08 || spec.format_code == 0x80 ||
                   spec.format_code == 0x81) {
            format = nfsu::PixelFormat::p8;
        }
        nfsu::level_size(format, spec.width, spec.height, top);
        put_u32(record, 64, spec.top_size.value_or(static_cast<std::uint32_t>(top)));
        put_u16(record, 68, spec.width);
        put_u16(record, 70, spec.height);
        record[72] = spec.width_log2.value_or(log2_of(spec.width));
        record[73] = spec.height_log2.value_or(log2_of(spec.height));
        record[74] = spec.format_code;
        record[78] = spec.mips;
        put_u32(record, 84, 0x01000000U);
        record[85] = spec.alpha_test_mode;
        record[86] = spec.alpha_blend_mode;
        layout.info_offsets.push_back(0); // patched after append
        info_table.insert(info_table.end(), record.begin(), record.end());
    }
    if (options.info_table_size) {
        info_table.resize(*options.info_table_size, 0);
    }
    const std::size_t info_payload = w.size() + 8;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        layout.info_offsets[i] = info_payload + i * 124;
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
    w.end(); // info container

    w.begin(nfsu::kTextureDataContainerId);
    Bytes data_header(24, 0);
    put_u32(data_header, 8, 1);
    put_u32(data_header, 12, nfsu::bin_hash(path));
    w.leaf(nfsu::kTextureDataHeaderId, data_header);
    w.begin(nfsu::kTextureDataId);
    layout.data_payload_offset = w.size();
    while (w.size() % 128 != 0) {
        w.bytes().push_back(0x11);
        ++layout.data_prefix;
    }
    if (options.corrupt_prefix && layout.data_prefix != 0) {
        w.bytes()[layout.data_payload_offset] = 0x00;
    }
    if (options.raw) {
        // Zero-filled span with every chain copied to its virtual offset.
        const std::size_t start = w.size();
        std::size_t extent = std::max<std::size_t>(options.raw_base,
                                                   raw_palette_bytes);
        for (std::size_t i = 0; i < specs.size(); ++i) {
            extent = std::max(extent, virtual_offsets[i] + specs[i].pixels.size());
        }
        w.append(Bytes(extent, 0));
        for (std::size_t i = 0; i < specs.size(); ++i) {
            layout.blob_offsets.push_back(start + virtual_offsets[i]);
            std::copy(specs[i].pixels.begin(), specs[i].pixels.end(),
                      w.bytes().begin() + static_cast<std::ptrdiff_t>(start + virtual_offsets[i]));
        }
    } else {
        for (std::size_t i = 0; i < specs.size(); ++i) {
            const Bytes blob = specs[i].blob.value_or(jdlz_literals(specs[i].pixels, i % 3));
            layout.blob_offsets.push_back(w.size());
            put_u32(w.bytes(), layout.blob_entry_offsets[i] + 4,
                    static_cast<std::uint32_t>(w.size()));
            put_u32(w.bytes(), layout.blob_entry_offsets[i] + 8,
                    static_cast<std::uint32_t>(blob.size()));
            w.append(blob);
        }
        w.append(Bytes(3, 0));
    }
    w.end(); // data chunk
    w.end(); // data container
    w.end(); // root
}

PackLayout build_pack(const std::vector<TexSpec>& specs, const PackOptions& options = {}) {
    PackLayout layout;
    Writer w;
    write_pack(w, specs, options, layout);
    w.leaf(0x00000000U, Bytes(16, 0));
    layout.bytes = w.bytes();
    return layout;
}

struct BunLayout {
    Bytes bytes;
    std::vector<PackLayout> packs;       // in root offset order
    std::vector<std::size_t> roots;
    std::size_t middle_container = 0;    // header of the depth-1 container of pack 1
    std::set<std::size_t> top_ends;      // end offsets of top-level chunks
};

// A BUN-like file: an unrelated leaf, raw pack 0 at the top level, raw pack
// 1 two containers deep, JDLZ pack 2 one container deep, trailing padding.
BunLayout build_bun(const std::vector<TexSpec>& a, const std::vector<TexSpec>& b,
                    const std::vector<TexSpec>& c) {
    BunLayout layout;
    layout.packs.resize(3);
    Writer w;
    w.leaf(0x00135200U, Bytes(160, 0x5A));
    layout.top_ends.insert(w.size());
    PackOptions raw;
    raw.raw = true;
    raw.raw_base = 256;
    layout.roots.push_back(w.size());
    write_pack(w, a, raw, layout.packs[0]);
    layout.top_ends.insert(w.size());
    w.leaf(0x00000000U, Bytes(24, 0));
    layout.top_ends.insert(w.size());
    layout.middle_container = w.size();
    w.begin(0x80034602U);
    w.leaf(0x00034603U, Bytes(12, 1));
    w.begin(0x80034700U);
    raw.raw_base = 0;
    layout.roots.push_back(w.size());
    write_pack(w, b, raw, layout.packs[1]);
    w.end();
    w.end();
    layout.top_ends.insert(w.size());
    w.begin(0x80134000U);
    w.leaf(0x00134002U, Bytes(4, 2));
    layout.roots.push_back(w.size());
    write_pack(w, c, PackOptions{}, layout.packs[2]);
    w.end();
    layout.top_ends.insert(w.size());
    w.leaf(0x00000000U, Bytes(64, 0));
    layout.bytes = w.bytes();
    return layout;
}

// A DXT1 block: color0 = pure red, color1 = pure blue, indices chosen per
// pixel from a pattern.
Bytes dxt1_block(std::uint16_t c0, std::uint16_t c1, std::uint32_t indices) {
    Bytes block;
    append_u16(block, c0);
    append_u16(block, c1);
    append_u32(block, indices);
    return block;
}

TexSpec dxt1_spec(const std::string& name) {
    TexSpec spec;
    spec.name = name;
    spec.pixels = dxt1_block(0xF800U, 0x001FU, 0xE4E4E4E4U);
    return spec;
}

TexSpec dxt3_spec() {
    TexSpec spec;
    spec.name = "DXT3_TEX";
    spec.format_code = 0x24;
    spec.d3d_format = 0x33545844U;
    spec.width = 8;
    spec.height = 4;
    for (int block = 0; block < 2; ++block) {
        for (int i = 0; i < 8; ++i) {
            spec.pixels.push_back(static_cast<unsigned char>(0x10 * i + block));
        }
        const Bytes color = dxt1_block(0x07E0U, 0x0000U, 0x00000000U);
        spec.pixels.insert(spec.pixels.end(), color.begin(), color.end());
    }
    return spec;
}

// 2x2 A8R8G8B8 with 2 mips: 16 + 4 bytes.
TexSpec argb_spec() {
    TexSpec spec;
    spec.name = "ARGB_TEX";
    spec.format_code = 0x20;
    spec.d3d_format = 21;
    spec.width = 2;
    spec.height = 2;
    spec.mips = 2;
    for (int i = 0; i < 5; ++i) {
        spec.pixels.insert(spec.pixels.end(),
                           {static_cast<unsigned char>(i), 0x20, 0x30,
                            static_cast<unsigned char>(0x80 + i)});
    }
    return spec;
}

TexSpec p8_spec(const std::string& name, std::uint8_t code = 0x08U) {
    TexSpec spec;
    spec.name = name;
    spec.format_code = code;
    spec.d3d_format = 0x29U;
    spec.width = 4;
    spec.height = 4;
    spec.mips = 1;
    for (unsigned char i = 0; i < 16; ++i) {
        spec.pixels.push_back(i);
    }
    Bytes stored = spec.pixels;
    stored.resize(128, 0);
    for (std::size_t i = 0; i < nfsu::kP8PaletteSize; ++i) {
        stored.push_back(static_cast<unsigned char>(i & 0xFFU));
    }
    spec.blob = jdlz_literals(stored);
    return spec;
}

nfsu::TexturePack parse(const Bytes& bytes, const nfsu::TextureLimits& limits = {}) {
    return nfsu::parse_texture_pack(bytes.data(), bytes.size(), limits);
}

bool has_issue(const nfsu::TexturePack& pack, const std::string& text) {
    for (const nfsu::TextureIssue& issue : pack.issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void dump_issues(const nfsu::TexturePack& pack) {
    for (const nfsu::TextureIssue& issue : pack.issues) {
        std::cerr << "  issue at " << issue.offset << ": " << issue.message << '\n';
    }
}

void expect_texture_rejected(const TexSpec& spec, const std::string& needle,
                             const std::string& label) {
    const PackLayout layout = build_pack({dxt1_spec("GOOD"), spec});
    const nfsu::TexturePack pack = parse(layout.bytes);
    const bool ok = !pack.ok() && pack.textures.size() == 2U &&
                    pack.textures[0].valid && !pack.textures[1].valid &&
                    has_issue(pack, needle);
    if (!ok) {
        std::cerr << "case '" << label << "' expected issue containing '"
                  << needle << "'\n";
        dump_issues(pack);
    }
    check(ok, label);
}

void test_hash() {
    check(nfsu::bin_hash("") == 0xFFFFFFFFU, "hash of empty string");
    // Values observed in the retail 240SX pack (see docs/FORMATS.md).
    check(nfsu::bin_hash("240SX_ENGINE") == 0x4B6F3195U, "hash of 240SX_ENGINE");
    check(nfsu::bin_hash("None") == 0x001A93CFU, "hash of None");
}

void test_valid_pack() {
    const PackLayout layout = build_pack({dxt1_spec("DXT1_TEX"), dxt3_spec(),
                                          argb_spec(), p8_spec("P8_MASK"),
                                          p8_spec("P8_COLOR", 0x80U),
                                          p8_spec("P8_CUSTOM", 0x81U)});
    const nfsu::TexturePack pack = parse(layout.bytes);
    if (!pack.ok()) {
        dump_issues(pack);
    }
    check(pack.ok() && pack.valid_texture_count() == 6U, "valid pack");
    check(pack.pack_name == "CARTEXTURES" && pack.pack_path == "Test\\Pack.tpk" &&
              pack.pack_hash == nfsu::bin_hash("Test\\Pack.tpk") &&
              pack.header_version == 4U,
          "pack header");
    check(pack.data_payload_offset == layout.data_payload_offset &&
              pack.data_prefix_bytes == layout.data_prefix,
          "data payload location");
    if (pack.textures.size() != 6U) {
        return;
    }
    const nfsu::TextureRecord& dxt3 = pack.textures[1];
    check(dxt3.format == nfsu::PixelFormat::dxt3 && dxt3.width == 8 &&
              dxt3.height == 4 && dxt3.mip_count == 1 && dxt3.data_size == 32 &&
              dxt3.top_level_size == 32 && dxt3.hash == nfsu::bin_hash("DXT3_TEX") &&
              dxt3.class_hash == 0x001A93CFU && dxt3.name == "DXT3_TEX" &&
              !dxt3.name_field_full,
          "dxt3 fields");
    check(dxt3.info_offset == layout.info_offsets[1] &&
              dxt3.blob_entry_offset == layout.blob_entry_offsets[1] &&
              dxt3.blob_offset == layout.blob_offsets[1],
          "record offsets");
    check(dxt3.info[74] == 0x24 && dxt3.info[84 + 3] == 0x01 &&
              get_u32(Bytes(dxt3.format_record.begin(), dxt3.format_record.end()), 16) == 6U,
          "raw records preserve unknown fields");
    const nfsu::TextureRecord& argb = pack.textures[2];
    check(argb.format == nfsu::PixelFormat::a8r8g8b8 && argb.mip_count == 2 &&
              argb.data_size == 20 && argb.top_level_size == 16,
          "argb fields");
    check(pack.textures[3].format == nfsu::PixelFormat::p8 &&
              pack.textures[3].data_size == 16 &&
              pack.textures[3].palette_size == nfsu::kP8PaletteSize &&
              pack.textures[4].format == nfsu::PixelFormat::p8 &&
              pack.textures[4].format_code == 0x80U &&
              pack.textures[5].format == nfsu::PixelFormat::p8 &&
              pack.textures[5].format_code == 0x81U,
          "all observed P8 format codes");
    {
        Bytes payload;
        nfsu::P8Palette palette{};
        std::size_t unused = 0;
        std::string error;
        check(nfsu::texture_payload(layout.bytes.data(), layout.bytes.size(),
                                    pack.textures[3], payload, unused, error,
                                    &palette) &&
                  payload == p8_spec("EXPECTED").pixels &&
                  palette[0] == 0U && palette[1] == 1U &&
                  palette[255] == 255U && palette[256] == 0U,
              "P8 payload strips padding and returns palette: " + error);
    }

    for (const nfsu::TextureRecord& texture : pack.textures) {
        Bytes payload;
        std::size_t unused = 99;
        std::string error;
        const bool ok = nfsu::texture_payload(layout.bytes.data(), layout.bytes.size(),
                                              texture, payload, unused, error);
        const std::size_t expected_unused =
            texture.format == nfsu::PixelFormat::p8 ? 0U : texture.index % 3U;
        check(ok && payload.size() == texture.data_size &&
                  unused == expected_unused,
              "payload of texture " + std::to_string(texture.index) + " " + error);
    }
}

void test_alpha_state() {
    TexSpec opaque = dxt1_spec("OPAQUE");

    TexSpec tested = dxt1_spec("TESTED");
    tested.alpha_test_mode = 1;
    tested.alpha_blend_mode = 1;

    TexSpec additive = dxt1_spec("ADDITIVE");
    additive.alpha_test_mode = 2;
    additive.alpha_blend_mode = 2;

    TexSpec other = dxt1_spec("OTHER");
    other.alpha_test_mode = 0xFF;
    other.alpha_blend_mode = 0xFF;

    const PackLayout layout = build_pack({opaque, tested, additive, other});
    const nfsu::TexturePack pack = parse(layout.bytes);
    check(pack.ok() && pack.textures.size() == 4U, "alpha-state fixture parses");
    if (pack.textures.size() != 4U) {
        return;
    }

    check(!nfsu::texture_alpha_test_enabled(pack.textures[0]) &&
              pack.textures[0].alpha_test_mode == 0U &&
              nfsu::texture_alpha_blend(pack.textures[0]) ==
                  nfsu::TextureAlphaBlend::disabled,
          "disabled alpha state");
    check(nfsu::texture_alpha_test_enabled(pack.textures[1]) &&
              pack.textures[1].alpha_test_mode == 1U &&
              pack.textures[1].alpha_blend_mode == 1U &&
              nfsu::texture_alpha_blend(pack.textures[1]) ==
                  nfsu::TextureAlphaBlend::source_over,
          "alpha test and source-over state");
    check(!nfsu::texture_alpha_test_enabled(pack.textures[2]) &&
              pack.textures[2].alpha_test_mode == 2U &&
              nfsu::texture_alpha_blend(pack.textures[2]) ==
                  nfsu::TextureAlphaBlend::additive,
          "mode 2 alpha test stays disabled and blend is additive");
    check(!nfsu::texture_alpha_test_enabled(pack.textures[3]) &&
              pack.textures[3].alpha_test_mode == 0xFFU &&
              pack.textures[3].alpha_blend_mode == 0xFFU &&
              nfsu::texture_alpha_blend(pack.textures[3]) ==
                  nfsu::TextureAlphaBlend::source_over &&
              std::string(nfsu::texture_alpha_blend_name(
                  nfsu::texture_alpha_blend(pack.textures[3]))) == "source-over",
          "unknown raw modes are preserved and follow the retail branches");
}

void test_alignment_sweep() {
    // Moving the pack changes the prefix of the data payload across every
    // 4-byte residue of 128, including none.
    std::set<std::size_t> prefixes;
    for (std::size_t pad = 0; pad <= 256; pad += 4) {
        if (pad == 4) {
            continue; // a padding chunk needs at least its header
        }
        PackOptions options;
        options.leading_pad = pad;
        const PackLayout layout = build_pack({dxt1_spec("A"), argb_spec()}, options);
        const nfsu::TexturePack pack = parse(layout.bytes);
        const bool ok = pack.ok() && pack.valid_texture_count() == 2U &&
                        pack.data_prefix_bytes == layout.data_prefix &&
                        (pack.data_payload_offset + pack.data_prefix_bytes) % 128U == 0U;
        if (!ok) {
            dump_issues(pack);
        }
        check(ok, "alignment sweep pad " + std::to_string(pad));
        prefixes.insert(layout.data_prefix);
    }
    check(prefixes.count(0) == 1U && prefixes.size() == 32U,
          "sweep covers every 4-byte residue including zero");

    PackOptions options;
    options.corrupt_prefix = true;
    const PackLayout layout = build_pack({dxt1_spec("A")}, options);
    check(layout.data_prefix != 0U, "corrupt prefix precondition");
    const nfsu::TexturePack pack = parse(layout.bytes);
    check(has_issue(pack, "alignment prefix byte at file offset " +
                              std::to_string(layout.data_payload_offset)) &&
              pack.textures.empty(),
          "bad data prefix rejected");
}


// Copies the blobs of textures 1 and 2 to the given offsets relative to the
// blob of texture 0 and points the blob table at the copies. Texture 0 has
// 100 unused trailing bytes, so the copies can sit inside it; header checks
// still pass for every texture on its own.
Bytes relocate_blobs(std::size_t b_at, std::size_t c_at, std::size_t& base) {
    TexSpec a = dxt1_spec("A");
    a.blob = jdlz_literals(a.pixels, 100);
    TexSpec b = dxt1_spec("B");
    b.blob = jdlz_literals(b.pixels);
    TexSpec c = dxt1_spec("C");
    c.blob = jdlz_literals(c.pixels);
    const PackLayout layout = build_pack({a, b, c});
    Bytes bytes = layout.bytes;
    base = layout.blob_offsets[0];
    const std::size_t targets[2] = {b_at, c_at};
    const Bytes* blobs[2] = {&*b.blob, &*c.blob};
    for (std::size_t i = 0; i < 2; ++i) {
        std::copy(blobs[i]->begin(), blobs[i]->end(), bytes.begin() + base + targets[i]);
        put_u32(bytes, layout.blob_entry_offsets[i + 1] + 4,
                static_cast<std::uint32_t>(base + targets[i]));
    }
    return bytes;
}

void test_nested_and_chained_overlap() {
    {
        // A = [0, 126) contains B = [30, 56) and C = [60, 86); B and C are
        // disjoint, so a neighbor-only check would miss A/C.
        std::size_t base = 0;
        const Bytes bytes = relocate_blobs(30, 60, base);
        const nfsu::TexturePack pack = parse(bytes);
        if (pack.textures.size() != 3U) {
            check(false, "nested fixture");
            return;
        }
        check(pack.textures[0].blob_size == 126U && pack.textures[1].blob_size == 26U,
              "nested fixture sizes");
        check(!pack.textures[0].valid && !pack.textures[1].valid && !pack.textures[2].valid,
              "nested overlap invalidates A, B and C");
        check(pack.textures[0].problem ==
                  "JDLZ blob at file offset " + std::to_string(base) +
                      " (126 bytes) overlaps the blobs of 2 textures: 1, 2",
              "container lists both partners: " + pack.textures[0].problem);
        check(pack.textures[1].problem.find("overlaps the blobs of 1 textures: 0") !=
                      std::string::npos &&
                  pack.textures[2].problem.find("overlaps the blobs of 1 textures: 0") !=
                      std::string::npos,
              "contained blobs name only the container");
        std::size_t overlap_issues = 0;
        for (const nfsu::TextureIssue& issue : pack.issues) {
            overlap_issues += issue.message.find("overlaps") != std::string::npos ? 1U : 0U;
        }
        check(overlap_issues == 3U, "one overlap issue per texture");
        const nfsu::TexturePack again = parse(bytes);
        bool same = again.issues.size() == pack.issues.size();
        for (std::size_t i = 0; same && i < pack.issues.size(); ++i) {
            same = again.issues[i].offset == pack.issues[i].offset &&
                   again.issues[i].message == pack.issues[i].message;
        }
        check(same, "overlap diagnostics are deterministic");
    }
    {
        // Chain: A = [0, 126), B = [110, 136), C = [130, 156). A/B and B/C
        // overlap, A/C do not.
        std::size_t base = 0;
        const nfsu::TexturePack pack = parse(relocate_blobs(110, 130, base));
        check(pack.textures.size() == 3U && pack.valid_texture_count() == 0U,
              "chain invalidates every member");
        if (pack.textures.size() == 3U) {
            check(pack.textures[0].problem.find("of 1 textures: 1") != std::string::npos &&
                      pack.textures[1].problem.find("of 2 textures: 0, 2") != std::string::npos &&
                      pack.textures[2].problem.find("of 1 textures: 1") != std::string::npos,
                  "chain partners are exact");
        }
    }
    {
        // Adjacent blobs share no byte and stay valid.
        std::size_t base = 0;
        const nfsu::TexturePack pack = parse(relocate_blobs(126, 152, base));
        check(pack.ok() && pack.valid_texture_count() == 3U, "touching blobs are not overlaps");
    }
}

void test_truncation_and_overflow() {
    const PackLayout layout = build_pack({dxt1_spec("A"), dxt3_spec()});
    for (std::size_t cut = 0; cut < layout.bytes.size(); cut += 7) {
        const Bytes bytes(layout.bytes.begin(), layout.bytes.begin() + cut);
        const nfsu::TexturePack pack = parse(bytes);
        check(!pack.ok(), "truncated at " + std::to_string(cut) + " reported");
    }
    {
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.blob_entry_offsets[1] + 8, 0xFFFFFFF0U);
        const nfsu::TexturePack pack = parse(bytes);
        check(pack.textures.size() == 2U && pack.textures[0].valid &&
                  !pack.textures[1].valid && has_issue(pack, "outside the texture data range"),
              "huge blob size rejected");
    }
    {
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.blob_entry_offsets[1] + 4, 0xFFFFFFFFU);
        const nfsu::TexturePack pack = parse(bytes);
        check(has_issue(pack, "outside the texture data range"), "huge blob offset rejected");
    }
    {
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.blob_entry_offsets[1] + 4, 0);
        const nfsu::TexturePack pack = parse(bytes);
        check(has_issue(pack, "outside the texture data range"),
              "blob offset before the aligned data start rejected");
    }
    {
        // Two textures with identical blobs; point the second at the first
        // so each blob is valid on its own but they share bytes.
        TexSpec twin = dxt1_spec("B");
        twin.blob = jdlz_literals(twin.pixels);
        TexSpec other = dxt1_spec("C");
        other.blob = twin.blob;
        const PackLayout twins = build_pack({twin, other});
        Bytes bytes = twins.bytes;
        put_u32(bytes, twins.blob_entry_offsets[1] + 4,
                static_cast<std::uint32_t>(twins.blob_offsets[0]));
        check(parse(twins.bytes).ok(), "twin blobs are valid apart");
        const nfsu::TexturePack pack = parse(bytes);
        check(!pack.ok() && pack.valid_texture_count() == 0U &&
                  has_issue(pack, "overlaps the blobs of 1 textures: 1") &&
                  has_issue(pack, "overlaps the blobs of 1 textures: 0"),
              "identical starts rejected");
    }
    {
        PackOptions options;
        options.hash_table_size = 12;
        const nfsu::TexturePack pack = parse(build_pack({dxt1_spec("A")}, options).bytes);
        check(has_issue(pack, "is not a multiple of 8"), "hash table size");
    }
    {
        PackOptions options;
        options.info_table_size = 124 * 2;
        const nfsu::TexturePack pack = parse(build_pack({dxt1_spec("A")}, options).bytes);
        check(has_issue(pack, "1 records of 124 bytes need 124"), "info table count");
    }
    {
        nfsu::TextureLimits limits;
        limits.max_textures = 1;
        const nfsu::TexturePack pack = parse(layout.bytes, limits);
        check(has_issue(pack, "lists 2 textures; limit is 1"), "texture count limit");
    }
    {
        nfsu::TextureLimits limits;
        limits.max_chunks = 4;
        check(has_issue(parse(layout.bytes, limits), "chunk limit 4 exceeded"), "chunk limit");
    }
    {
        nfsu::TextureLimits limits;
        limits.max_decompressed_size = 16;
        check(has_issue(parse(layout.bytes, limits), "exceeds the limit 16"),
              "decompressed size limit");
    }
    {
        const nfsu::TexturePack pack = nfsu::parse_texture_pack(nullptr, 16);
        check(has_issue(pack, "null input"), "null input");
    }
    {
        Writer w;
        w.leaf(0x00000000U, Bytes(8, 0));
        check(has_issue(parse(w.bytes()), "expected exactly one 0xB3300000 chunk, found 0"),
              "missing root");
    }
}

void test_malformed_records() {
    {
        TexSpec spec = dxt1_spec("ZERO");
        spec.width = 0;
        spec.width_log2 = 0;
        expect_texture_rejected(spec, "dimensions 0x4 are outside", "zero width");
    }
    {
        TexSpec spec = dxt1_spec("HUGE");
        spec.width = 8192;
        expect_texture_rejected(spec, "dimensions 8192x4 are outside", "width limit");
    }
    {
        TexSpec spec = dxt1_spec("LOG2");
        spec.width_log2 = 3;
        expect_texture_rejected(spec, "do not match log2 fields 3, 2", "log2 mismatch");
    }
    {
        TexSpec spec = dxt1_spec("NPOT");
        spec.width = 6;
        spec.width_log2 = 3;
        expect_texture_rejected(spec, "do not match log2 fields", "non power of two");
    }
    {
        TexSpec spec = dxt1_spec("MIPS0");
        spec.mips = 0;
        expect_texture_rejected(spec, "mip count 0 is outside 1..3", "zero mips");
    }
    {
        TexSpec spec = dxt1_spec("MIPS9");
        spec.mips = 9;
        expect_texture_rejected(spec, "mip count 9 is outside 1..3", "too many mips");
    }
    {
        TexSpec spec = dxt1_spec("SIZE");
        spec.data_size = 16;
        expect_texture_rejected(spec, "needs 8 top-level and 8 total bytes; info +64 is 8 and +56 is 16",
                                "data size mismatch");
    }
    {
        TexSpec spec = dxt1_spec("TOP");
        spec.top_size = 4;
        expect_texture_rejected(spec, "info +64 is 4", "top size mismatch");
    }
    {
        TexSpec spec = dxt1_spec("HASH");
        spec.table_hash = 0x12345678U;
        expect_texture_rejected(spec, "differs from hash table 0x12345678", "hash table mismatch");
    }
    {
        TexSpec spec = dxt1_spec("NONAME");
        spec.name = std::string(24, 'N');
        expect_texture_rejected(spec, "no NUL terminator", "unterminated name");
    }
    {
        TexSpec spec = dxt1_spec("BAD");
        spec.name = "BAD\x7F";
        expect_texture_rejected(spec, "byte 3 is not printable", "non-printable name");
    }
    {
        TexSpec spec = dxt1_spec("MAGIC");
        Bytes blob = jdlz_literals(spec.pixels);
        blob[4] = 0x03;
        spec.blob = blob;
        expect_texture_rejected(spec, "does not start with JDLZ 02 10 00 00", "bad JDLZ version");
    }
    {
        TexSpec spec = dxt1_spec("JSIZE");
        Bytes blob = jdlz_literals(spec.pixels);
        blob[8] = 9;
        spec.blob = blob;
        expect_texture_rejected(spec, "JDLZ header sizes 9/", "JDLZ size mismatch");
    }
    {
        TexSpec spec = p8_spec("P8_JSIZE");
        put_u32(*spec.blob, 8, static_cast<std::uint32_t>(spec.pixels.size()));
        expect_texture_rejected(spec, "differ from expected decompressed size 1152",
                                "P8 JDLZ includes aligned palette data");
    }
    {
        const PackLayout layout = build_pack({p8_spec("P8_PALETTE_SIZE")});
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[0] + 60, 512);
        const nfsu::TexturePack pack = parse(bytes);
        check(pack.textures.size() == 1U && !pack.textures[0].valid &&
                  has_issue(pack, "P8 palette size at info +60 is 512; expected 1024"),
              "P8 palette size field checked");
    }
    {
        const PackLayout layout = build_pack({p8_spec("P8_PALETTE_OFFSET")});
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[0] + 52, 64);
        const nfsu::TexturePack pack = parse(bytes);
        check(pack.textures.size() == 1U && !pack.textures[0].valid &&
                  has_issue(pack, "P8 palette virtual offset at info +52 is 64; expected 128"),
              "P8 palette offset field checked");
    }
    {
        TexSpec spec = dxt1_spec("SHORT");
        spec.blob = Bytes{'J', 'D', 'L', 'Z'};
        expect_texture_rejected(spec, "smaller than its 16-byte header", "short blob");
    }
}

void test_unsupported_formats() {
    {
        TexSpec spec = dxt1_spec("DXT5");
        spec.format_code = 0x26;
        spec.d3d_format = 0x35545844U;
        expect_texture_rejected(spec,
                                "unsupported format: code 0x26 at info +74 with D3D format 0x35545844",
                                "DXT5 is not claimed");
    }
    {
        TexSpec spec = dxt1_spec("MIXED");
        spec.d3d_format = 0x33545844U; // code says DXT1, record says DXT3
        expect_texture_rejected(spec, "unsupported format: code 0x22", "disagreeing format fields");
    }
    std::uint64_t size = 0;
    check(!nfsu::level_size(nfsu::PixelFormat::unsupported, 4, 4, size), "unsupported level size");
    Bytes rgba;
    std::string error;
    const Bytes data(16, 0);
    check(!nfsu::decode_level(nfsu::PixelFormat::unsupported, 4, 4, data.data(), data.size(),
                              rgba, error),
          "unsupported decode refused");
    const Bytes indices(16, 0);
    check(!nfsu::decode_level(nfsu::PixelFormat::p8, 4, 4, indices.data(), indices.size(),
                              rgba, error) &&
              error == "P8 level requires a 256-entry palette" && rgba.empty(),
          "P8 decode requires a palette");
    std::ostringstream output;
    check(!nfsu::write_dds(output, nfsu::PixelFormat::unsupported, 4, 4, 1, data.data(),
                           data.size(), error) &&
              output.str().empty(),
          "unsupported DDS refused");
    std::ostringstream p8_output;
    check(!nfsu::write_dds(p8_output, nfsu::PixelFormat::p8, 4, 4, 1,
                           indices.data(), indices.size(), error) &&
              error == "P8 DDS export requires a 256-entry palette" &&
              p8_output.str().empty(),
          "P8 DDS requires a palette");
}

void test_duplicates_and_naming() {
    TexSpec first = dxt1_spec("SAME_NAME");
    first.hash = 0x00000001U;
    TexSpec second = dxt1_spec("SAME_NAME");
    second.hash = 0x00000002U;
    TexSpec third = dxt1_spec("240SX_STYLE01_BRAKELIGH");
    third.hash = 0x00000002U; // duplicate hash, truncated name
    TexSpec fourth = dxt1_spec("A B/C:\\D");
    const nfsu::TexturePack pack = parse(build_pack({first, second, third, fourth}).bytes);
    check(pack.textures.size() == 4U && pack.valid_texture_count() == 4U,
          "duplicates stay exportable");
    check(has_issue(pack, "texture 2 repeats hash 0x00000002 of texture 1"),
          "duplicate hash reported");
    if (pack.textures.size() != 4U) {
        return;
    }
    check(pack.textures[2].name_field_full && !pack.textures[0].name_field_full,
          "full name field flagged");
    std::set<std::string> stems;
    for (const nfsu::TextureRecord& texture : pack.textures) {
        stems.insert(nfsu::texture_file_stem(texture));
    }
    check(stems.size() == 4U, "file stems are unique");
    check(nfsu::texture_file_stem(pack.textures[0]) == "t0000_00000001_SAME_NAME" &&
              nfsu::texture_file_stem(pack.textures[1]) == "t0001_00000002_SAME_NAME",
          "stem format");
    check(nfsu::texture_file_stem(pack.textures[3]) ==
              "t0003_" + nfsu::texture_file_stem(pack.textures[3]).substr(6, 8) + "_A_B_C__D",
          "unsafe characters replaced");
    nfsu::TextureRecord unnamed;
    unnamed.index = 12345;
    unnamed.hash = 0xABCDEF01U;
    check(nfsu::texture_file_stem(unnamed) == "t12345_ABCDEF01_unnamed", "unnamed stem");
}

void test_jdlz() {
    Bytes output;
    std::size_t unused = 0;
    std::string error;
    {
        // Two literals then a short match (flags2 bit set): distance 2,
        // length 6.
        const Bytes blob = finish_jdlz(8, {0x04, 0x01, 'A', 'B', 0x01, 0x03});
        check(nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  std::string(output.begin(), output.end()) == "ABABABAB" && unused == 0,
              "short match " + error);
    }
    {
        // 17 literals then a long match (flags2 bit clear): distance 17,
        // length 3.
        Bytes body = {0x00, 0x00};
        for (int i = 0; i < 8; ++i) body.push_back(static_cast<unsigned char>('a' + i));
        body.push_back(0x00);
        for (int i = 8; i < 16; ++i) body.push_back(static_cast<unsigned char>('a' + i));
        body.push_back(0x02);
        body.push_back('q');
        body.push_back(0x00);
        body.push_back(0x00);
        const Bytes blob = finish_jdlz(20, body);
        check(nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  std::string(output.begin(), output.end()) == "abcdefghijklmnopqabc",
              "long match " + error);
    }
    {
        const Bytes blob = finish_jdlz(8, {0x01, 0x01, 0x00, 0x05});
        check(!nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  error.find("reaches 1 bytes back from output offset 0") != std::string::npos &&
                  output.empty(),
              "match before output start rejected");
    }
    {
        const Bytes blob = finish_jdlz(4, {0x02, 0x01, 'A', 0x00, 0x05});
        check(!nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  error.find("overruns the output") != std::string::npos,
              "match past output end rejected");
    }
    {
        const Bytes blob = finish_jdlz(8, {0x00, 0x00, 'A', 'B'});
        check(!nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  error.find("ended after 2 of 8") != std::string::npos,
              "truncated stream rejected");
    }
    {
        Bytes blob = jdlz_literals(Bytes(4, 7));
        put_u32(blob, 12, 99);
        check(!nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  error.find("differs from the blob size") != std::string::npos,
              "compressed size mismatch rejected");
    }
    {
        const Bytes blob = jdlz_literals(Bytes(100, 7));
        check(!nfsu::jdlz_decompress(blob.data(), blob.size(), 99, output, unused, error) &&
                  error.find("exceeds the limit 99") != std::string::npos,
              "output limit enforced before allocation");
    }
    {
        const Bytes blob = jdlz_literals(Bytes(3, 7), 2);
        check(nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  unused == 2U && output == Bytes(3, 7),
              "trailing input reported as unused");
    }
    {
        const Bytes blob = jdlz_header(0, 16);
        check(nfsu::jdlz_decompress(blob.data(), blob.size(), 64, output, unused, error) &&
                  output.empty(),
              "empty output");
        check(!nfsu::jdlz_decompress(blob.data(), 15, 64, output, unused, error),
              "header truncation rejected");
    }
}

std::array<unsigned char, 4> pixel(const Bytes& rgba, std::uint32_t width, std::uint32_t x,
                                   std::uint32_t y) {
    const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4U;
    return {rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

void test_block_decoding() {
    std::uint64_t size = 0;
    check(nfsu::level_size(nfsu::PixelFormat::dxt1, 1, 1, size) && size == 8U, "1x1 DXT1");
    check(nfsu::level_size(nfsu::PixelFormat::dxt3, 5, 3, size) && size == 32U, "5x3 DXT3");
    check(nfsu::level_size(nfsu::PixelFormat::a8r8g8b8, 3, 2, size) && size == 24U, "ARGB");
    check(nfsu::level_size(nfsu::PixelFormat::p8, 512, 512, size) &&
              size == 262144U,
          "512x512 P8");
    check(!nfsu::level_size(nfsu::PixelFormat::dxt1, 0, 4, size), "zero width");
    check(nfsu::mip_chain_size(nfsu::PixelFormat::dxt1, 256, 256, 6, size) && size == 0xAAA0U,
          "retail 256x256 DXT1 6-level size");
    check(nfsu::mip_chain_size(nfsu::PixelFormat::a8r8g8b8, 512, 512, 7, size) &&
              size == 0x155500U,
          "retail 512x512 ARGB 7-level size");
    check(nfsu::mip_chain_size(nfsu::PixelFormat::p8, 512, 512, 7, size) &&
              size == 349504U,
          "retail 512x512 P8 7-level size");
    // 8x2, 4x1, 2x1, 1x1: 2 + 1 + 1 + 1 blocks.
    check(nfsu::mip_chain_size(nfsu::PixelFormat::dxt1, 8, 2, 4, size) && size == 40U,
          "non-square chain clamps at 1");
    check(!nfsu::mip_chain_size(nfsu::PixelFormat::dxt1, 4, 4, 0, size), "zero mips");

    Bytes rgba;
    std::string error;
    {
        // Four-color mode: red, blue, 2/3 red + 1/3 blue, 1/3 red + 2/3 blue.
        // Row y uses indices 0,1,2,3 (0xE4 = 11 10 01 00).
        const Bytes block = dxt1_block(0xF800U, 0x001FU, 0xE4E4E4E4U);
        check(nfsu::decode_level(nfsu::PixelFormat::dxt1, 4, 4, block.data(), block.size(),
                                 rgba, error),
              "dxt1 decode");
        check(pixel(rgba, 4, 0, 0) == std::array<unsigned char, 4>{255, 0, 0, 255} &&
                  pixel(rgba, 4, 1, 0) == std::array<unsigned char, 4>{0, 0, 255, 255} &&
                  pixel(rgba, 4, 2, 3) == std::array<unsigned char, 4>{170, 0, 85, 255} &&
                  pixel(rgba, 4, 3, 3) == std::array<unsigned char, 4>{85, 0, 170, 255},
              "dxt1 four-color palette");
    }
    {
        // Three-color mode (color0 <= color1): index 2 is the midpoint and
        // index 3 transparent black.
        const Bytes block = dxt1_block(0x001FU, 0xF800U, 0xE4E4E4E4U);
        nfsu::decode_level(nfsu::PixelFormat::dxt1, 4, 4, block.data(), block.size(), rgba,
                           error);
        check(pixel(rgba, 4, 2, 0) == std::array<unsigned char, 4>{127, 0, 127, 255} &&
                  pixel(rgba, 4, 3, 0) == std::array<unsigned char, 4>{0, 0, 0, 0},
              "dxt1 three-color palette");
    }
    {
        // 5x3 image: 2x1 blocks. Right block is solid green; only its first
        // column is visible and the bottom row of both blocks is cropped.
        Bytes data = dxt1_block(0xF800U, 0xF800U, 0);
        const Bytes green = dxt1_block(0x07E0U, 0x07E0U, 0);
        data.insert(data.end(), green.begin(), green.end());
        check(nfsu::decode_level(nfsu::PixelFormat::dxt1, 5, 3, data.data(), data.size(), rgba,
                                 error) &&
                  rgba.size() == 5U * 3U * 4U,
              "edge block decode");
        check(pixel(rgba, 5, 3, 2) == std::array<unsigned char, 4>{255, 0, 0, 255} &&
                  pixel(rgba, 5, 4, 0) == std::array<unsigned char, 4>{0, 255, 0, 255} &&
                  pixel(rgba, 5, 4, 2) == std::array<unsigned char, 4>{0, 255, 0, 255},
              "edge blocks cropped");
        check(!nfsu::decode_level(nfsu::PixelFormat::dxt1, 5, 3, data.data(), 8, rgba, error) &&
                  error.find("needs exactly 16 bytes, got 8") != std::string::npos,
              "short edge data rejected");
    }
    {
        // 1x1 DXT3 uses one whole block.
        Bytes block = {0x0F, 0, 0, 0, 0, 0, 0, 0};
        const Bytes color = dxt1_block(0x001FU, 0xF800U, 0x00000003U);
        block.insert(block.end(), color.begin(), color.end());
        check(nfsu::decode_level(nfsu::PixelFormat::dxt3, 1, 1, block.data(), block.size(),
                                 rgba, error) &&
                  rgba == Bytes({170, 0, 85, 255}),
              "1x1 dxt3 uses four-color mode and explicit alpha");
        block[0] = 0x70;
        nfsu::decode_level(nfsu::PixelFormat::dxt3, 1, 1, block.data(), block.size(), rgba,
                           error);
        check(rgba[3] == 0U, "dxt3 low nibble is the first pixel");
    }
    {
        const Bytes data = {1, 2, 3, 4, 5, 6, 7, 8};
        check(nfsu::decode_level(nfsu::PixelFormat::a8r8g8b8, 2, 1, data.data(), data.size(),
                                 rgba, error) &&
                  rgba == Bytes({3, 2, 1, 4, 7, 6, 5, 8}),
              "argb bytes B,G,R,A reordered without change");
    }
}

void test_writers() {
    std::string error;
    {
        const Bytes rgba = {1, 2, 3, 4, 5, 6, 7, 8};
        std::ostringstream output;
        check(nfsu::write_tga(output, 2, 1, rgba, error), "write tga");
        const std::string text = output.str();
        const Bytes bytes(text.begin(), text.end());
        const Bytes expected = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 0x28,
                                3, 2, 1, 4, 7, 6, 5, 8};
        check(bytes == expected, "tga bytes");
        std::ostringstream bad;
        check(!nfsu::write_tga(bad, 2, 2, rgba, error) && bad.str().empty(),
              "tga size mismatch refused");
        check(!nfsu::write_tga(bad, 70000, 1, rgba, error), "tga dimension limit");
    }
    {
        const Bytes payload(8 + 8, 0xAB);
        std::ostringstream output;
        check(nfsu::write_dds(output, nfsu::PixelFormat::dxt1, 4, 4, 2, payload.data(),
                              payload.size(), error),
              "write dds");
        const std::string text = output.str();
        const Bytes bytes(text.begin(), text.end());
        check(bytes.size() == 128U + 16U && std::string(text, 0, 4) == "DDS " &&
                  get_u32(bytes, 4) == 124U && get_u32(bytes, 12) == 4U &&
                  get_u32(bytes, 20) == 8U && get_u32(bytes, 28) == 2U &&
                  std::string(text, 84, 4) == "DXT1" && bytes[128] == 0xAB,
              "dds header and raw payload");
        std::ostringstream bad;
        check(!nfsu::write_dds(bad, nfsu::PixelFormat::dxt1, 4, 4, 3, payload.data(),
                               payload.size(), error) &&
                  bad.str().empty(),
              "dds size mismatch refused");
    }
}

bool set_has_issue(const nfsu::TexturePackSet& set, const std::string& text) {
    for (const nfsu::TextureIssue& issue : set.issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void dump_set(const nfsu::TexturePackSet& set) {
    for (const nfsu::TextureIssue& issue : set.issues) {
        std::cerr << "  set issue at " << issue.offset << ": " << issue.message << '\n';
    }
    for (std::size_t p = 0; p < set.packs.size(); ++p) {
        for (const nfsu::TextureIssue& issue : set.packs[p].issues) {
            std::cerr << "  pack " << p << " issue at " << issue.offset << ": "
                      << issue.message << '\n';
        }
    }
}

std::string describe(const nfsu::TexturePackSet& set) {
    std::ostringstream text;
    text << set.complete << '|';
    for (const nfsu::TextureIssue& issue : set.issues) {
        text << issue.offset << ':' << issue.message << '|';
    }
    for (const nfsu::TexturePack& pack : set.packs) {
        text << "pack " << pack.root_offset << '|';
        for (const nfsu::TextureIssue& issue : pack.issues) {
            text << issue.offset << ':' << issue.message << '|';
        }
        for (const nfsu::TextureRecord& texture : pack.textures) {
            text << texture.index << ',' << texture.hash << ',' << texture.valid << ','
                 << texture.blob_offset << '|';
        }
    }
    return text.str();
}

nfsu::TexturePackSet parse_set(const Bytes& bytes, const nfsu::TextureLimits& limits = {}) {
    return nfsu::parse_texture_pack_set(bytes.data(), bytes.size(), limits);
}

std::vector<TexSpec> bun_a() { return {dxt1_spec("GA1"), dxt3_spec(), argb_spec()}; }
std::vector<TexSpec> bun_b() { return {dxt1_spec("GB1")}; }
std::vector<TexSpec> bun_c() { return {dxt1_spec("GC1")}; }

void test_pack_set() {
    const BunLayout bun = build_bun(bun_a(), bun_b(), bun_c());
    const nfsu::TexturePackSet set = parse_set(bun.bytes);
    if (!set.ok()) {
        dump_set(set);
    }
    check(set.ok() && set.complete && set.packs.size() == 3U, "three packs found");
    if (set.packs.size() != 3U) {
        return;
    }
    for (std::size_t p = 0; p < 3; ++p) {
        check(set.packs[p].root_offset == bun.roots[p], "root offset " + std::to_string(p));
        check(set.packs[p].parse_source == bun.bytes.data(),
              "parse source of pack " + std::to_string(p));
    }
    const nfsu::TexturePack& a = set.packs[0];
    check(a.storage == nfsu::TextureStorage::raw && a.tables_parsed &&
              set.packs[1].storage == nfsu::TextureStorage::raw &&
              set.packs[2].storage == nfsu::TextureStorage::jdlz,
          "storage kinds");
    check(a.valid_texture_count() == 3U && a.data_start % 128U == 0U &&
              a.data_start == bun.packs[0].data_payload_offset + bun.packs[0].data_prefix,
          "raw pack data start");
    const std::vector<TexSpec> specs = bun_a();
    for (std::size_t i = 0; i < a.textures.size(); ++i) {
        const nfsu::TextureRecord& texture = a.textures[i];
        check(texture.storage == nfsu::TextureStorage::raw &&
                  texture.blob_offset == bun.packs[0].blob_offsets[i] &&
                  texture.blob_size == texture.data_size && texture.virtual_end == 0x00ABCDEFU,
              "raw record " + std::to_string(i) + " maps to data_start + info +48");
        Bytes payload;
        std::size_t unused = 99;
        std::string error;
        check(nfsu::texture_payload(bun.bytes.data(), bun.bytes.size(), texture, payload,
                                    unused, error) &&
                  payload == specs[i].pixels && unused == 0U,
              "raw payload " + std::to_string(i) + " " + error);
    }
    // 256-byte base, then DXT1 (8) at 256, DXT3 (32) at 384, ARGB (20) at 512.
    check(a.textures[0].virtual_offset == 256U && a.textures[2].virtual_offset == 512U &&
              a.raw_extent == 532U,
          "raw extent is the largest virtual end");
    check(set.packs[2].valid_texture_count() == 1U && set.packs[2].raw_extent == 0U,
          "nested JDLZ pack");

    // Decoding a raw DXT3 chain gives the same pixels as the JDLZ pack.
    Bytes raw_rgba;
    Bytes jdlz_rgba;
    {
        std::string error;
        Bytes payload;
        std::size_t unused = 0;
        nfsu::texture_payload(bun.bytes.data(), bun.bytes.size(), a.textures[1], payload,
                              unused, error);
        nfsu::decode_level(nfsu::PixelFormat::dxt3, 8, 4, payload.data(), payload.size(),
                           raw_rgba, error);
        const PackLayout standalone = build_pack({dxt3_spec()});
        const nfsu::TexturePack pack = parse(standalone.bytes);
        nfsu::texture_payload(standalone.bytes.data(), standalone.bytes.size(),
                              pack.textures[0], payload, unused, error);
        nfsu::decode_level(nfsu::PixelFormat::dxt3, 8, 4, payload.data(), payload.size(),
                           jdlz_rgba, error);
    }
    check(!raw_rgba.empty() && raw_rgba == jdlz_rgba, "raw and JDLZ storage decode alike");

    // Deterministic results and order.
    check(describe(parse_set(bun.bytes)) == describe(set), "pack set parse is deterministic");

    // Every pack also parses on its own at its root, with the same records.
    for (std::size_t p = 0; p < 3; ++p) {
        const nfsu::TexturePack alone =
            nfsu::parse_texture_pack_at(bun.bytes.data(), bun.bytes.size(), bun.roots[p]);
        check(alone.ok() && alone.textures.size() == set.packs[p].textures.size(),
              "parse_texture_pack_at " + std::to_string(p));
    }

    // The standalone parser keeps its rules: a raw pack is not accepted.
    const nfsu::TexturePack standalone = parse(bun.bytes);
    check(has_issue(standalone, "expected exactly one 0x33310003 chunk, found 0") &&
              standalone.textures.empty(),
          "standalone parser still requires a blob table");

    // A standalone TEXTURES.BIN gives the same pack through the set parser.
    const PackLayout single = build_pack({dxt1_spec("A"), dxt3_spec(), argb_spec()});
    const nfsu::TexturePack direct = parse(single.bytes);
    const nfsu::TexturePackSet wrapped = parse_set(single.bytes);
    bool same = wrapped.ok() && wrapped.packs.size() == 1U &&
                wrapped.packs[0].textures.size() == direct.textures.size();
    for (std::size_t i = 0; same && i < direct.textures.size(); ++i) {
        const nfsu::TextureRecord& x = direct.textures[i];
        const nfsu::TextureRecord& y = wrapped.packs[0].textures[i];
        same = x.hash == y.hash && x.valid == y.valid && x.blob_offset == y.blob_offset &&
               x.blob_size == y.blob_size && x.storage == y.storage && x.info == y.info;
    }
    check(same, "set parser matches parse_texture_pack on TEXTURES.BIN");
}

void test_pack_set_malformed() {
    const BunLayout bun = build_bun(bun_a(), bun_b(), bun_c());
    {
        // Truncation at every byte is reported and never crashes, except
        // at the end of a top-level chunk after the first root, which
        // leaves a shorter valid file.
        std::size_t valid_cuts = 0;
        for (std::size_t cut = 0; cut < bun.bytes.size(); ++cut) {
            const Bytes bytes(bun.bytes.begin(), bun.bytes.begin() + static_cast<std::ptrdiff_t>(cut));
            const bool ok = parse_set(bytes).ok();
            valid_cuts += ok ? 1U : 0U;
            if (ok != (bun.top_ends.count(cut) != 0 && cut > bun.roots[0])) {
                check(false, "truncated BUN at " + std::to_string(cut));
            }
        }
        check(valid_cuts == 4U, "exactly the four top-level boundaries after a root are valid");
    }
    {
        // The depth-1 container claims one byte more than its parent holds:
        // it is reported and skipped, the rest of the top level is not
        // reachable either, but the earlier root is still parsed.
        Bytes bytes = bun.bytes;
        put_u32(bytes, bun.middle_container + 4,
                static_cast<std::uint32_t>(bytes.size() - bun.middle_container - 8 + 1));
        const nfsu::TexturePackSet set = parse_set(bytes);
        check(!set.complete && set.packs.size() == 1U && set.packs[0].root_offset == bun.roots[0] &&
                  set_has_issue(set, "top level: chunk 0x80034602 at offset " +
                                         std::to_string(bun.middle_container)),
              "overrunning container reported, earlier root kept");
    }
    {
        // A child overrunning its container: that container's rest is
        // skipped, the parent's later siblings are still walked.
        Bytes bytes = bun.bytes;
        const std::size_t child = bun.middle_container + 8;  // 0x00034603
        put_u32(bytes, child + 4, 0x7FFFFFF0U);
        const nfsu::TexturePackSet set = parse_set(bytes);
        check(!set.complete && set.packs.size() == 2U &&
                  set.packs[1].root_offset == bun.roots[2] &&
                  set_has_issue(set, "chunk 0x00034603 at offset " + std::to_string(child) +
                                         " declares 2147483632 payload bytes"),
              "sibling after a broken container is still searched");
    }
    {
        nfsu::TextureLimits limits;
        limits.max_depth = 1;
        const nfsu::TexturePackSet set = parse_set(bun.bytes, limits);
        check(!set.complete && set.packs.size() == 2U &&
                  set_has_issue(set, "chunk 0x80034700 at offset") &&
                  set_has_issue(set, "is nested deeper than the limit 1; not searched"),
              "depth limit reported");
    }
    {
        nfsu::TextureLimits limits;
        limits.max_walk_chunks = 3;
        const nfsu::TexturePackSet set = parse_set(bun.bytes, limits);
        check(!set.complete && set_has_issue(set, "root search chunk limit 3 exceeded"),
              "walk chunk limit");
    }
    {
        nfsu::TextureLimits limits;
        limits.max_packs = 2;
        const nfsu::TexturePackSet set = parse_set(bun.bytes, limits);
        check(!set.complete && set.packs.size() == 2U &&
                  set_has_issue(set, "texture pack limit 2 exceeded"),
              "pack limit");
    }
    {
        nfsu::TextureLimits limits;
        limits.max_set_textures = 3;
        const nfsu::TexturePackSet set = parse_set(bun.bytes, limits);
        check(!set.complete && set.packs.size() == 2U &&
                  set_has_issue(set, "texture packs hold 4 records after the root at offset " +
                                         std::to_string(bun.roots[1]) + "; limit is 3"),
              "record limit over the set");
    }
    {
        // Deep nesting does not recurse: 10000 nested containers.
        Writer w;
        for (int i = 0; i < 10000; ++i) {
            w.begin(0x80000001U);
        }
        w.leaf(0x00000001U, Bytes(4, 0));
        for (int i = 0; i < 10000; ++i) {
            w.end();
        }
        nfsu::TextureLimits limits;
        limits.max_depth = 20000;
        const nfsu::TexturePackSet set = parse_set(w.bytes(), limits);
        check(set.complete && set.packs.empty() &&
                  set_has_issue(set, "no 0xB3300000 texture pack root found"),
              "deep nesting walked iteratively");
    }
    {
        // A root inside a root is not a separate pack.
        Writer w;
        w.begin(nfsu::kTexturePackRootId);
        PackLayout inner;
        write_pack(w, bun_b(), PackOptions{}, inner);
        w.end();
        const nfsu::TexturePackSet set = parse_set(w.bytes());
        check(set.packs.size() == 1U && set.packs[0].root_offset == 0U,
              "no descent into a root");
    }
    {
        const nfsu::TexturePack none =
            nfsu::parse_texture_pack_at(bun.bytes.data(), bun.bytes.size(), bun.bytes.size() - 4);
        check(has_issue(none, "leaves no room for a chunk header"), "root offset past the end");
        const nfsu::TexturePack wrong =
            nfsu::parse_texture_pack_at(bun.bytes.data(), bun.bytes.size(), 0);
        check(has_issue(wrong, "chunk 0x00135200 at offset 0 is not a 0xB3300000"),
              "root offset at another chunk");
        Bytes bytes(bun.bytes.begin(), bun.bytes.begin() + static_cast<std::ptrdiff_t>(bun.roots[0] + 64));
        const nfsu::TexturePack cut =
            nfsu::parse_texture_pack_at(bytes.data(), bytes.size(), bun.roots[0]);
        check(has_issue(cut, "remain in the file") && !cut.tables_parsed, "root overruns the file");
        check(has_issue(nfsu::parse_texture_pack_at(nullptr, 8, 0), "null input"), "null root input");
    }
    {
        const nfsu::TexturePackSet set = nfsu::parse_texture_pack_set(nullptr, 8);
        check(set_has_issue(set, "null input") && !set.complete, "null set input");
    }
}

// A raw pack as the only content of a file.
nfsu::TexturePack parse_raw(const std::vector<TexSpec>& specs, PackLayout* out = nullptr) {
    PackOptions options;
    options.raw = true;
    const PackLayout layout = build_pack(specs, options);
    if (out != nullptr) {
        *out = layout;
    }
    return nfsu::parse_texture_pack_at(layout.bytes.data(), layout.bytes.size(), 0);
}

void test_raw_records() {
    {
        PackLayout layout;
        parse_raw({dxt1_spec("A"), dxt1_spec("FAR")}, &layout);
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[1] + 48, 0xFFFFFF80U);
        const nfsu::TexturePack pack =
            nfsu::parse_texture_pack_at(bytes.data(), bytes.size(), 0);
        check(pack.textures.size() == 2U && pack.textures[0].valid && !pack.textures[1].valid &&
                  has_issue(pack, "raw data at virtual offset 4294967168 with 8 bytes is outside"),
              "virtual offset near 2^32 rejected");
    }
    {
        TexSpec odd = dxt1_spec("ODD");
        odd.virtual_offset = 136;
        const nfsu::TexturePack pack = parse_raw({dxt1_spec("A"), odd});
        check(pack.textures.size() == 2U && !pack.textures[1].valid &&
                  has_issue(pack, "virtual offset 136 at info +48 is not a multiple of 128"),
              "unaligned virtual offset rejected");
    }
    {
        // Ends one byte past the payload: the builder's span ends at 136.
        PackLayout layout;
        TexSpec tail = dxt1_spec("TAIL");
        tail.virtual_offset = 128;
        parse_raw({dxt1_spec("A"), tail}, &layout);
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[1] + 48, 256);
        const nfsu::TexturePack pack =
            nfsu::parse_texture_pack_at(bytes.data(), bytes.size(), 0);
        check(pack.textures.size() == 2U && pack.textures[0].valid && !pack.textures[1].valid &&
                  has_issue(pack, "raw data at virtual offset 256 with 8 bytes is outside the "
                                  "texture data range"),
              "range past the payload end rejected");
    }
    {
        // B starts inside A; C touches A's end and stays valid.
        TexSpec a = argb_spec();
        TexSpec b = dxt1_spec("B");
        b.virtual_offset = 0;
        TexSpec c = dxt1_spec("C");
        c.virtual_offset = 128;
        const nfsu::TexturePack pack = parse_raw({a, b, c});
        check(pack.textures.size() == 3U && !pack.textures[0].valid && !pack.textures[1].valid &&
                  pack.textures[2].valid &&
                  pack.textures[0].problem.find("raw data at file offset") == 0U &&
                  pack.textures[0].problem.find("overlaps the data of 1 textures: 1") !=
                      std::string::npos,
              "raw overlap invalidates both participants");
        bool offset_ok = false;
        for (const nfsu::TextureIssue& issue : pack.issues) {
            offset_ok = offset_ok || (issue.message.find("overlaps") != std::string::npos &&
                                      issue.offset == pack.textures[0].info_offset + 48);
        }
        check(offset_ok, "raw overlap issue points at info +48");
    }
    {
        // P8 payloads are structurally valid without decoding their palette.
        TexSpec p8 = p8_spec("PALETTED");
        const nfsu::TexturePack pack = parse_raw({dxt1_spec("A"), p8, argb_spec()});
        check(pack.ok() && pack.textures.size() == 3U &&
                  pack.valid_texture_count() == 3U && pack.textures[1].valid &&
                  pack.textures[1].format == nfsu::PixelFormat::p8,
              "raw P8 payload accepted");
    }
    {
        PackOptions options;
        options.raw = true;
        const PackLayout layout = build_pack({p8_spec("RAW_P8_RANGE")}, options);
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[0] + 52, 0xFFFFFF00U);
        const nfsu::TexturePack pack =
            nfsu::parse_texture_pack_at(bytes.data(), bytes.size(), 0);
        check(pack.textures.size() == 1U && !pack.textures[0].valid &&
                  has_issue(pack, "raw P8 palette at virtual offset"),
              "raw P8 palette range checked");
    }
    {
        PackOptions options;
        options.raw = true;
        const PackLayout layout =
            build_pack({p8_spec("RAW_P8_A"), p8_spec("RAW_P8_B")}, options);
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[1] + 52, 0);
        const nfsu::TexturePack pack =
            nfsu::parse_texture_pack_at(bytes.data(), bytes.size(), 0);
        check(pack.textures.size() == 2U && pack.valid_texture_count() == 0U &&
                  pack.issues.size() == 2U &&
                  pack.issues[0].offset == layout.info_offsets[0] + 52U &&
                  pack.issues[1].offset == layout.info_offsets[1] + 52U &&
                  has_issue(pack, "raw index data or P8 palette overlaps the resources of 1 textures"),
              "shared raw P8 palette invalidates both owners");
    }
    {
        TexSpec large = argb_spec();
        large.name = "LARGE_RAW";
        large.width = 32;
        large.height = 32;
        large.mips = 1;
        large.pixels.assign(32U * 32U * 4U, 0x55U);
        PackOptions options;
        options.raw = true;
        const PackLayout layout = build_pack({p8_spec("RAW_P8"), large}, options);
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[0] + 52, 1152);
        const nfsu::TexturePack pack =
            nfsu::parse_texture_pack_at(bytes.data(), bytes.size(), 0);
        check(pack.textures.size() == 2U && pack.valid_texture_count() == 0U &&
                  pack.issues.size() == 2U &&
                  pack.issues[0].offset == layout.info_offsets[0] + 52U &&
                  pack.issues[1].offset == layout.info_offsets[1] + 48U &&
                  has_issue(pack, "raw index data or P8 palette overlaps the resources of 1 textures"),
              "raw P8 palette overlap invalidates another index chain");
    }
    {
        // GLOBALB stores a raw P8 palette at data_start + info +52, before
        // the independently addressed index chain at data_start + info +48.
        TexSpec p8 = p8_spec("RAW_P8");
        PackOptions options;
        options.raw = true;
        options.raw_base = static_cast<std::uint32_t>(nfsu::kP8PaletteSize);
        const PackLayout layout = build_pack({p8}, options);
        Bytes bytes = layout.bytes;
        put_u32(bytes, layout.info_offsets[0] + 52, 0);
        const std::size_t data_start =
            layout.blob_offsets[0] - nfsu::kP8PaletteSize;
        for (std::size_t i = 0; i < nfsu::kP8PaletteSize; ++i) {
            bytes[data_start + i] = static_cast<unsigned char>(i & 0xFFU);
        }
        const nfsu::TexturePack pack =
            nfsu::parse_texture_pack_at(bytes.data(), bytes.size(), 0);
        Bytes payload;
        nfsu::P8Palette palette{};
        std::size_t unused = 99;
        std::string error;
        check(pack.ok() && pack.textures.size() == 1U &&
                  nfsu::texture_payload(bytes.data(), bytes.size(), pack.textures[0],
                                        payload, unused, error, &palette) &&
                  payload == p8.pixels && unused == 0U && palette[0] == 0U &&
                  palette[1] == 1U && palette[1023] == 255U,
              "raw P8 payload and palette offsets: " + error);
    }
    {
        TexSpec p8 = p8_spec("P8_BAD_PADDING");
        Bytes stored = p8.pixels;
        stored.resize(128, 0);
        stored[p8.pixels.size()] = 1;
        stored.insert(stored.end(), nfsu::kP8PaletteSize, 0x5AU);
        p8.blob = jdlz_literals(stored);
        const PackLayout layout = build_pack({p8});
        const nfsu::TexturePack pack = parse(layout.bytes);
        Bytes payload;
        nfsu::P8Palette palette;
        palette.fill(0xFFU);
        std::size_t unused = 0;
        std::string error;
        check(pack.ok() && pack.textures.size() == 1U &&
                  !nfsu::texture_payload(layout.bytes.data(), layout.bytes.size(),
                                         pack.textures[0], payload, unused, error,
                                         &palette) &&
                  payload.empty() &&
                  error == "P8 JDLZ padding before the palette is not zero" &&
                  std::all_of(palette.begin(), palette.end(),
                              [](unsigned char value) { return value == 0; }),
              "non-zero P8 palette padding rejected");
    }
    {
        TexSpec wrong = dxt1_spec("WRONG");
        wrong.table_hash = 0x0BADF00DU;
        const nfsu::TexturePack pack = parse_raw({dxt1_spec("A"), wrong});
        check(pack.textures.size() == 2U && !pack.textures[1].valid &&
                  has_issue(pack, "differs from hash table 0x0BADF00D") &&
                  !has_issue(pack, "blob table"),
              "raw hash table mismatch");
        check(nfsu::record_claims_hash(pack.textures[1], 0x0BADF00DU) &&
                  nfsu::record_claims_hash(pack.textures[1], nfsu::bin_hash("WRONG")),
              "an invalid record claims its info and table hashes");
    }
    {
        // A record rejected for its name still reports its hash.
        TexSpec unnamed = dxt1_spec("X");
        unnamed.name = std::string(24, 'N');
        unnamed.hash = 0x12345678U;
        const nfsu::TexturePack pack = parse_raw({unnamed});
        check(pack.textures.size() == 1U && !pack.textures[0].valid &&
                  pack.textures[0].hash == 0x12345678U &&
                  nfsu::record_claims_hash(pack.textures[0], 0x12345678U),
              "hash read before the name check");
    }
    {
        TexSpec first = dxt1_spec("DUP");
        TexSpec second = dxt1_spec("DUP");
        second.virtual_offset = 128;
        const nfsu::TexturePack pack = parse_raw({first, second});
        check(pack.valid_texture_count() == 2U &&
                  has_issue(pack, "texture 1 repeats hash " ),
              "duplicate hash inside a raw pack");
    }
    {
        // Duplicates across packs of one file are set issues, in order.
        TexSpec dup = dxt1_spec("GA1");
        const BunLayout bun = build_bun(bun_a(), {dup}, {dxt1_spec("GA1")});
        const nfsu::TexturePackSet set = parse_set(bun.bytes);
        std::ostringstream first;
        first << "pack 1 texture 0 repeats hash 0x" << std::uppercase << std::hex;
        first.width(8);
        first.fill('0');
        first << nfsu::bin_hash("GA1") << " of pack 0 texture 0";
        check(set.packs.size() == 3U && set.issues.size() == 2U &&
                  set.issues[0].message.find(first.str()) == 0U &&
                  set.issues[1].message.find("pack 2 texture 0 repeats hash") == 0U &&
                  set.issues[1].message.find("of pack 1 texture 0") != std::string::npos &&
                  set.complete && !set.ok(),
              "cross-pack duplicates reported in order");
    }
    {
        // texture_payload refuses a raw record whose size disagrees.
        const nfsu::TexturePack pack = parse_raw({dxt1_spec("A")});
        PackLayout layout;
        parse_raw({dxt1_spec("A")}, &layout);
        nfsu::TextureRecord record = pack.textures[0];
        record.blob_size = 4;
        Bytes payload;
        std::size_t unused = 0;
        std::string error;
        check(!nfsu::texture_payload(layout.bytes.data(), layout.bytes.size(), record, payload,
                                     unused, error) &&
                  error.find("raw data size 4 differs") != std::string::npos && payload.empty(),
              "raw payload size mismatch refused");
    }
}

Bytes read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void test_export_safety() {
    const fs::path root = fs::temp_directory_path() / "nfsu_texture_export_test";
    std::error_code error;
    fs::remove_all(root, error);
    const fs::path input = root / "TEXTURES.BIN";

    TexSpec broken = dxt1_spec("BROKEN");
    // Valid metadata and header, but the stream reaches before the output.
    broken.blob = finish_jdlz(8, {0x01, 0x01, 0x00, 0x05});
    TexSpec unsupported = dxt1_spec("DXT5");
    unsupported.format_code = 0x26;
    const PackLayout good = build_pack({dxt1_spec("A"), dxt3_spec(), argb_spec(), unsupported});
    const PackLayout bad = build_pack({dxt1_spec("A"), dxt3_spec(), broken});
    fs::create_directories(root, error);
    {
        std::ofstream stream(input, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(good.bytes.data()),
                     static_cast<std::streamsize>(good.bytes.size()));
    }

    const nfsu::TexturePack pack = parse(good.bytes);
    nfsu::TextureExportResult first;
    check(nfsu::export_textures(good.bytes, pack, input, root / "out1", nfsu::ExportFormat::tga,
                                first) &&
              first.files.size() == 3U && first.skipped == std::vector<std::size_t>{3},
          "export succeeds and skips unsupported: " + first.error);
    nfsu::TextureExportResult second;
    check(nfsu::export_textures(good.bytes, pack, input, root / "out2", nfsu::ExportFormat::tga,
                                second) &&
              second.files.size() == 3U,
          "second export");
    for (std::size_t i = 0; i < first.files.size() && i < second.files.size(); ++i) {
        check(first.files[i].path.filename() == second.files[i].path.filename() &&
                  read_file(first.files[i].path) == read_file(second.files[i].path),
              "export is deterministic");
    }
    if (!first.files.empty()) {
        std::ostringstream name;
        name << "t0000_" << std::uppercase << std::hex;
        name.width(8);
        name.fill('0');
        name << nfsu::bin_hash("A");
        check(first.files[0].path.filename() == fs::path(name.str() + "_A.tga"),
              "export file name");
        check(read_file(first.files[0].path).size() == 18U + 4U * 4U * 4U, "tga file size");
    }

    // Existing target: refuse before writing anything and leave it intact.
    const Bytes marker = {'k', 'e', 'e', 'p'};
    const fs::path existing = root / "out3" / (nfsu::texture_file_stem(pack.textures[1]) + ".tga");
    fs::create_directories(root / "out3", error);
    {
        std::ofstream stream(existing, std::ios::binary);
        stream.write("keep", 4);
    }
    nfsu::TextureExportResult refused;
    check(!nfsu::export_textures(good.bytes, pack, input, root / "out3", nfsu::ExportFormat::tga,
                                 refused) &&
              refused.error.find("refusing to overwrite") != std::string::npos,
          "existing target refused");
    std::size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(root / "out3")) {
        (void)entry;
        ++entries;
    }
    check(entries == 1U && read_file(existing) == marker, "nothing written, target kept");

    // Output directory equal to the input file.
    nfsu::TextureExportResult same;
    check(!nfsu::export_textures(good.bytes, pack, input, input, nfsu::ExportFormat::tga, same) &&
              read_file(input) == good.bytes,
          "input used as output refused");

    // A failure after two files removes them and leaves no partial file.
    const nfsu::TexturePack bad_pack = parse(bad.bytes);
    check(bad_pack.valid_texture_count() == 3U, "broken stream is only found at export");
    nfsu::TextureExportResult failed;
    check(!nfsu::export_textures(bad.bytes, bad_pack, input, root / "out4",
                                 nfsu::ExportFormat::dds, failed) &&
              failed.error.find("removed all 2 files created by this export") !=
                  std::string::npos &&
              failed.files.empty() && failed.not_removed.empty(),
          "failed export reports cleanup: " + failed.error);
    std::size_t leftover = 0;
    for (const auto& entry : fs::directory_iterator(root / "out4")) {
        (void)entry;
        ++leftover;
    }
    check(leftover == 0U, "failed export leaves no files");

    // A failed export into a directory with an unrelated file keeps it.
    fs::create_directories(root / "out6", error);
    const fs::path unrelated = root / "out6" / "unrelated.txt";
    {
        std::ofstream stream(unrelated, std::ios::binary);
        stream.write("keep", 4);
    }
    nfsu::TextureExportResult failed_again;
    check(!nfsu::export_textures(bad.bytes, bad_pack, input, root / "out6",
                                 nfsu::ExportFormat::tga, failed_again) &&
              read_file(unrelated) == marker,
          "rollback never touches pre-existing files");

    // Rollback attempts every path and reports the ones it could not
    // remove. A non-empty directory stands in for an undeletable file.
    {
        const fs::path dir = root / "rollback";
        fs::create_directories(dir / "stuck", error);
        const fs::path first_file = dir / "a.tga";
        const fs::path last_file = dir / "c.tga";
        for (const fs::path& path : {first_file, last_file, dir / "stuck" / "x"}) {
            std::ofstream stream(path, std::ios::binary);
            stream.write("x", 1);
        }
        std::string summary;
        const std::vector<fs::path> failures = nfsu::rollback_created_files(
            {first_file, dir / "stuck", last_file, dir / "already_gone.tga"}, summary);
        check(failures == std::vector<fs::path>{dir / "stuck"},
              "only the undeletable path is reported");
        check(!fs::exists(first_file) && !fs::exists(last_file),
              "removal continues after a failure");
        check(summary.find("removed 3 of 4 files created by this export; could not remove: ") ==
                      0U &&
                  summary.find("stuck") != std::string::npos &&
                  summary.find("removed all") == std::string::npos,
              "summary is honest: " + summary);
        std::string empty_summary;
        check(nfsu::rollback_created_files({}, empty_summary).empty() &&
                  empty_summary == "removed all 0 files created by this export",
              "empty rollback");
    }

    nfsu::TextureExportResult dds;
    check(nfsu::export_textures(good.bytes, pack, input, root / "out5", nfsu::ExportFormat::dds,
                                dds) &&
              dds.files.size() == 3U &&
              read_file(dds.files[2].path).size() == 128U + 20U,
          "dds export keeps the whole chain");

    // File loading.
    std::vector<unsigned char> bytes;
    const nfsu::TexturePack loaded = nfsu::load_texture_pack(input, bytes);
    check(loaded.valid_texture_count() == 3U && bytes == good.bytes &&
              loaded.parse_source == bytes.data(),
          "load from file");
    std::vector<unsigned char> set_bytes;
    const nfsu::TexturePackSet loaded_set = nfsu::load_texture_pack_set(input, set_bytes);
    check(loaded_set.complete && loaded_set.packs.size() == 1U &&
              loaded_set.packs[0].valid_texture_count() == 3U &&
              loaded_set.packs[0].parse_source == set_bytes.data(),
          "load set from file");
    nfsu::TextureLimits limits;
    limits.max_file_size = 16;
    const nfsu::TexturePack too_big = nfsu::load_texture_pack(input, bytes, limits);
    check(has_issue(too_big, "limit is 16") && too_big.parse_source == nullptr &&
              bytes.empty(),
          "file size limit");
    check(set_has_issue(nfsu::load_texture_pack_set(input, set_bytes, limits), "limit is 16") &&
              set_bytes.empty(),
          "set file size limit is checked before reading");
    check(read_file(input) == good.bytes, "input unchanged");

    fs::remove_all(root, error);
    check(!nfsu::load_texture_pack(input, bytes).ok(), "missing file reported");
}

} // namespace

int main() {
    test_hash();
    test_valid_pack();
    test_alpha_state();
    test_alignment_sweep();
    test_truncation_and_overflow();
    test_nested_and_chained_overlap();
    test_malformed_records();
    test_unsupported_formats();
    test_duplicates_and_naming();
    test_jdlz();
    test_block_decoding();
    test_writers();
    test_export_safety();
    test_pack_set();
    test_pack_set_malformed();
    test_raw_records();

    if (g_failures != 0) {
        std::cerr << g_failures << " texture test failures\n";
        return 1;
    }
    std::cout << "texture tests passed\n";
    return 0;
}
