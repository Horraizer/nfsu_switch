#include "nfsu/texture.h"

#include "nfsu/chunk.h"
#include "nfsu/image.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

constexpr std::uint64_t kHeaderSize = 8;
constexpr unsigned char kAlignmentByte = 0x11U;

// Observed pairs of the info +74 format code and the format record +20
// D3DFORMAT value. 21 is D3DFMT_A8R8G8B8.
constexpr std::uint8_t kFormatCodeA8R8G8B8 = 0x20U;
constexpr std::uint8_t kFormatCodeDxt1 = 0x22U;
constexpr std::uint8_t kFormatCodeDxt3 = 0x24U;
constexpr std::uint8_t kFormatCodeP8Mask = 0x08U;
constexpr std::uint8_t kFormatCodeP8Color = 0x80U;
constexpr std::uint8_t kFormatCodeP8Custom = 0x81U;
constexpr std::uint32_t kD3dFormatA8R8G8B8 = 21U;
constexpr std::uint32_t kD3dFormatP8 = 41U;
constexpr std::uint32_t kFourccDxt1 = 0x31545844U; // "DXT1"
constexpr std::uint32_t kFourccDxt3 = 0x33545844U; // "DXT3"

// JDLZ header bytes 4..7 as observed in every sample blob.
constexpr unsigned char kJdlzMagic[8] = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};

struct RawChunk {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
};

std::uint16_t read_u16_le(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(bytes[0]) |
        (static_cast<unsigned int>(bytes[1]) << 8U));
}

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) {
        return false;
    }
    out = a + b;
    return true;
}

bool align_up(std::uint64_t value, std::uint64_t alignment, std::uint64_t& out) {
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0) {
        out = value;
        return true;
    }
    return checked_add(value, alignment - remainder, out);
}

