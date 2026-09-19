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

// Compressed blobs start at the first absolute file offset inside the
// 0x33320002 payload that is a multiple of this; the bytes before it are 0x11.
constexpr std::uint64_t kTextureDataAlignment = 128;
constexpr std::size_t kJdlzHeaderSize = 16;

// NFS string hash: h = 0xFFFFFFFF, then h = h * 33 + byte (mod 2^32).
std::uint32_t bin_hash(const std::string& text) noexcept;

enum class PixelFormat { unsupported, dxt1, dxt3, a8r8g8b8 };

const char* pixel_format_name(PixelFormat format) noexcept;

// Byte size of one level. DXT1 is 8 bytes and DXT3 16 bytes per 4x4 block,
// max(1, ceil(w / 4)) * max(1, ceil(h / 4)) blocks; A8R8G8B8 is 4 bytes per
// pixel. Returns false for unsupported formats, zero dimensions or overflow.
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
    std::uint32_t hash = 0;
    std::uint32_t class_hash = 0;      // info +40
    std::uint32_t virtual_offset = 0;  // info +48
    std::uint32_t virtual_end = 0;     // info +52
    std::uint32_t data_size = 0;       // info +56, all mip levels
    std::uint32_t top_level_size = 0;  // info +64
    std::uint16_t width = 0;           // info +68
    std::uint16_t height = 0;          // info +70
    std::uint8_t width_log2 = 0;       // info +72
    std::uint8_t height_log2 = 0;      // info +73
    std::uint8_t format_code = 0;      // info +74
    std::uint8_t mip_count = 0;        // info +78
    std::uint32_t d3d_format = 0;      // format record +20
    PixelFormat format = PixelFormat::unsupported;

    std::uint64_t blob_offset = 0;     // blob entry +4, absolute
    std::uint32_t blob_size = 0;       // blob entry +8
    std::uint32_t jdlz_decompressed_size = 0;

    bool valid = false;
    std::string problem;
};

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
};

struct TexturePack {
    std::uint64_t file_size = 0;
    std::uint64_t header_offset = 0;
    std::uint32_t header_version = 0;
    std::string pack_name;
    std::string pack_path;
    std::uint32_t pack_hash = 0;
    std::uint64_t data_payload_offset = 0;
    std::uint64_t data_payload_size = 0;
    std::uint64_t data_prefix_bytes = 0;
    std::vector<TextureRecord> textures;
    std::vector<TextureIssue> issues;

    bool ok() const noexcept { return issues.empty(); }
    std::size_t valid_texture_count() const noexcept;
};

// Parses an in-memory TEXTURES.BIN image. Never throws for malformed input.
// A texture whose own records are inconsistent is kept with valid == false
// and a problem message; table-level errors are reported in issues.
TexturePack parse_texture_pack(
    const unsigned char* data,
    std::size_t size,
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

// Decompresses the full mip chain of a valid texture from the file image.
bool texture_payload(
    const unsigned char* data,
    std::size_t size,
    const TextureRecord& texture,
    std::vector<unsigned char>& payload,
    std::size_t& unused_input,
    std::string& error);

// Decodes one level to RGBA8, rows top to bottom as stored. DXT edge blocks
// are cropped. size must equal level_size exactly. DXT endpoints are
// expanded from 5/6 bits by bit replication; interpolated colors use integer
// (2a + b) / 3 and (a + b) / 2 on the expanded values; DXT1 uses 3-color
// mode with transparent black when color0 <= color1. A8R8G8B8 is stored as
// little-endian u32 ARGB, i.e. bytes B, G, R, A, and is reordered only.
bool decode_level(
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    const unsigned char* data,
    std::size_t size,
    std::vector<unsigned char>& rgba,
    std::string& error);

// Writes an uncompressed 32-bit TGA (image type 2, top-left origin, 8 alpha
// bits). rgba must hold width * height * 4 bytes; dimensions 1..65535.
bool write_tga(
    std::ostream& output,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgba,
    std::string& error);

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

enum class ExportFormat { tga, dds };

struct ExportedTexture {
    std::size_t index = 0;
    std::filesystem::path path;
    std::size_t jdlz_unused_bytes = 0;
};

struct TextureExportResult {
    std::vector<ExportedTexture> files;
    std::vector<std::size_t> skipped;
    std::string error;
};

// Writes one file per valid texture into output_dir (created if missing):
// a TGA of the decoded top level, or a DDS header plus the decompressed
// payload of every level unchanged. Invalid textures are listed in skipped.
// Nothing is written if any target already exists or is the input file.
// Each file is written as "<target>.partial" and renamed when complete; on
// any failure every file written by this call is removed and false is
// returned with error set.
bool export_textures(
    const std::vector<unsigned char>& bytes,
    const TexturePack& pack,
    const std::filesystem::path& input,
    const std::filesystem::path& output_dir,
    ExportFormat format,
    TextureExportResult& result);

} // namespace nfsu
