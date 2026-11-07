#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace nfsu {

// Chunk IDs used by NFSU texture packs (TEXTURES.BIN). See docs/FORMATS.md.
constexpr std::uint32_t kTexturePackRootId = 0xB3300000U;
constexpr std::uint32_t kTextureInfoContainerId = 0xB3310000U;
constexpr std::uint32_t kTexturePackHeaderId = 0x33310001U;
constexpr std::uint32_t kTextureHashTableId = 0x33310002U;
constexpr std::uint32_t kTextureBlobTableId = 0x33310003U;
constexpr std::uint32_t kTextureInfoTableId = 0x33310004U;
constexpr std::uint32_t kTextureFormatTableId = 0x33310005U;
constexpr std::uint32_t kTextureDataContainerId = 0xB3320000U;
constexpr std::uint32_t kTextureDataHeaderId = 0x33320001U;
constexpr std::uint32_t kTextureDataId = 0x33320002U;

constexpr std::size_t kTexturePackHeaderSize = 124;
constexpr std::size_t kTextureHashRecordSize = 8;
constexpr std::size_t kTextureBlobRecordSize = 20;
constexpr std::size_t kTextureInfoRecordSize = 124;
constexpr std::size_t kTextureFormatRecordSize = 32;
constexpr std::size_t kTextureNameOffset = 12;
constexpr std::size_t kTextureNameSize = 24;
constexpr std::size_t kP8PaletteSize = 256U * 4U;
using P8Palette = std::array<unsigned char, kP8PaletteSize>;

// Compressed blobs start at the first absolute file offset inside the
// 0x33320002 payload that is a multiple of this; the bytes before it are 0x11.
constexpr std::uint64_t kTextureDataAlignment = 128;
constexpr std::size_t kJdlzHeaderSize = 16;

// NFS string hash: h = 0xFFFFFFFF, then h = h * 33 + byte (mod 2^32).
std::uint32_t bin_hash(const std::string& text) noexcept;

enum class PixelFormat { unsupported, dxt1, dxt3, a8r8g8b8, p8 };

// Runtime blend state selected by texture-info byte +86. The retail draw
// path treats every non-zero value except 2 as source-over blending.
enum class TextureAlphaBlend { disabled, source_over, additive };

// How a record's mip chain is stored in the file. Standalone TEXTURES.BIN
// packs have a 0x33310003 blob table and JDLZ blobs. Packs embedded in a BUN
// (observed: GLOBAL/GLOBALB.BUN) have no blob table; their chains are
// stored uncompressed and located through info +48. See docs/FORMATS.md.
enum class TextureStorage { jdlz, raw };

const char* texture_storage_name(TextureStorage storage) noexcept;

const char* pixel_format_name(PixelFormat format) noexcept;

// Byte size of one level. DXT1 is 8 bytes and DXT3 16 bytes per 4x4 block,
// max(1, ceil(w / 4)) * max(1, ceil(h / 4)) blocks; A8R8G8B8 is 4 bytes per
// pixel and P8 is one palette index per pixel. Returns false for unsupported
// formats, zero dimensions or overflow.
bool level_size(
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t& out) noexcept;

// Sum of level_size over mip_count levels, each level halving width and
// height (minimum 1).
bool mip_chain_size(
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_count,
    std::uint64_t& out) noexcept;

struct TextureRecord {
    std::size_t index = 0;
    // Absolute file offsets of the three per-texture records.
    std::uint64_t info_offset = 0;
    std::uint64_t format_offset = 0;
    std::uint64_t blob_entry_offset = 0;
    // Raw records, kept so unknown fields are preserved.
    std::array<unsigned char, kTextureInfoRecordSize> info{};
    std::array<unsigned char, kTextureFormatRecordSize> format_record{};
    std::array<unsigned char, kTextureBlobRecordSize> blob_entry{};