void put_u32(unsigned char* out, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        out[i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

std::string hex32(std::uint32_t value) {
    std::ostringstream text;
    text << "0x" << std::uppercase << std::hex;
    text.width(8);
    text.fill('0');
    text << value;
    return text.str();
}

std::string chunk_label(const RawChunk& chunk) {
    std::ostringstream text;
    text << "chunk " << format_chunk_id(chunk.id) << " at offset "
         << chunk.offset;
    return text.str();
}

// Reads a NUL-terminated printable ASCII string from a fixed field.
bool read_fixed_string(
    const unsigned char* field,
    std::size_t size,
    std::string& out,
    std::string& error) {
    std::size_t length = 0;
    while (length < size && field[length] != 0) {
        const unsigned char ch = field[length];
        if (ch < 0x20U || ch > 0x7EU) {
            error = "byte " + std::to_string(length) +
                    " is not printable ASCII";
            return false;
        }
        ++length;
    }
    if (length == size) {
        error = "no NUL terminator in the " + std::to_string(size) +
                "-byte field";
        return false;
    }
    out.assign(reinterpret_cast<const char*>(field), length);
    return true;
}

class TextureParser {
public:
    TextureParser(
        const unsigned char* data,
        std::uint64_t size,
        const TextureLimits& limits,
        TexturePack& pack)
        : data_(data), size_(size), limits_(limits), pack_(pack) {}

    // Standalone TEXTURES.BIN: exactly one top-level root, JDLZ storage.
    void run() {
        if (size_ < kHeaderSize) {
            add_issue(0, "file is smaller than a chunk header (8 bytes)");
            return;
        }
        std::vector<RawChunk> top;
        list_children(0, size_, "top level", top);
        RawChunk root;
        if (!find_single(top, kTexturePackRootId, 0, "top level", root)) {
            return;
        }
        parse_root(root, false);
    }

    // The root whose header is at root_offset; the blob table is optional.
    void run_at(std::uint64_t root_offset) {
        pack_.root_offset = root_offset;
        if (root_offset > size_ || size_ - root_offset < kHeaderSize) {
            std::ostringstream text;
            text << "root offset " << root_offset
                 << " leaves no room for a chunk header in " << size_
                 << " bytes";
            add_issue(root_offset, text.str());
            return;
        }
        RawChunk root;
        root.offset = root_offset;
        root.id = read_u32_le(data_ + root_offset);
        root.payload_size = read_u32_le(data_ + root_offset + 4);
        root.payload_offset = root_offset + kHeaderSize;
        if (root.id != kTexturePackRootId) {
            add_issue(root_offset, chunk_label(root) + " is not a " +
                                       format_chunk_id(kTexturePackRootId) +
                                       " texture pack root");
            return;
        }
        if (root.payload_size > size_ - root.payload_offset) {
            std::ostringstream text;
            text << chunk_label(root) << " declares " << root.payload_size
                 << " payload bytes but only "
                 << (size_ - root.payload_offset) << " remain in the file";
            add_issue(root_offset, text.str());
            return;
        }
        ++chunk_count_;
        parse_root(root, true);
    }

private:
    void parse_root(const RawChunk& root, bool allow_raw) {
        pack_.root_offset = root.offset;
        std::vector<RawChunk> sections;
        if (!list_children(root.payload_offset,
                           root.payload_offset + root.payload_size,
                           chunk_label(root), sections)) {
            return;
        }
        RawChunk info;
        RawChunk data;
        if (!find_single(sections, kTextureInfoContainerId, root.offset,
                         chunk_label(root), info) ||
            !find_single(sections, kTextureDataContainerId, root.offset,
                         chunk_label(root), data)) {
            return;
        }

        std::vector<RawChunk> tables;
        std::vector<RawChunk> blobs;
        if (!list_children(info.payload_offset,
                           info.payload_offset + info.payload_size,
                           chunk_label(info), tables) ||
            !list_children(data.payload_offset,
                           data.payload_offset + data.payload_size,
                           chunk_label(data), blobs)) {
            return;
        }

        RawChunk header;
        RawChunk hashes;
        RawChunk blob_table;
        RawChunk info_table;
        RawChunk format_table;
        RawChunk data_header;
        RawChunk data_chunk;
        const std::string info_context = chunk_label(info);
        const std::string data_context = chunk_label(data);
        const bool raw =
            allow_raw && std::none_of(tables.begin(), tables.end(),
                                      [](const RawChunk& chunk) {
                                          return chunk.id == kTextureBlobTableId;
                                      });
        pack_.storage = raw ? TextureStorage::raw : TextureStorage::jdlz;
        if (!find_single(tables, kTexturePackHeaderId, info.offset,
                         info_context, header) ||
            !find_single(tables, kTextureHashTableId, info.offset,
                         info_context, hashes) ||
            (!raw && !find_single(tables, kTextureBlobTableId, info.offset,
                                  info_context, blob_table)) ||
            !find_single(tables, kTextureInfoTableId, info.offset,
                         info_context, info_table) ||
            !find_single(tables, kTextureFormatTableId, info.offset,
                         info_context, format_table) ||
            !find_single(blobs, kTextureDataHeaderId, data.offset,
                         data_context, data_header) ||
            !find_single(blobs, kTextureDataId, data.offset, data_context,
                         data_chunk)) {
            return;
        }

        if (!parse_header(header) || !parse_data_chunk(data_chunk)) {
            return;
        }
        parse_data_header(data_header);
        if (!parse_tables(hashes, raw ? nullptr : &blob_table, info_table,
                          format_table)) {
            return;
        }
        pack_.tables_parsed = true;
        check_duplicate_hashes();
        check_blob_overlap();
        compute_raw_extent();
    }

    void add_issue(std::uint64_t offset, std::string message) {
        pack_.issues.push_back(TextureIssue{offset, std::move(message)});
    }

    bool list_children(
        std::uint64_t begin,
        std::uint64_t end,
        const std::string& context,
        std::vector<RawChunk>& out) {
        if (begin > end || end > size_) {
            add_issue(begin, context + ": invalid chunk range");
            return false;
        }
        std::uint64_t offset = begin;
        while (offset < end) {
            if (end - offset < kHeaderSize) {
                std::ostringstream text;
                text << context << ": " << (end - offset)
                     << " trailing bytes are smaller than a chunk header";
                add_issue(offset, text.str());
                return false;
            }
            if (chunk_count_ >= limits_.max_chunks) {
                std::ostringstream text;
                text << context << ": chunk limit " << limits_.max_chunks
                     << " exceeded";
                add_issue(offset, text.str());
                return false;
            }
            RawChunk chunk;
            chunk.offset = offset;
            chunk.id = read_u32_le(data_ + offset);
            chunk.payload_size = read_u32_le(data_ + offset + 4);
            chunk.payload_offset = offset + kHeaderSize;
            if (chunk.payload_size > end - chunk.payload_offset) {
                std::ostringstream text;
                text << context << ": " << chunk_label(chunk) << " declares "
                     << chunk.payload_size << " payload bytes but only "
                     << (end - chunk.payload_offset)
                     << " remain in its parent";
                add_issue(offset, text.str());
                return false;
            }
            ++chunk_count_;
            out.push_back(chunk);
            offset = chunk.payload_offset + chunk.payload_size;
        }
        return true;
    }

    bool find_single(
        const std::vector<RawChunk>& chunks,
        std::uint32_t id,
        std::uint64_t parent_offset,
        const std::string& context,
        RawChunk& out) {
        std::size_t count = 0;
        for (const RawChunk& chunk : chunks) {
            if (chunk.id == id) {
                if (count == 0) {
                    out = chunk;
                }
                ++count;
            }
        }
        if (count != 1) {
            std::ostringstream text;
            text << context << ": expected exactly one " << format_chunk_id(id)
                 << " chunk, found " << count;
            add_issue(parent_offset, text.str());
            return false;
        }
        return true;
    }

    bool parse_header(const RawChunk& header) {
        if (header.payload_size != kTexturePackHeaderSize) {
            std::ostringstream text;
            text << chunk_label(header) << " payload is " << header.payload_size
                 << " bytes; expected " << kTexturePackHeaderSize;
            add_issue(header.offset, text.str());
            return false;
        }
        const unsigned char* payload = data_ + header.payload_offset;
        std::string error;
        if (!read_fixed_string(payload + 4, 28, pack_.pack_name, error) ||
            !read_fixed_string(payload + 32, 64, pack_.pack_path, error)) {
            add_issue(header.offset,
                      chunk_label(header) + " pack name: " + error);
            return false;
        }
        pack_.header_offset = header.offset;
        pack_.header_version = read_u32_le(payload);
        pack_.pack_hash = read_u32_le(payload + 96);
        return true;
    }

    // 0x33320001: observed as 24 bytes with the pack hash at +12.
    void parse_data_header(const RawChunk& chunk) {
        if (chunk.payload_size != 24) {
            std::ostringstream text;
            text << chunk_label(chunk) << " payload is " << chunk.payload_size
                 << " bytes; expected 24";
            add_issue(chunk.offset, text.str());
            return;
        }
        const std::uint32_t hash = read_u32_le(data_ + chunk.payload_offset + 12);
        if (hash != pack_.pack_hash) {
            add_issue(chunk.offset, chunk_label(chunk) + " pack hash " +
                                        hex32(hash) + " differs from header " +
                                        hex32(pack_.pack_hash));
        }
    }

    // Blobs start at the first 128-byte aligned absolute offset in the
    // payload; every byte before it must be 0x11.
    bool parse_data_chunk(const RawChunk& chunk) {
        std::uint64_t aligned = 0;
        if (!align_up(chunk.payload_offset, kTextureDataAlignment, aligned) ||
            aligned - chunk.payload_offset > chunk.payload_size) {
            add_issue(chunk.offset, chunk_label(chunk) +
                                        " is smaller than its alignment prefix");
            return false;
        }
        for (std::uint64_t at = chunk.payload_offset; at < aligned; ++at) {
            if (data_[at] != kAlignmentByte) {
                std::ostringstream text;
                text << chunk_label(chunk) << " alignment prefix byte at file"
                     << " offset " << at << " is not 0x11";
                add_issue(at, text.str());
                return false;
            }
        }
        pack_.data_payload_offset = chunk.payload_offset;
        pack_.data_payload_size = chunk.payload_size;
        pack_.data_prefix_bytes = aligned - chunk.payload_offset;
        pack_.data_start = aligned;
        return true;
    }

    bool check_table_size(
        const RawChunk& chunk,
        std::uint64_t count,
        std::size_t record_size) {
        std::uint64_t expected = 0;
        if (!checked_mul(count, record_size, expected) ||
            chunk.payload_size != expected) {
            std::ostringstream text;
            text << chunk_label(chunk) << " payload is " << chunk.payload_size
                 << " bytes; " << count << " records of " << record_size
                 << " bytes need " << expected;
            add_issue(chunk.offset, text.str());
            return false;
        }
        return true;
    }

    // blob_table is null for raw storage.
    bool parse_tables(
        const RawChunk& hashes,
        const RawChunk* blob_table,
        const RawChunk& info_table,
        const RawChunk& format_table) {
        if (hashes.payload_size % kTextureHashRecordSize != 0) {
            std::ostringstream text;
            text << chunk_label(hashes) << " payload " << hashes.payload_size
                 << " is not a multiple of " << kTextureHashRecordSize;
            add_issue(hashes.offset, text.str());
            return false;
        }
        const std::uint64_t count = hashes.payload_size / kTextureHashRecordSize;
        if (count > limits_.max_textures) {
            std::ostringstream text;
            text << chunk_label(hashes) << " lists " << count
                 << " textures; limit is " << limits_.max_textures;
            add_issue(hashes.offset, text.str());
            return false;
        }
        if ((blob_table != nullptr &&
             !check_table_size(*blob_table, count, kTextureBlobRecordSize)) ||
            !check_table_size(info_table, count, kTextureInfoRecordSize) ||
            !check_table_size(format_table, count, kTextureFormatRecordSize)) {
            return false;
        }

        pack_.textures.resize(static_cast<std::size_t>(count));
        for (std::size_t i = 0; i < pack_.textures.size(); ++i) {
            TextureRecord& texture = pack_.textures[i];
            texture.index = i;
            const std::uint64_t hash_offset =
                hashes.payload_offset + i * kTextureHashRecordSize;
            texture.info_offset =
                info_table.payload_offset + i * kTextureInfoRecordSize;
            texture.format_offset =
                format_table.payload_offset + i * kTextureFormatRecordSize;
            std::memcpy(texture.info.data(), data_ + texture.info_offset,
                        kTextureInfoRecordSize);
            std::memcpy(texture.format_record.data(),
                        data_ + texture.format_offset, kTextureFormatRecordSize);
            if (blob_table != nullptr) {
                texture.blob_entry_offset =
                    blob_table->payload_offset + i * kTextureBlobRecordSize;
                std::memcpy(texture.blob_entry.data(),
                            data_ + texture.blob_entry_offset,
                            kTextureBlobRecordSize);
            } else {
                texture.storage = TextureStorage::raw;
            }
            parse_texture(texture, hash_offset);
        }
        return true;
    }

    bool reject(TextureRecord& texture, std::uint64_t offset, const std::string& message) {
        std::ostringstream text;
        text << "texture " << texture.index;
        if (!texture.name.empty()) {
            text << " '" << texture.name << "'";
        }
        text << " (info record at offset " << texture.info_offset
             << "): " << message;
        texture.valid = false;
        texture.problem = message;
        add_issue(offset, text.str());
        return false;
    }

    bool parse_texture(TextureRecord& texture, std::uint64_t hash_offset) {
        const unsigned char* info = texture.info.data();
        // Every field is read before the first check, so that a rejected
        // record still reports each hash it claims.
        texture.hash = read_u32_le(info + 36);
        texture.class_hash = read_u32_le(info + 40);
        texture.virtual_offset = read_u32_le(info + 48);
        texture.virtual_end = read_u32_le(info + 52);
        texture.data_size = read_u32_le(info + 56);
        texture.palette_size = read_u32_le(info + 60);
        texture.top_level_size = read_u32_le(info + 64);
        texture.width = read_u16_le(info + 68);
        texture.height = read_u16_le(info + 70);
        texture.width_log2 = info[72];
        texture.height_log2 = info[73];
        texture.format_code = info[74];
        texture.mip_count = info[78];
        texture.layer_order = info[81];
        texture.layer_group = info[84];
        texture.alpha_test_mode = info[85];
        texture.alpha_blend_mode = info[86];
        texture.d3d_format = read_u32_le(texture.format_record.data() + 20);
        const bool raw = texture.storage == TextureStorage::raw;
        const std::uint32_t listed_hash = read_u32_le(data_ + hash_offset);
        texture.table_hash = listed_hash;
        if (!raw) {
            texture.blob_offset = read_u32_le(texture.blob_entry.data() + 4);
            texture.blob_size = read_u32_le(texture.blob_entry.data() + 8);
        }

        std::string error;
        if (!read_fixed_string(info + kTextureNameOffset, kTextureNameSize,
                               texture.name, error)) {
            return reject(texture, texture.info_offset, "name: " + error);
        }
        texture.name_field_full = texture.name.size() == kTextureNameSize - 1;
        if (raw) {
            if (listed_hash != texture.hash) {
                return reject(texture, hash_offset,
                              "hash " + hex32(texture.hash) +
                                  " differs from hash table " +
                                  hex32(listed_hash));
            }
        } else {
            const std::uint32_t blob_hash = read_u32_le(texture.blob_entry.data());
            if (listed_hash != texture.hash || blob_hash != texture.hash) {
                return reject(texture, hash_offset,
                              "hash " + hex32(texture.hash) +
                                  " differs from hash table " + hex32(listed_hash) +
                                  " or blob table " + hex32(blob_hash));
            }
        }
        if (read_u32_le(data_ + hash_offset + 4) != 0U) {
            return reject(texture, hash_offset + 4,
                          "hash table entry has a non-zero second word");
        }

        texture.format = classify_format(texture.format_code, texture.d3d_format);
        if (texture.format == PixelFormat::unsupported) {
            std::ostringstream text;
            text << "unsupported format: code 0x" << std::hex << std::uppercase
                 << static_cast<unsigned int>(texture.format_code)
                 << " at info +74 with D3D format " << hex32(texture.d3d_format)
                 << " at format record +20";
            return reject(texture, texture.info_offset + 74, text.str());
        }
        return check_dimensions(texture) &&
               (raw ? check_raw(texture) : check_blob(texture));
    }

    static PixelFormat classify_format(std::uint8_t code, std::uint32_t d3d) {
        if (code == kFormatCodeDxt1 && d3d == kFourccDxt1) {
            return PixelFormat::dxt1;
        }
        if (code == kFormatCodeDxt3 && d3d == kFourccDxt3) {
            return PixelFormat::dxt3;
        }
        if (code == kFormatCodeA8R8G8B8 && d3d == kD3dFormatA8R8G8B8) {
            return PixelFormat::a8r8g8b8;
        }
        if ((code == kFormatCodeP8Mask || code == kFormatCodeP8Color ||
             code == kFormatCodeP8Custom) &&
            d3d == kD3dFormatP8) {
            return PixelFormat::p8;
        }
        return PixelFormat::unsupported;
    }

    bool check_dimensions(TextureRecord& texture) {
        std::ostringstream text;
        if (texture.width == 0 || texture.height == 0 ||
            texture.width > limits_.max_dimension ||
            texture.height > limits_.max_dimension) {
            text << "dimensions " << texture.width << "x" << texture.height
                 << " are outside 1.." << limits_.max_dimension;
            return reject(texture, texture.info_offset + 68, text.str());
        }
        if (texture.width_log2 >= 16 || texture.height_log2 >= 16 ||
            (1U << texture.width_log2) != texture.width ||
            (1U << texture.height_log2) != texture.height) {
            text << "dimensions " << texture.width << "x" << texture.height
                 << " do not match log2 fields "
                 << static_cast<unsigned int>(texture.width_log2) << ", "
                 << static_cast<unsigned int>(texture.height_log2);
            return reject(texture, texture.info_offset + 72, text.str());
        }
        const unsigned int max_mips =
            std::max(texture.width_log2, texture.height_log2) + 1U;
        if (texture.mip_count == 0 || texture.mip_count > max_mips) {
            text << "mip count " << static_cast<unsigned int>(texture.mip_count)
                 << " is outside 1.." << max_mips;
            return reject(texture, texture.info_offset + 78, text.str());
        }
        std::uint64_t top = 0;
        std::uint64_t chain = 0;
        if (!level_size(texture.format, texture.width, texture.height, top) ||
            !mip_chain_size(texture.format, texture.width, texture.height,
                            texture.mip_count, chain)) {
            return reject(texture, texture.info_offset, "size overflow");
        }
        if (top != texture.top_level_size || chain != texture.data_size) {
            text << pixel_format_name(texture.format) << " " << texture.width
                 << "x" << texture.height << " with "
                 << static_cast<unsigned int>(texture.mip_count)
                 << " levels needs " << top << " top-level and " << chain
                 << " total bytes; info +64 is " << texture.top_level_size
                 << " and +56 is " << texture.data_size;
            return reject(texture, texture.info_offset + 56, text.str());
        }
        if (texture.data_size > limits_.max_decompressed_size) {
            text << "data size " << texture.data_size << " exceeds the limit "
                 << limits_.max_decompressed_size;
            return reject(texture, texture.info_offset + 56, text.str());
        }
        if (texture.format == PixelFormat::p8 &&
            texture.palette_size != kP8PaletteSize) {
            text << "P8 palette size at info +60 is " << texture.palette_size
                 << "; expected " << kP8PaletteSize;
            return reject(texture, texture.info_offset + 60, text.str());
        }
        return true;
    }

    bool check_blob(TextureRecord& texture) {
        std::ostringstream text;
        const std::uint64_t begin =
            pack_.data_payload_offset + pack_.data_prefix_bytes;
        const std::uint64_t end =
            pack_.data_payload_offset + pack_.data_payload_size;
        if (texture.blob_offset < begin || texture.blob_offset > end ||
            texture.blob_size > end - texture.blob_offset) {
            text << "JDLZ blob at file offset " << texture.blob_offset
                 << " with " << texture.blob_size << " bytes is outside the"
                 << " texture data range " << begin << ".." << end;
            return reject(texture, texture.blob_entry_offset + 4, text.str());
        }
        if (texture.blob_size < kJdlzHeaderSize) {
            text << "JDLZ blob size " << texture.blob_size
                 << " is smaller than its 16-byte header";
            return reject(texture, texture.blob_entry_offset + 8, text.str());
        }
        const unsigned char* blob = data_ + texture.blob_offset;
        if (std::memcmp(blob, kJdlzMagic, sizeof(kJdlzMagic)) != 0) {
            text << "blob at file offset " << texture.blob_offset
                 << " does not start with JDLZ 02 10 00 00";
            return reject(texture, texture.blob_offset, text.str());
        }
        texture.jdlz_decompressed_size = read_u32_le(blob + 8);
        const std::uint32_t compressed = read_u32_le(blob + 12);
        std::uint64_t expected = texture.data_size;
        if (texture.format == PixelFormat::p8 &&
            (!align_up(expected, kTextureDataAlignment, expected) ||
             !checked_add(expected, texture.palette_size, expected))) {
            return reject(texture, texture.blob_offset + 8,
                          "P8 JDLZ decompressed size overflows");
        }
        if (texture.format == PixelFormat::p8) {
            std::uint64_t palette_offset = 0;
            std::uint64_t virtual_palette = 0;
            if (!align_up(texture.data_size, kTextureDataAlignment,
                          palette_offset) ||
                !checked_add(texture.virtual_offset, palette_offset,
                             virtual_palette) ||
                virtual_palette != texture.virtual_end) {
                text << "P8 palette virtual offset at info +52 is "
                     << texture.virtual_end << "; expected "
                     << virtual_palette;
                return reject(texture, texture.info_offset + 52, text.str());
            }
        }
        if (expected > limits_.max_decompressed_size) {
            text << "JDLZ decompressed size " << expected
                 << " exceeds the limit " << limits_.max_decompressed_size;
            return reject(texture, texture.blob_offset + 8, text.str());
        }
        if (texture.jdlz_decompressed_size != expected ||
            compressed != texture.blob_size) {
            text << "JDLZ header sizes " << texture.jdlz_decompressed_size
                 << "/" << compressed << " at file offset " << texture.blob_offset
                 << " differ from expected decompressed size " << expected
                 << " / blob table size " << texture.blob_size;
            return reject(texture, texture.blob_offset + 8, text.str());
        }
        texture.valid = true;
        return true;
    }

    // Raw storage: the chain is [data_start + +48, + +56). +48 is a
    // multiple of 128 in every observed record. +52 is not an end offset in
    // embedded packs and is not checked. All arithmetic is 64-bit.
    bool check_raw(TextureRecord& texture) {
        std::ostringstream text;
        if (texture.virtual_offset % kTextureDataAlignment != 0) {
            text << "raw data virtual offset " << texture.virtual_offset
                 << " at info +48 is not a multiple of "
                 << kTextureDataAlignment;
            return reject(texture, texture.info_offset + 48, text.str());
        }
        const std::uint64_t begin = pack_.data_start;
        const std::uint64_t end =
            pack_.data_payload_offset + pack_.data_payload_size;
        std::uint64_t start = 0;
        std::uint64_t stop = 0;
        if (!checked_add(begin, texture.virtual_offset, start) ||
            !checked_add(start, texture.data_size, stop) || stop > end) {
            text << "raw data at virtual offset " << texture.virtual_offset
                 << " with " << texture.data_size << " bytes is outside the"
                 << " texture data range " << begin << ".." << end
                 << " (" << (end - begin) << " bytes)";
            return reject(texture, texture.info_offset + 48, text.str());
        }
        if (texture.format == PixelFormat::p8) {
            std::uint64_t palette_start = 0;
            std::uint64_t palette_stop = 0;
            if (!checked_add(begin, texture.virtual_end, palette_start) ||
                !checked_add(palette_start, texture.palette_size,
                             palette_stop) ||
                palette_stop > end) {
                text << "raw P8 palette at virtual offset "
                     << texture.virtual_end << " with "
                     << texture.palette_size << " bytes is outside the"
                     << " texture data range " << begin << ".." << end;
                return reject(texture, texture.info_offset + 52, text.str());
            }
            if (palette_start < stop && start < palette_stop) {
                return reject(texture, texture.info_offset + 52,
                              "raw P8 palette overlaps its index chain");
            }
        }
        texture.blob_offset = start;
        texture.blob_size = texture.data_size;
        texture.valid = true;
        return true;
    }

    void compute_raw_extent() {
        if (pack_.storage != TextureStorage::raw) {
            return;
        }
        // Every record, valid or not: both fields are u32, so the sum fits.
        for (const TextureRecord& texture : pack_.textures) {
            pack_.raw_extent = std::max<std::uint64_t>(
                pack_.raw_extent,
                std::uint64_t{texture.virtual_offset} + texture.data_size);
        }
    }

    void check_duplicate_hashes() {
        std::vector<std::pair<std::uint32_t, std::size_t>> order;
        for (const TextureRecord& texture : pack_.textures) {
            order.emplace_back(texture.hash, texture.index);
        }
        std::sort(order.begin(), order.end());
        for (std::size_t i = 1; i < order.size(); ++i) {
            if (order[i].first == order[i - 1].first) {
                const TextureRecord& texture = pack_.textures[order[i].second];
                std::ostringstream text;
                text << "texture " << texture.index << " repeats hash "
                     << hex32(texture.hash) << " of texture "
                     << order[i - 1].second << "; hash lookups are ambiguous";
                add_issue(texture.info_offset + 36, text.str());
            }
        }
    }

    // Finds every pair of valid textures whose stored resources share a
    // byte, then invalidates each participant once. JDLZ resources are the
    // blobs. Raw resources are index chains plus the separately addressed
    // P8 palettes. Partners stay unique even if two resources of the same
    // pair overlap.
    void check_blob_overlap() {
        std::vector<std::size_t> order;
        for (const TextureRecord& texture : pack_.textures) {
            if (texture.valid) {
                order.push_back(texture.index);
            }
        }
        std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
            const std::uint64_t oa = pack_.textures[a].blob_offset;
            const std::uint64_t ob = pack_.textures[b].blob_offset;
            return oa != ob ? oa < ob : a < b;
        });

        // Memory stays bounded when many blobs overlap: each texture keeps a
        // partner count and only its kListed smallest partner indices.
        constexpr std::size_t kListed = 8;
        struct Partners {
            std::size_t count = 0;
            std::vector<std::size_t> smallest;

            void add(std::size_t index) {
                ++count;
                smallest.insert(
                    std::upper_bound(smallest.begin(), smallest.end(), index),
                    index);
                if (smallest.size() > kListed) {
                    smallest.pop_back();
                }
            }
        };
        std::vector<Partners> partners(pack_.textures.size());
        std::vector<bool> p8_resource_overlap(pack_.textures.size(), false);
        std::vector<bool> p8_palette_overlap(pack_.textures.size(), false);
        for (std::size_t i = 0; i < order.size(); ++i) {
            const TextureRecord& first = pack_.textures[order[i]];
            std::uint64_t end = 0;
            if (!checked_add(first.blob_offset, first.blob_size, end)) {
                // Unreachable for bounds-checked blobs; fail closed.
                end = std::numeric_limits<std::uint64_t>::max();
            }
            for (std::size_t j = i + 1; j < order.size(); ++j) {
                const TextureRecord& second = pack_.textures[order[j]];
                if (second.blob_offset >= end) {
                    break;
                }
                partners[first.index].add(second.index);
                partners[second.index].add(first.index);
            }
        }

        if (pack_.storage == TextureStorage::raw) {
            const auto overlaps = [](std::uint64_t a_begin, std::uint64_t a_size,
                                     std::uint64_t b_begin, std::uint64_t b_size) {
                std::uint64_t a_end = 0;
                std::uint64_t b_end = 0;
                return checked_add(a_begin, a_size, a_end) &&
                       checked_add(b_begin, b_size, b_end) &&
                       a_begin < b_end && b_begin < a_end;
            };
            for (std::size_t i = 0; i < pack_.textures.size(); ++i) {
                const TextureRecord& first = pack_.textures[i];
                if (!first.valid) {
                    continue;
                }
                const bool first_p8 = first.format == PixelFormat::p8;
                const std::uint64_t first_palette =
                    pack_.data_start + first.virtual_end;
                for (std::size_t j = i + 1; j < pack_.textures.size(); ++j) {
                    const TextureRecord& second = pack_.textures[j];
                    if (!second.valid) {
                        continue;
                    }
                    const bool second_p8 = second.format == PixelFormat::p8;
                    const std::uint64_t second_palette =
                        pack_.data_start + second.virtual_end;
                    const bool chain_overlap =
                        overlaps(first.blob_offset, first.blob_size,
                                 second.blob_offset, second.blob_size);
                    const bool first_palette_second_chain =
                        first_p8 &&
                        overlaps(first_palette, first.palette_size,
                                 second.blob_offset, second.blob_size);
                    const bool second_palette_first_chain =
                        second_p8 &&
                        overlaps(second_palette, second.palette_size,
                                 first.blob_offset, first.blob_size);
                    const bool palette_overlap =
                        first_p8 && second_p8 &&
                        overlaps(first_palette, first.palette_size,
                                 second_palette, second.palette_size);
                    if (!first_palette_second_chain &&
                        !second_palette_first_chain && !palette_overlap) {
                        continue;
                    }
                    p8_resource_overlap[i] = true;
                    p8_resource_overlap[j] = true;
                    p8_palette_overlap[i] = p8_palette_overlap[i] ||
                                            first_palette_second_chain ||
                                            palette_overlap;
                    p8_palette_overlap[j] = p8_palette_overlap[j] ||
                                            second_palette_first_chain ||
                                            palette_overlap;
                    if (!chain_overlap) {
                        partners[i].add(j);
                        partners[j].add(i);
                    }
                }
            }
        }

        for (std::size_t index = 0; index < partners.size(); ++index) {
            const Partners& list = partners[index];
            if (list.count == 0) {
                continue;
            }
            TextureRecord& texture = pack_.textures[index];
            const bool raw = texture.storage == TextureStorage::raw;
            std::ostringstream text;
            if (raw && p8_resource_overlap[index]) {
                text << "raw index data or P8 palette overlaps the resources of ";
            } else {
                text << (raw ? "raw data at file offset "
                             : "JDLZ blob at file offset ")
                     << texture.blob_offset << " (" << texture.blob_size
                     << " bytes) overlaps the " << (raw ? "data" : "blobs")
                     << " of ";
            }
            text << list.count << " textures: ";
            for (std::size_t k = 0; k < list.smallest.size(); ++k) {
                text << (k == 0 ? "" : ", ") << list.smallest[k];
            }
            if (list.count > list.smallest.size()) {
                text << " and " << (list.count - list.smallest.size()) << " more";
            }
            reject(texture,
                   raw ? texture.info_offset +
                             (p8_palette_overlap[index] ? 52U : 48U)
                       : texture.blob_entry_offset + 4,
                   text.str());
        }
    }

    const unsigned char* data_;
    std::uint64_t size_;
    const TextureLimits& limits_;
    TexturePack& pack_;
    std::size_t chunk_count_ = 0;
};

