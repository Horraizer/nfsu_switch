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
    Bytes pixels;                       // decompressed chain
    std::optional<std::uint32_t> data_size;
    std::optional<std::uint32_t> top_size;
    std::optional<Bytes> blob;          // replaces the literal JDLZ blob
    std::optional<std::uint32_t> table_hash;
};

struct PackOptions {
    std::size_t leading_pad = 0;        // padding chunk before the root
    bool corrupt_prefix = false;
    std::optional<std::uint32_t> hash_table_size;
    std::optional<std::uint32_t> info_table_size;
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

PackLayout build_pack(const std::vector<TexSpec>& specs, const PackOptions& options = {}) {
    PackLayout layout;
    Writer w;
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

    Bytes info_table;
    std::uint32_t virtual_offset = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const TexSpec& spec = specs[i];
        Bytes record(124, 0);
        std::memcpy(&record[12], spec.name.data(), std::min<std::size_t>(spec.name.size(), 24));
        put_u32(record, 36, hashes[i]);
        put_u32(record, 40, 0x001A93CFU);
        const std::uint32_t size =
            spec.data_size.value_or(static_cast<std::uint32_t>(spec.pixels.size()));
        put_u32(record, 48, virtual_offset);
        virtual_offset = (virtual_offset + size + 127U) & ~127U;
        put_u32(record, 52, virtual_offset);
        put_u32(record, 56, size);
        std::uint64_t top = 0;
        nfsu::PixelFormat format = nfsu::PixelFormat::dxt1;
        if (spec.format_code == 0x24) {
            format = nfsu::PixelFormat::dxt3;
        } else if (spec.format_code == 0x20) {
            format = nfsu::PixelFormat::a8r8g8b8;
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
    w.end(); // data chunk
    w.end(); // data container
    w.end(); // root
    w.leaf(0x00000000U, Bytes(16, 0));
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
    const PackLayout layout = build_pack({dxt1_spec("DXT1_TEX"), dxt3_spec(), argb_spec()});
    const nfsu::TexturePack pack = parse(layout.bytes);
    if (!pack.ok()) {
        dump_issues(pack);
    }
    check(pack.ok() && pack.valid_texture_count() == 3U, "valid pack");
    check(pack.pack_name == "CARTEXTURES" && pack.pack_path == "Test\\Pack.tpk" &&
              pack.pack_hash == nfsu::bin_hash("Test\\Pack.tpk") &&
              pack.header_version == 4U,
          "pack header");
    check(pack.data_payload_offset == layout.data_payload_offset &&
              pack.data_prefix_bytes == layout.data_prefix,
          "data payload location");
    if (pack.textures.size() != 3U) {
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

    for (const nfsu::TextureRecord& texture : pack.textures) {
        Bytes payload;
        std::size_t unused = 99;
        std::string error;
        const bool ok = nfsu::texture_payload(layout.bytes.data(), layout.bytes.size(),
                                              texture, payload, unused, error);
        check(ok && payload.size() == texture.data_size &&
                  unused == texture.index % 3,
              "payload of texture " + std::to_string(texture.index) + " " + error);
    }
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
    std::ostringstream output;
    check(!nfsu::write_dds(output, nfsu::PixelFormat::unsupported, 4, 4, 1, data.data(),
                           data.size(), error) &&
              output.str().empty(),
          "unsupported DDS refused");
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
    check(!nfsu::level_size(nfsu::PixelFormat::dxt1, 0, 4, size), "zero width");
    check(nfsu::mip_chain_size(nfsu::PixelFormat::dxt1, 256, 256, 6, size) && size == 0xAAA0U,
          "retail 256x256 DXT1 6-level size");
    check(nfsu::mip_chain_size(nfsu::PixelFormat::a8r8g8b8, 512, 512, 7, size) &&
              size == 0x155500U,
          "retail 512x512 ARGB 7-level size");
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
    check(loaded.valid_texture_count() == 3U && bytes == good.bytes, "load from file");
    nfsu::TextureLimits limits;
    limits.max_file_size = 16;
    check(has_issue(nfsu::load_texture_pack(input, bytes, limits), "limit is 16"),
          "file size limit");
    check(read_file(input) == good.bytes, "input unchanged");

    fs::remove_all(root, error);
    check(!nfsu::load_texture_pack(input, bytes).ok(), "missing file reported");
}

} // namespace

int main() {
    test_hash();
    test_valid_pack();
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

    if (g_failures != 0) {
        std::cerr << g_failures << " texture test failures\n";
        return 1;
    }
    std::cout << "texture tests passed\n";
    return 0;
}