    std::string name;
    // True when the name uses all 23 characters of its field; such names may
    // be truncated, so the name hash may not match.
    bool name_field_full = false;
    std::uint32_t hash = 0;            // info +36
    std::uint32_t class_hash = 0;      // info +40
    std::uint32_t virtual_offset = 0;  // info +48
    // info +52. TEXTURES.BIN: align_up(+48 + +56, 128). Embedded GLOBALB
    // packs: a palette offset, not an end (see docs/FORMATS.md).
    std::uint32_t virtual_end = 0;
    std::uint32_t data_size = 0;       // info +56, all mip levels
    std::uint32_t palette_size = 0;    // info +60, 1024 for observed P8
    std::uint32_t top_level_size = 0;  // info +64
    std::uint16_t width = 0;           // info +68
    std::uint16_t height = 0;          // info +70
    std::uint8_t width_log2 = 0;       // info +72
    std::uint8_t height_log2 = 0;      // info +73
    std::uint8_t format_code = 0;      // info +74
    std::uint8_t mip_count = 0;        // info +78
    // The retail draw path turns these two bytes into the row of the draw
    // queue bucket a material is appended to: +81 counts as a signed byte
    // and +84, when nonzero, adds 8. No name is claimed for either byte;
    // texture_draw_layer holds the decoded rule.
    std::uint8_t layer_order = 0;      // info +81
    std::uint8_t layer_group = 0;      // info +84
    // The game enables alpha test only when +85 == 1, using GREATER and
    // reference 0xAA. Other observed values leave it disabled.
    std::uint8_t alpha_test_mode = 0;  // info +85
    // 0 disables blending, 2 selects SRCALPHA/ONE, and every other non-zero
    // value selects SRCALPHA/INVSRCALPHA.
    std::uint8_t alpha_blend_mode = 0; // info +86
    std::uint32_t d3d_format = 0;      // format record +20
    PixelFormat format = PixelFormat::unsupported;

    TextureStorage storage = TextureStorage::jdlz;
    // jdlz: blob entry +4 (absolute) and +8. raw: absolute file offset of
    // the uncompressed chain (pack data_start + virtual_offset) and
    // data_size; the blob entry is then all zero.
    std::uint64_t blob_offset = 0;
    std::uint32_t blob_size = 0;
    std::uint32_t jdlz_decompressed_size = 0;

    bool valid = false;
    std::string problem;

    // Hash table entry i, +0. The info, hash table and blob table hashes
    // are all read before any check, so an invalid record still names every
    // hash it claims (see record_claims_hash).
    std::uint32_t table_hash = 0;
};

// The draw queue row: (layer_group != 0 ? 8 : 0) + (signed) layer_order,
// clamped to 0..kTextureDrawLayerCount - 1. The retail code does not clamp;
// every record read from retail data stays inside the range on its own.
constexpr int kTextureDrawLayerCount = 16;

int texture_draw_layer(const TextureRecord& texture) noexcept;

bool texture_alpha_test_enabled(const TextureRecord& texture) noexcept;

TextureAlphaBlend texture_alpha_blend(const TextureRecord& texture) noexcept;

const char* texture_alpha_blend_name(TextureAlphaBlend blend) noexcept;

// True if any hash field of the record (info +36, hash table +0, or blob
// table +0 for JDLZ storage) equals hash, valid or not.
bool record_claims_hash(const TextureRecord& texture, std::uint32_t hash) noexcept;

struct TextureAnimation {
    std::uint32_t hash=0, fps=0, clock=0;
    std::vector<std::uint32_t> frames;
};
// Original B0300100, version1: 52-byte definitions and16-byte frame refs.
bool append_texture_animations(const unsigned char*,std::size_t,
    std::vector<TextureAnimation>&,std::string& error);
// Scan an uncompressed chunk file for animation metadata only. Texture/mesh
// payloads are skipped on disk, so common INGAMEB definitions do not add a
// whole-source buffer to the bounded scene loader's allocation peak.
bool load_texture_animations(const std::filesystem::path&,std::vector<TextureAnimation>&,
                             std::string& error);