// Expands a 5:6:5 color by bit replication.
std::array<unsigned int, 3> expand_565(std::uint16_t color) {
    const unsigned int r = (color >> 11U) & 0x1FU;
    const unsigned int g = (color >> 5U) & 0x3FU;
    const unsigned int b = color & 0x1FU;
    return {(r << 3U) | (r >> 2U), (g << 2U) | (g >> 4U), (b << 3U) | (b >> 2U)};
}

using BlockPixels = std::array<std::array<unsigned char, 4>, 16>;

// Decodes the 8-byte color part of a block into 16 RGBA pixels.
void decode_color_block(
    const unsigned char* block,
    bool allow_three_color,
    BlockPixels& pixels) {
    const std::uint16_t c0 = read_u16_le(block);
    const std::uint16_t c1 = read_u16_le(block + 2);
    const std::uint32_t indices = read_u32_le(block + 4);
    const auto a = expand_565(c0);
    const auto b = expand_565(c1);
    const bool three_color = allow_three_color && c0 <= c1;

    std::array<std::array<unsigned char, 4>, 4> palette{};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        palette[0][ch] = static_cast<unsigned char>(a[ch]);
        palette[1][ch] = static_cast<unsigned char>(b[ch]);
        if (three_color) {
            palette[2][ch] = static_cast<unsigned char>((a[ch] + b[ch]) / 2U);
            palette[3][ch] = 0;
        } else {
            palette[2][ch] =
                static_cast<unsigned char>((2U * a[ch] + b[ch]) / 3U);
            palette[3][ch] =
                static_cast<unsigned char>((a[ch] + 2U * b[ch]) / 3U);
        }
    }
    palette[0][3] = 255;
    palette[1][3] = 255;
    palette[2][3] = 255;
    palette[3][3] = three_color ? 0 : 255;

    for (unsigned int i = 0; i < 16; ++i) {
        pixels[i] = palette[(indices >> (2U * i)) & 0x3U];
    }
}

// DXT3 explicit alpha: 4 bits per pixel, low nibble first, scaled by 17.
void decode_dxt3_alpha(const unsigned char* block, BlockPixels& pixels) {
    for (unsigned int i = 0; i < 16; ++i) {
        const unsigned int nibble = (block[i / 2U] >> (4U * (i % 2U))) & 0x0FU;
        pixels[i][3] = static_cast<unsigned char>(nibble * 17U);
    }
}

} // namespace

std::uint32_t bin_hash(const std::string& text) noexcept {
    std::uint32_t hash = 0xFFFFFFFFU;
    for (char ch : text) {
        hash = hash * 33U + static_cast<unsigned char>(ch);
    }
    return hash;
}

const char* pixel_format_name(PixelFormat format) noexcept {
    switch (format) {
    case PixelFormat::dxt1:
        return "DXT1";
    case PixelFormat::dxt3:
        return "DXT3";
    case PixelFormat::a8r8g8b8:
        return "A8R8G8B8";
    case PixelFormat::p8:
        return "P8";
    case PixelFormat::unsupported:
        break;
    }
    return "unsupported";
}

bool level_size(
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t& out) noexcept {
    if (width == 0 || height == 0) {
        return false;
    }
    switch (format) {
    case PixelFormat::dxt1:
    case PixelFormat::dxt3: {
        const std::uint64_t blocks_x = (std::uint64_t{width} + 3U) / 4U;
        const std::uint64_t blocks_y = (std::uint64_t{height} + 3U) / 4U;
        std::uint64_t blocks = 0;
        return checked_mul(blocks_x, blocks_y, blocks) &&
               checked_mul(blocks, format == PixelFormat::dxt1 ? 8U : 16U, out);
    }
    case PixelFormat::a8r8g8b8: {
        std::uint64_t pixels = 0;
        return checked_mul(width, height, pixels) && checked_mul(pixels, 4U, out);
    }
    case PixelFormat::p8:
        return checked_mul(width, height, out);
    case PixelFormat::unsupported:
        break;
    }
    return false;
}