std::size_t texture_animation_phase(const TextureAnimation&,std::uint32_t ticks_60hz) noexcept;

struct TextureIssue {
    std::uint64_t offset = 0;
    std::string message;
};

struct TextureLimits {
    std::uint64_t max_file_size = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_chunks = 4096;
    std::size_t max_textures = 4096;
    std::uint32_t max_dimension = 4096;
    std::uint32_t max_decompressed_size = 64U * 1024U * 1024U;
    // parse_texture_pack_set only: container nesting depth below the top
    // level, chunk headers read by the root search (max_chunks still
    // applies inside each pack), roots, and records over all packs.
    std::size_t max_depth = 32;
    std::size_t max_walk_chunks = 1U << 20;
    std::size_t max_packs = 256;
    std::size_t max_set_textures = 16384;
};

struct TexturePack {
    std::uint64_t file_size = 0;
    // Non-owning identity of the buffer the pack was parsed from: the data
    // pointer given to parse_texture_pack or parse_texture_pack_at (and so
    // by the set and load entry points), even if parsing then failed. It is
    // compared, never dereferenced, and it identifies that buffer only while
    // the caller keeps it alive and unmodified in size and address. Null if
    // no parser ran (for example a file that could not be read).
    const unsigned char* parse_source = nullptr;
    // Absolute offset of the 0xB3300000 chunk header.
    std::uint64_t root_offset = 0;
    TextureStorage storage = TextureStorage::jdlz;
    std::uint64_t header_offset = 0;
    std::uint32_t header_version = 0;
    std::string pack_name;
    std::string pack_path;
    std::uint32_t pack_hash = 0;
    std::uint64_t data_payload_offset = 0;
    std::uint64_t data_payload_size = 0;
    std::uint64_t data_prefix_bytes = 0;
    // data_payload_offset + data_prefix_bytes: the first 128-byte aligned
    // absolute offset of the 0x33320002 payload.
    std::uint64_t data_start = 0;
    // Raw storage: max(info +48 + info +56) over all records, valid or not.
    // Observed equal to data_payload_size - data_prefix_bytes in both
    // GLOBALB packs, a cross-check that the base mapping is right. Not
    // enforced.
    std::uint64_t raw_extent = 0;
    // True once the record tables were read: textures then lists every
    // record of the pack. False means the pack's hashes are unknown.
    bool tables_parsed = false;
    std::vector<TextureRecord> textures;
    std::vector<TextureIssue> issues;

    bool ok() const noexcept { return issues.empty(); }
    std::size_t valid_texture_count() const noexcept;
};

// Parses an in-memory TEXTURES.BIN image. Never throws for malformed input.
// A texture whose own records are inconsistent is kept with valid == false
// and a problem message; table-level errors are reported in issues. The
// image must hold exactly one top-level 0xB3300000 root with a 0x33310003
// blob table (JDLZ storage).
TexturePack parse_texture_pack(
    const unsigned char* data,
    std::size_t size,
    const TextureLimits& limits = TextureLimits{});

// Parses the 0xB3300000 chunk whose header is at root_offset, in place.
// Every offset in the result is absolute in data. With exactly one
// 0x33310003 the pack is parsed as by parse_texture_pack. Without one it is
// parsed as a raw pack:
//   - record i maps to [data_start + info +48, ... + info +56);
//   - info +48 must be a multiple of 128;
//   - the range must lie inside the 0x33320002 payload after its prefix;
//   - ranges of valid records must not share a byte (all participants are
//     invalidated, as for JDLZ blobs).
// Observed in both GLOBALB.BUN packs and verified there by comparing each
// stored mip level 1 with a box filter of level 0; tested on synthetic
// data. That "no 0x33310003" always means raw storage is a hypothesis.
// Raw P8 info +52 must address its in-range palette; raw index and palette
// resources must not overlap. The field is not checked for other raw formats.
// See docs/FORMATS.md.
TexturePack parse_texture_pack_at(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t root_offset,
    const TextureLimits& limits = TextureLimits{});