bool mip_chain_size(
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_count,
    std::uint64_t& out) noexcept {
    if (mip_count == 0 || mip_count > 32) {
        return false;
    }
    out = 0;
    for (std::uint32_t level = 0; level < mip_count; ++level) {
        std::uint64_t size = 0;
        if (!level_size(format, width, height, size) ||
            !checked_add(out, size, out)) {
            return false;
        }
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return true;
}

bool record_claims_hash(const TextureRecord& texture, std::uint32_t hash) noexcept {
    return texture.hash == hash || texture.table_hash == hash ||
           (texture.storage == TextureStorage::jdlz &&
            read_u32_le(texture.blob_entry.data()) == hash);
}

bool texture_alpha_test_enabled(const TextureRecord& texture) noexcept {
    return texture.alpha_test_mode == 1U;
}

int texture_draw_layer(const TextureRecord& texture) noexcept {
    const int layer = (texture.layer_group != 0U ? 8 : 0) +
                      static_cast<int>(static_cast<std::int8_t>(texture.layer_order));
    if (layer < 0) {
        return 0;
    }
    return layer < kTextureDrawLayerCount ? layer : kTextureDrawLayerCount - 1;
}

TextureAlphaBlend texture_alpha_blend(const TextureRecord& texture) noexcept {
    if (texture.alpha_blend_mode == 0U) {
        return TextureAlphaBlend::disabled;
    }
    if (texture.alpha_blend_mode == 2U) {
        return TextureAlphaBlend::additive;
    }
    return TextureAlphaBlend::source_over;
}

const char* texture_alpha_blend_name(TextureAlphaBlend blend) noexcept {
    switch (blend) {
    case TextureAlphaBlend::disabled:
        return "disabled";
    case TextureAlphaBlend::source_over:
        return "source-over";
    case TextureAlphaBlend::additive:
        return "additive";
    }
    return "unknown";
}

std::size_t TexturePack::valid_texture_count() const noexcept {
    std::size_t count = 0;
    for (const TextureRecord& texture : textures) {
        if (texture.valid) {
            ++count;
        }
    }
    return count;
}

TexturePack parse_texture_pack(
    const unsigned char* data,
    std::size_t size,
    const TextureLimits& limits) {
    TexturePack pack;
    pack.file_size = size;
    pack.parse_source = data;
    if (data == nullptr && size != 0) {
        pack.issues.push_back(TextureIssue{0, "null input buffer"});
        return pack;
    }
    if (size > limits.max_file_size) {
        std::ostringstream text;
        text << "buffer is " << size << " bytes; limit is "
             << limits.max_file_size;
        pack.issues.push_back(TextureIssue{0, text.str()});
        return pack;
    }
    try {
        TextureParser parser(data, size, limits, pack);
        parser.run();
    } catch (const std::bad_alloc&) {
        pack.issues.push_back(
            TextureIssue{0, "out of memory while parsing textures"});
    } catch (const std::exception& error) {
        pack.issues.push_back(
            TextureIssue{0, std::string("texture parse failed: ") + error.what()});
    }
    return pack;
}

namespace {

// Reads the whole file read-only into bytes. Returns an empty string on
// success, otherwise the reason; file_size is set once it is known.
std::string read_whole_file(
    const std::filesystem::path& path,
    std::uint64_t max_file_size,
    std::vector<unsigned char>& bytes,
    std::uint64_t& file_size) {
    bytes.clear();
    file_size = 0;
    std::error_code size_error;
    const std::uintmax_t size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        return "failed to read file size";
    }
    file_size = size;
    if (size > max_file_size || size > std::numeric_limits<std::size_t>::max() ||
        size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max())) {
        std::ostringstream text;
        text << "file is " << size << " bytes; limit is " << max_file_size;
        return text.str();
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return "failed to open file";
    }
    try {
        bytes.resize(static_cast<std::size_t>(size));
    } catch (const std::exception&) {
        return "out of memory while reading file";
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        return "failed to read whole file";
    }
    return {};
}

// Checks shared by the in-memory entry points; returns false with an issue.
bool check_buffer(
    const unsigned char* data,
    std::size_t size,
    const TextureLimits& limits,
    std::vector<TextureIssue>& issues) {
    if (data == nullptr && size != 0) {
        issues.push_back(TextureIssue{0, "null input buffer"});
        return false;
    }
    if (size > limits.max_file_size) {
        std::ostringstream text;
        text << "buffer is " << size << " bytes; limit is "
             << limits.max_file_size;
        issues.push_back(TextureIssue{0, text.str()});
        return false;
    }
    return true;
}

// Collects the offsets of every 0xB3300000 root. Containers are walked
// depth first in file order with an explicit stack of pending ranges.
class RootWalker {
public:
    RootWalker(
        const unsigned char* data,
        std::uint64_t size,
        const TextureLimits& limits,
        std::vector<std::uint64_t>& roots,
        std::vector<TextureIssue>& issues)
        : data_(data), size_(size), limits_(limits), roots_(roots),
          issues_(issues) {}

    void run() {
        std::vector<Range> pending;
        pending.push_back(Range{0, size_, 0, false, RawChunk{}});
        while (!pending.empty() && !stopped_) {
            const Range range = pending.back();
            pending.pop_back();
            walk(range, pending);
        }
    }

private:
    struct Range {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
        std::size_t depth = 0;
        bool has_owner = false;
        RawChunk owner;
    };

    void add_issue(std::uint64_t offset, std::string message) {
        issues_.push_back(TextureIssue{offset, std::move(message)});
    }

    void walk(const Range& range, std::vector<Range>& pending) {
        const std::string context =
            range.has_owner ? chunk_label(range.owner) : "top level";
        std::vector<Range> children;
        std::uint64_t offset = range.begin;
        while (offset < range.end) {
            if (range.end - offset < kHeaderSize) {
                std::ostringstream text;
                text << context << ": " << (range.end - offset)
                     << " trailing bytes are smaller than a chunk header";
                add_issue(offset, text.str());
                break;
            }
            if (chunk_count_ >= limits_.max_walk_chunks) {
                std::ostringstream text;
                text << context << ": root search chunk limit "
                     << limits_.max_walk_chunks << " exceeded";
                add_issue(offset, text.str());
                stopped_ = true;
                return;
            }
            RawChunk chunk;
            chunk.offset = offset;
            chunk.id = read_u32_le(data_ + offset);
            chunk.payload_size = read_u32_le(data_ + offset + 4);
            chunk.payload_offset = offset + kHeaderSize;
            if (chunk.payload_size > range.end - chunk.payload_offset) {
                // No reliable next offset: skip the rest of this parent.
                std::ostringstream text;
                text << context << ": " << chunk_label(chunk) << " declares "
                     << chunk.payload_size << " payload bytes but only "
                     << (range.end - chunk.payload_offset)
                     << " remain in its parent";
                add_issue(offset, text.str());
                break;
            }
            ++chunk_count_;
            if (chunk.id == kTexturePackRootId) {
                if (roots_.size() >= limits_.max_packs) {
                    std::ostringstream text;
                    text << context << ": texture pack limit "
                         << limits_.max_packs << " exceeded";
                    add_issue(offset, text.str());
                    stopped_ = true;
                    return;
                }
                roots_.push_back(offset);
            } else if ((chunk.id & 0x80000000U) != 0 && chunk.payload_size != 0) {
                if (range.depth >= limits_.max_depth) {
                    std::ostringstream text;
                    text << context << ": " << chunk_label(chunk)
                         << " is nested deeper than the limit "
                         << limits_.max_depth << "; not searched";
                    add_issue(offset, text.str());
                } else {
                    children.push_back(Range{
                        chunk.payload_offset,
                        chunk.payload_offset + chunk.payload_size,
                        range.depth + 1, true, chunk});
                }
            }
            offset = chunk.payload_offset + chunk.payload_size;
        }
        // Reverse, so the first child is walked next.
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }

    const unsigned char* data_;
    std::uint64_t size_;
    const TextureLimits& limits_;
    std::vector<std::uint64_t>& roots_;
    std::vector<TextureIssue>& issues_;
    std::size_t chunk_count_ = 0;
    bool stopped_ = false;
};

// Reports each hash that a pack repeats from an earlier pack of the set.
// Repeats inside one pack are already issues of that pack.
void check_cross_pack_hashes(TexturePackSet& set) {
    struct Entry {
        std::uint32_t hash;
        std::size_t pack;
        std::size_t index;
        bool operator<(const Entry& other) const {
            if (hash != other.hash) return hash < other.hash;
            if (pack != other.pack) return pack < other.pack;
            return index < other.index;
        }
    };
    std::vector<Entry> entries;
    for (std::size_t p = 0; p < set.packs.size(); ++p) {
        for (const TextureRecord& texture : set.packs[p].textures) {
            entries.push_back(Entry{texture.hash, p, texture.index});
        }
    }
    std::sort(entries.begin(), entries.end());
    for (std::size_t i = 1; i < entries.size(); ++i) {
        const Entry& previous = entries[i - 1];
        const Entry& entry = entries[i];
        if (entry.hash != previous.hash || entry.pack == previous.pack) {
            continue;
        }
        const TextureRecord& texture = set.packs[entry.pack].textures[entry.index];
        std::ostringstream text;
        text << "pack " << entry.pack << " texture " << entry.index
             << " repeats hash " << hex32(entry.hash) << " of pack "
             << previous.pack << " texture " << previous.index
             << "; hash lookups in this file are ambiguous";
        set.issues.push_back(TextureIssue{texture.info_offset + 36, text.str()});
    }
}

} // namespace

const char* texture_storage_name(TextureStorage storage) noexcept {
    switch (storage) {
    case TextureStorage::jdlz:
        return "jdlz";
    case TextureStorage::raw:
        return "raw";
    }
    return "unknown";
}

TexturePack load_texture_pack(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TextureLimits& limits) {
    TexturePack pack;
    const std::string error =
        read_whole_file(path, limits.max_file_size, bytes, pack.file_size);
    if (!error.empty()) {
        pack.issues.push_back(TextureIssue{0, error});
        return pack;
    }
    return parse_texture_pack(bytes.data(), bytes.size(), limits);
}

TexturePack parse_texture_pack_at(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t root_offset,
    const TextureLimits& limits) {
    TexturePack pack;
    pack.file_size = size;
    pack.parse_source = data;
    pack.root_offset = root_offset;
    if (!check_buffer(data, size, limits, pack.issues)) {
        return pack;
    }
    try {
        TextureParser parser(data, size, limits, pack);
        parser.run_at(root_offset);
    } catch (const std::bad_alloc&) {
        pack.issues.push_back(
            TextureIssue{0, "out of memory while parsing textures"});
    } catch (const std::exception& error) {
        pack.issues.push_back(
            TextureIssue{0, std::string("texture parse failed: ") + error.what()});
    }
    return pack;
}

bool TexturePackSet::ok() const noexcept {
    if (!issues.empty()) {
        return false;
    }
    for (const TexturePack& pack : packs) {
        if (!pack.ok()) {
            return false;
        }
    }
    return true;
}

TexturePackSet parse_texture_pack_set(
    const unsigned char* data,
    std::size_t size,
    const TextureLimits& limits) {
    TexturePackSet set;
    set.file_size = size;
    if (!check_buffer(data, size, limits, set.issues)) {
        return set;
    }
    try {
        std::vector<std::uint64_t> roots;
        RootWalker walker(data, size, limits, roots, set.issues);
        walker.run();
        bool complete = set.issues.empty();
        // Depth-first order already is file order; sorting makes that
        // independent of the walk.
        std::sort(roots.begin(), roots.end());
        if (roots.empty()) {
            set.issues.push_back(TextureIssue{
                0, "no " + format_chunk_id(kTexturePackRootId) +
                       " texture pack root found"});
        }
        set.packs.reserve(roots.size());
        std::size_t records = 0;
        for (const std::uint64_t root : roots) {
            TexturePack pack = parse_texture_pack_at(data, size, root, limits);
            complete = complete && pack.tables_parsed;
            records += pack.textures.size();
            set.packs.push_back(std::move(pack));
            if (records > limits.max_set_textures) {
                std::ostringstream text;
                text << "texture packs hold " << records
                     << " records after the root at offset " << root
                     << "; limit is " << limits.max_set_textures
                     << "; later roots are not parsed";
                set.issues.push_back(TextureIssue{root, text.str()});
                complete = false;
                break;
            }
        }
        check_cross_pack_hashes(set);
        set.complete = complete;
    } catch (const std::bad_alloc&) {
        set.packs.clear();
        set.complete = false;
        set.issues.push_back(
            TextureIssue{0, "out of memory while parsing texture packs"});
    }
    return set;
}

TexturePackSet load_texture_pack_set(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TextureLimits& limits) {
    TexturePackSet set;
    const std::string error =
        read_whole_file(path, limits.max_file_size, bytes, set.file_size);
    if (!error.empty()) {
        set.issues.push_back(TextureIssue{0, error});
        return set;
    }
    return parse_texture_pack_set(bytes.data(), bytes.size(), limits);
}