// Every texture pack root in one file, in ascending root offset order.
struct TexturePackSet {
    std::uint64_t file_size = 0;
    std::vector<TexturePack> packs;
    // Walk problems, a missing root, limits and hashes repeated across
    // packs. Problems inside a pack stay in that pack's issues.
    std::vector<TextureIssue> issues;
    // True when the whole file was searched and every root was parsed up
    // to its record tables, so packs lists every record of the file.
    bool complete = false;

    bool ok() const noexcept;
};

// Finds every 0xB3300000 root in a chunk file (for example a BUN) and
// parses each with parse_texture_pack_at, in ascending offset order. The
// walk is iterative (an explicit stack, no recursion): it descends into
// chunks with bit 31 set, up to limits.max_depth levels below the top
// level, never into a root itself. A child that does not fit its parent is
// reported, the rest of that parent is skipped, and the walk continues
// with the parent's own siblings. No bytes are copied. A file without any
// root is an issue. Parsing stops with an issue once the packs hold more
// than limits.max_set_textures records.
TexturePackSet parse_texture_pack_set(
    const unsigned char* data,
    std::size_t size,
    const TextureLimits& limits = TextureLimits{});

TexturePackSet load_texture_pack_set(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TextureLimits& limits = TextureLimits{});

// Reads the whole file read-only into bytes and parses it.
TexturePack load_texture_pack(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TextureLimits& limits = TextureLimits{});

// Decompresses a JDLZ blob (16-byte header "JDLZ" 02 10 00 00, u32
// decompressed size, u32 compressed size). The output must be filled
// exactly; back-references must stay inside the output. unused_input is the
// number of trailing input bytes left once the output is full.
bool jdlz_decompress(
    const unsigned char* blob,
    std::size_t size,
    std::uint32_t max_output,
    std::vector<unsigned char>& output,
    std::size_t& unused_input,
    std::string& error);

// Returns the full mip chain of a valid texture from the file image:
// JDLZ-decompressed, or copied unchanged for raw storage (unused_input 0).
// P8 JDLZ blobs append zero padding to 128 bytes and a 1024-byte palette;
// the returned payload excludes both, and p8_palette receives the palette
// when requested. Raw P8 palettes are read through info +52 when requested.
bool texture_payload(
    const unsigned char* data,
    std::size_t size,
    const TextureRecord& texture,
    std::vector<unsigned char>& payload,
    std::size_t& unused_input,
    std::string& error,
    P8Palette* p8_palette = nullptr);

// Decodes one level to RGBA8, rows top to bottom as stored. DXT edge blocks
// are cropped. size must equal level_size exactly. DXT endpoints are
// expanded from 5/6 bits by bit replication; interpolated colors use integer
// (2a + b) / 3 and (a + b) / 2 on the expanded values; DXT1 uses 3-color
// mode with transparent black when color0 <= color1. A8R8G8B8 is stored as
// little-endian u32 ARGB, i.e. bytes B, G, R, A, and is reordered only. P8
// uses an explicit 256-entry palette; without one it is refused. Palette
// entries are stored A8R8G8B8 pixels (B, G, R, A); retail copies each dword
// into an A8R8G8B8 texture at 0x00407E70..0x00407EA8.
bool decode_level(
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    const unsigned char* data,
    std::size_t size,
    std::vector<unsigned char>& rgba,
    std::string& error,
    const P8Palette* p8_palette = nullptr);

// Writes a DDS header followed by the payload bytes unchanged. payload must
// be exactly mip_chain_size bytes.
bool write_dds(
    std::ostream& output,
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_count,
    const unsigned char* payload,
    std::size_t size,
    std::string& error);

// Deterministic ASCII file stem "t<index:4>_<HASH:8>_<name>", characters
// outside [A-Za-z0-9_.-] replaced by '_'. Index and hash keep it unique for
// duplicate or truncated names.
std::string texture_file_stem(const TextureRecord& texture);

} // namespace nfsu