// JDLZ: after the header, two flag bytes are loaded whenever their shift
// registers are exhausted (flags1 first). A clear flags1 bit copies one
// literal byte. A set flags1 bit reads a 2-byte match b0 b1: if the next
// flags2 bit is set, length = (b1 | (b0 & 0xF0) << 4) + 3 and distance =
// (b0 & 0x0F) + 1; otherwise distance = (b1 | (b0 & 0xE0) << 3) + 17 and
// length = (b0 & 0x1F) + 3. Decoding stops when the output is full.
bool jdlz_decompress(
    const unsigned char* blob,
    std::size_t size,
    std::uint32_t max_output,
    std::vector<unsigned char>& output,
    std::size_t& unused_input,
    std::string& error) {
    output.clear();
    unused_input = 0;
    if (blob == nullptr || size < kJdlzHeaderSize) {
        error = "JDLZ blob is smaller than its 16-byte header";
        return false;
    }
    if (std::memcmp(blob, kJdlzMagic, sizeof(kJdlzMagic)) != 0) {
        error = "JDLZ blob does not start with JDLZ 02 10 00 00";
        return false;
    }
    const std::uint32_t target = read_u32_le(blob + 8);
    const std::uint32_t compressed = read_u32_le(blob + 12);
    if (compressed != size) {
        error = "JDLZ header compressed size " + std::to_string(compressed) +
                " differs from the blob size " + std::to_string(size);
        return false;
    }
    if (target > max_output) {
        error = "JDLZ decompressed size " + std::to_string(target) +
                " exceeds the limit " + std::to_string(max_output);
        return false;
    }

    output.assign(target, 0);
    std::size_t in = kJdlzHeaderSize;
    std::size_t out = 0;
    unsigned int flags1 = 1;
    unsigned int flags2 = 1;
    while (out < target) {
        if (flags1 == 1) {
            if (in >= size) {
                break;
            }
            flags1 = blob[in++] | 0x100U;
        }
        if (flags2 == 1) {
            if (in >= size) {
                break;
            }
            flags2 = blob[in++] | 0x100U;
        }
        if ((flags1 & 1U) != 0) {
            if (size - in < 2) {
                break;
            }
            const unsigned int b0 = blob[in];
            const unsigned int b1 = blob[in + 1];
            in += 2;
            std::size_t length = 0;
            std::size_t distance = 0;
            if ((flags2 & 1U) != 0) {
                length = (b1 | ((b0 & 0xF0U) << 4U)) + 3U;
                distance = (b0 & 0x0FU) + 1U;
            } else {
                distance = (b1 | ((b0 & 0xE0U) << 3U)) + 17U;
                length = (b0 & 0x1FU) + 3U;
            }
            flags2 >>= 1U;
            if (distance > out) {
                error = "JDLZ match at input offset " + std::to_string(in - 2) +
                        " reaches " + std::to_string(distance) +
                        " bytes back from output offset " + std::to_string(out);
                output.clear();
                return false;
            }
            if (length > target - out) {
                error = "JDLZ match at input offset " + std::to_string(in - 2) +
                        " of " + std::to_string(length) +
                        " bytes overruns the output at offset " +
                        std::to_string(out);
                output.clear();
                return false;
            }
            for (std::size_t i = 0; i < length; ++i, ++out) {
                output[out] = output[out - distance];
            }
        } else {
            if (in >= size) {
                break;
            }
            output[out++] = blob[in++];
        }
        flags1 >>= 1U;
    }
    if (out != target) {
        error = "JDLZ input ended after " + std::to_string(out) + " of " +
                std::to_string(target) + " output bytes";
        output.clear();
        return false;
    }
    unused_input = size - in;
    return true;
}

bool texture_payload(
    const unsigned char* data,
    std::size_t size,
    const TextureRecord& texture,
    std::vector<unsigned char>& payload,
    std::size_t& unused_input,
    std::string& error,
    P8Palette* p8_palette) {
    payload.clear();
    if (p8_palette != nullptr) {
        p8_palette->fill(0);
    }
    if (!texture.valid) {
        error = "texture " + std::to_string(texture.index) + " is not valid";
        return false;
    }
    if (data == nullptr || texture.blob_offset > size ||
        texture.blob_size > size - texture.blob_offset) {
        error = "texture blob is outside the input buffer";
        return false;
    }
    if (texture.storage == TextureStorage::raw) {
        if (texture.blob_size != texture.data_size) {
            error = "raw data size " + std::to_string(texture.blob_size) +
                    " differs from the info data size " +
                    std::to_string(texture.data_size);
            return false;
        }
        unused_input = 0;
        const unsigned char* begin =
            data + static_cast<std::size_t>(texture.blob_offset);
        payload.assign(begin, begin + texture.blob_size);
        if (texture.format == PixelFormat::p8 && p8_palette != nullptr) {
            if (texture.blob_offset < texture.virtual_offset) {
                error = "P8 raw palette base underflows";
                payload.clear();
                return false;
            }
            const std::uint64_t base =
                texture.blob_offset - texture.virtual_offset;
            std::uint64_t palette_offset = 0;
            if (!checked_add(base, texture.virtual_end, palette_offset) ||
                palette_offset > size || kP8PaletteSize > size - palette_offset) {
                error = "P8 raw palette is outside the input buffer";
                payload.clear();
                return false;
            }
            std::copy_n(data + static_cast<std::size_t>(palette_offset),
                        kP8PaletteSize, p8_palette->begin());
        }
        return true;
    }
    std::uint64_t decompressed_size = texture.data_size;
    if (texture.format == PixelFormat::p8 &&
        (!align_up(decompressed_size, kTextureDataAlignment,
                   decompressed_size) ||
         !checked_add(decompressed_size, kP8PaletteSize,
                      decompressed_size))) {
        error = "P8 JDLZ decompressed size overflows";
        return false;
    }
    if (decompressed_size > std::numeric_limits<std::size_t>::max() ||
        !jdlz_decompress(data + texture.blob_offset, texture.blob_size,
                         static_cast<std::size_t>(decompressed_size), payload,
                         unused_input, error)) {
        return false;
    }
    if (texture.format == PixelFormat::p8) {
        std::uint64_t palette_offset = 0;
        if (!align_up(texture.data_size, kTextureDataAlignment,
                      palette_offset) ||
            palette_offset > payload.size() ||
            kP8PaletteSize > payload.size() - palette_offset) {
            error = "P8 JDLZ palette layout is outside the decompressed data";
            payload.clear();
            return false;
        }
        if (!std::all_of(payload.begin() + texture.data_size,
                         payload.begin() + static_cast<std::size_t>(palette_offset),
                         [](unsigned char byte) { return byte == 0; })) {
            error = "P8 JDLZ padding before the palette is not zero";
            payload.clear();
            return false;
        }
        if (p8_palette != nullptr) {
            std::copy_n(payload.begin() + static_cast<std::size_t>(palette_offset),
                        kP8PaletteSize, p8_palette->begin());
        }
        payload.resize(texture.data_size);
    }
    if (payload.size() != texture.data_size) {
        error = "decompressed size differs from the info record";
        payload.clear();
        return false;
    }
    return true;
}

bool decode_level(
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    const unsigned char* data,
    std::size_t size,
    std::vector<unsigned char>& rgba,
    std::string& error,
    const P8Palette* p8_palette) {
    rgba.clear();
    std::uint64_t expected = 0;
    std::uint64_t pixel_bytes = 0;
    if (!level_size(format, width, height, expected) ||
        !checked_mul(std::uint64_t{width} * height, 4U, pixel_bytes) ||
        pixel_bytes > std::numeric_limits<std::size_t>::max()) {
        error = std::string("cannot decode ") + pixel_format_name(format) +
                " " + std::to_string(width) + "x" + std::to_string(height);
        return false;
    }
    if (data == nullptr || size != expected) {
        error = std::string(pixel_format_name(format)) + " level " +
                std::to_string(width) + "x" + std::to_string(height) +
                " needs exactly " + std::to_string(expected) +
                " bytes, got " + std::to_string(size);
        return false;
    }
    if (format == PixelFormat::p8) {
        if (p8_palette == nullptr) {
            error = "P8 level requires a 256-entry palette";
            return false;
        }
        rgba.resize(static_cast<std::size_t>(pixel_bytes));
        for (std::size_t i = 0; i < size; ++i) {
            const std::size_t entry = static_cast<std::size_t>(data[i]) * 4U;
            rgba[i * 4U + 0U] = (*p8_palette)[entry + 2U];
            rgba[i * 4U + 1U] = (*p8_palette)[entry + 1U];
            rgba[i * 4U + 2U] = (*p8_palette)[entry + 0U];
            rgba[i * 4U + 3U] = (*p8_palette)[entry + 3U];
        }
        return true;
    }

    rgba.assign(static_cast<std::size_t>(pixel_bytes), 0);
    if (format == PixelFormat::a8r8g8b8) {
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i + 0] = data[i + 2];
            rgba[i + 1] = data[i + 1];
            rgba[i + 2] = data[i + 0];
            rgba[i + 3] = data[i + 3];
        }
        return true;
    }

    const std::size_t stride = format == PixelFormat::dxt1 ? 8U : 16U;
    const std::uint32_t blocks_x = (width + 3U) / 4U;
    const std::uint32_t blocks_y = (height + 3U) / 4U;
    BlockPixels pixels{};
    const unsigned char* block = data;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx, block += stride) {
            if (format == PixelFormat::dxt1) {
                decode_color_block(block, true, pixels);
            } else {
                decode_color_block(block + 8, false, pixels);
                decode_dxt3_alpha(block, pixels);
            }
            for (std::uint32_t py = 0; py < 4; ++py) {
                const std::uint64_t y = std::uint64_t{by} * 4U + py;
                if (y >= height) {
                    break;
                }
                for (std::uint32_t px = 0; px < 4; ++px) {
                    const std::uint64_t x = std::uint64_t{bx} * 4U + px;
                    if (x >= width) {
                        break;
                    }
                    const auto at = static_cast<std::size_t>((y * width + x) * 4U);
                    std::memcpy(&rgba[at], pixels[py * 4U + px].data(), 4);
                }
            }
        }
    }
    return true;
}

bool write_dds(
    std::ostream& output,
    PixelFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_count,
    const unsigned char* payload,
    std::size_t size,
    std::string& error) {
    std::uint64_t base = 0;
    std::uint64_t total = 0;
    if (!level_size(format, width, height, base) ||
        !mip_chain_size(format, width, height, mip_count, total) ||
        base > 0xFFFFFFFFU) {
        error = "invalid DDS format, dimensions or mip count";
        return false;
    }
    if (payload == nullptr || total != size) {
        error = "DDS payload is " + std::to_string(size) + " bytes; " +
                std::to_string(mip_count) + " levels need " +
                std::to_string(total);
        return false;
    }
    if (format == PixelFormat::p8) {
        error = "P8 DDS export requires a 256-entry palette";
        return false;
    }

    // DDS_HEADER as publicly documented for DirectDraw Surface files.
    std::array<unsigned char, 128> header{};
    std::memcpy(&header[0], "DDS ", 4);
    const bool compressed = format != PixelFormat::a8r8g8b8;
    std::uint32_t flags = 0x1U | 0x2U | 0x4U | 0x1000U; // caps, h, w, pf
    flags |= compressed ? 0x80000U : 0x8U;               // linear size : pitch
    std::uint32_t caps = 0x1000U;                        // texture
    if (mip_count > 1) {
        flags |= 0x20000U;           // mip map count
        caps |= 0x8U | 0x400000U;    // complex, mip map
    }
    put_u32(&header[4], 124);
    put_u32(&header[8], flags);
    put_u32(&header[12], height);
    put_u32(&header[16], width);
    put_u32(&header[20], compressed ? static_cast<std::uint32_t>(base) : width * 4U);
    put_u32(&header[28], mip_count);
    put_u32(&header[76], 32); // pixel format size
    if (compressed) {
        put_u32(&header[80], 0x4U); // DDPF_FOURCC
        put_u32(&header[84], format == PixelFormat::dxt1 ? kFourccDxt1 : kFourccDxt3);
    } else {
        put_u32(&header[80], 0x41U); // DDPF_RGB | DDPF_ALPHAPIXELS
        put_u32(&header[88], 32);
        put_u32(&header[92], 0x00FF0000U);
        put_u32(&header[96], 0x0000FF00U);
        put_u32(&header[100], 0x000000FFU);
        put_u32(&header[104], 0xFF000000U);
    }
    put_u32(&header[108], caps);

    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char*>(payload),
                 static_cast<std::streamsize>(size));
    output.flush();
    if (!output) {
        error = "failed to write DDS output";
        return false;
    }
    return true;
}

std::string texture_file_stem(const TextureRecord& texture) {
    std::string index = std::to_string(texture.index);
    if (index.size() < 4) {
        index.insert(0, 4 - index.size(), '0');
    }
    std::string stem = "t" + index + "_" + hex32(texture.hash).substr(2) + "_";
    if (texture.name.empty()) {
        return stem + "unnamed";
    }
    for (char ch : texture.name) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
                          ch == '.';
        stem.push_back(safe ? ch : '_');
    }
    return stem;
}

bool append_texture_animations(const unsigned char* data,std::size_t size,
    std::vector<TextureAnimation>& animations,std::string& error) {
    const auto word=[](const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);};
    auto next=animations;
    for(std::size_t at=0;at+8<=size;) {
        const auto tag=word(data+at),bytes=word(data+at+4);
        if(bytes>size-at-8){error="texture animation source chunk truncated";return false;}
        if(tag==0xB0300100U) {
            const auto* p=data+at+8;
            if(bytes<24||word(p)!=0x30300101U||word(p+4)!=16){error="texture animation header invalid";return false;}
            if(word(p+8)==1) {
                if(bytes<40||word(p+24)!=0x30300102U){error="texture animation definitions absent";return false;}
                const auto rows=word(p+28);const std::size_t frames_at=32ULL+rows;
                if(rows%52||rows/52>128||frames_at+8>bytes||word(p+frames_at)!=0x30300103U||word(p+frames_at+4)!=bytes-frames_at-8){error="texture animation tables invalid";return false;}
                std::size_t frame=frames_at+8;
                for(std::size_t row=32;row<frames_at;row+=52) {
                    TextureAnimation value;value.hash=word(p+row+24);
                    const auto count=word(p+row+28);value.fps=word(p+row+32);value.clock=word(p+row+36);
                    if(!count||count>512||!value.fps||value.fps>240||value.clock>1||count>(bytes-frame)/16){error="texture animation definition outside bounds";return false;}
                    for(unsigned i=0;i<count;++i)value.frames.push_back(word(p+frame+i*16));
                    frame+=count*16;
                    const auto old=std::find_if(next.begin(),next.end(),[&](const auto& a){return a.hash==value.hash;});
                    if(old==next.end())next.push_back(std::move(value));
                    else if(old->fps!=value.fps||old->clock!=value.clock||old->frames!=value.frames){error="conflicting texture animation definition";return false;}
                }
                if(frame!=bytes){error="texture animation frame count mismatch";return false;}
            }
        }
        at+=8+bytes;
    }
    animations=std::move(next);error.clear();return true;
}
bool load_texture_animations(const std::filesystem::path& path,
    std::vector<TextureAnimation>& animations,std::string& error) {
    try {
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input||input.tellg()<0){error="animation metadata source unavailable";return false;}
        const auto size=static_cast<std::uint64_t>(input.tellg());
        if(size>TextureLimits{}.max_file_size){error="animation source exceeds file bound";return false;}
        auto next=animations;std::array<unsigned char,8> header{};
        std::vector<unsigned char> metadata;std::size_t chunks=0;
        for(std::uint64_t at=0;at<size;) {
            if(size-at<8||++chunks>TextureLimits{}.max_walk_chunks){error="animation source chunk header truncated or excessive";return false;}
            input.seekg(static_cast<std::streamoff>(at));
            if(!input.read(reinterpret_cast<char*>(header.data()),8)){error="animation source header read failed";return false;}
            const auto tag=read_u32_le(header.data()),bytes=read_u32_le(header.data()+4);
            if(bytes>size-at-8){error="animation source chunk exceeds file";return false;}
            if(tag==0xB0300100U) {
                // Port metadata cap; the parser separately bounds rows/frames.
                if(bytes>2U*1024U*1024U){error="animation metadata exceeds bounded scratch";return false;}
                metadata.resize(std::size_t(bytes)+8);std::copy(header.begin(),header.end(),metadata.begin());
                if(!input.read(reinterpret_cast<char*>(metadata.data()+8),bytes)){error="animation metadata read failed";return false;}
                if(!append_texture_animations(metadata.data(),metadata.size(),next,error))return false;
            }
            at+=8ULL+bytes;
        }
        animations=std::move(next);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::size_t texture_animation_phase(const TextureAnimation& a,std::uint32_t ticks_60hz) noexcept {
    if(a.frames.empty())return 0;
    //545EA0 stores integer animation ticks*(1/60) as binary32, then truncates fps*t.
    const float seconds=float(ticks_60hz)*.01666666753590107F;
    return std::uint64_t(float(a.fps)*seconds)%a.frames.size();
}

} // namespace nfsu
