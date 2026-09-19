#include "nfsu/texture.h"

#include "nfsu/chunk.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <new>
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
constexpr std::uint32_t kD3dFormatA8R8G8B8 = 21U;
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

void put_u16(unsigned char* out, std::uint32_t value) {
    out[0] = static_cast<unsigned char>(value & 0xFFU);
    out[1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
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
        if (!find_single(tables, kTexturePackHeaderId, info.offset,
                         info_context, header) ||
            !find_single(tables, kTextureHashTableId, info.offset,
                         info_context, hashes) ||
            !find_single(tables, kTextureBlobTableId, info.offset,
                         info_context, blob_table) ||
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
        if (!parse_tables(hashes, blob_table, info_table, format_table)) {
            return;
        }
        check_duplicate_hashes();
        check_blob_overlap();
    }

private:
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

    bool parse_tables(
        const RawChunk& hashes,
        const RawChunk& blob_table,
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
        if (!check_table_size(blob_table, count, kTextureBlobRecordSize) ||
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
            texture.blob_entry_offset =
                blob_table.payload_offset + i * kTextureBlobRecordSize;
            texture.info_offset =
                info_table.payload_offset + i * kTextureInfoRecordSize;
            texture.format_offset =
                format_table.payload_offset + i * kTextureFormatRecordSize;
            std::memcpy(texture.info.data(), data_ + texture.info_offset,
                        kTextureInfoRecordSize);
            std::memcpy(texture.format_record.data(),
                        data_ + texture.format_offset, kTextureFormatRecordSize);
            std::memcpy(texture.blob_entry.data(),
                        data_ + texture.blob_entry_offset,
                        kTextureBlobRecordSize);
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
        std::string error;
        if (!read_fixed_string(info + kTextureNameOffset, kTextureNameSize,
                               texture.name, error)) {
            return reject(texture, texture.info_offset, "name: " + error);
        }
        texture.name_field_full = texture.name.size() == kTextureNameSize - 1;
        texture.hash = read_u32_le(info + 36);
        texture.class_hash = read_u32_le(info + 40);
        texture.virtual_offset = read_u32_le(info + 48);
        texture.virtual_end = read_u32_le(info + 52);
        texture.data_size = read_u32_le(info + 56);
        texture.top_level_size = read_u32_le(info + 64);
        texture.width = read_u16_le(info + 68);
        texture.height = read_u16_le(info + 70);
        texture.width_log2 = info[72];
        texture.height_log2 = info[73];
        texture.format_code = info[74];
        texture.mip_count = info[78];
        texture.d3d_format = read_u32_le(texture.format_record.data() + 20);
        texture.blob_offset = read_u32_le(texture.blob_entry.data() + 4);
        texture.blob_size = read_u32_le(texture.blob_entry.data() + 8);

        const std::uint32_t listed_hash = read_u32_le(data_ + hash_offset);
        const std::uint32_t blob_hash = read_u32_le(texture.blob_entry.data());
        if (listed_hash != texture.hash || blob_hash != texture.hash) {
            return reject(texture, hash_offset,
                          "hash " + hex32(texture.hash) +
                              " differs from hash table " + hex32(listed_hash) +
                              " or blob table " + hex32(blob_hash));
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
        return check_dimensions(texture) && check_blob(texture);
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
        if (texture.jdlz_decompressed_size != texture.data_size ||
            compressed != texture.blob_size) {
            text << "JDLZ header sizes " << texture.jdlz_decompressed_size
                 << "/" << compressed << " at file offset " << texture.blob_offset
                 << " differ from info data size " << texture.data_size
                 << " / blob table size " << texture.blob_size;
            return reject(texture, texture.blob_offset + 8, text.str());
        }
        texture.valid = true;
        return true;
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

    void check_blob_overlap() {
        std::vector<std::size_t> order;
        for (const TextureRecord& texture : pack_.textures) {
            if (texture.valid) {
                order.push_back(texture.index);
            }
        }
        std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
            return pack_.textures[a].blob_offset < pack_.textures[b].blob_offset;
        });
        for (std::size_t i = 1; i < order.size(); ++i) {
            TextureRecord& previous = pack_.textures[order[i - 1]];
            TextureRecord& current = pack_.textures[order[i]];
            if (current.blob_offset - previous.blob_offset < previous.blob_size) {
                std::ostringstream text;
                text << "JDLZ blob at file offset " << current.blob_offset
                     << " overlaps the blob of texture " << previous.index;
                reject(current, current.blob_entry_offset + 4, text.str());
                previous.valid = false;
                previous.problem = "blob overlaps texture " +
                                   std::to_string(current.index);
            }
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

TexturePack load_texture_pack(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TextureLimits& limits) {
    TexturePack pack;
    bytes.clear();

    std::error_code size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        pack.issues.push_back(TextureIssue{0, "failed to read file size"});
        return pack;
    }
    pack.file_size = file_size;
    if (file_size > limits.max_file_size ||
        file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::streamsize>::max())) {
        std::ostringstream text;
        text << "file is " << file_size << " bytes; limit is "
             << limits.max_file_size;
        pack.issues.push_back(TextureIssue{0, text.str()});
        return pack;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        pack.issues.push_back(TextureIssue{0, "failed to open file"});
        return pack;
    }
    try {
        bytes.resize(static_cast<std::size_t>(file_size));
    } catch (const std::exception&) {
        pack.issues.push_back(TextureIssue{0, "out of memory while reading file"});
        return pack;
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        pack.issues.push_back(TextureIssue{0, "failed to read whole file"});
        return pack;
    }
    return parse_texture_pack(bytes.data(), bytes.size(), limits);
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
    std::string& error) {
    payload.clear();
    if (!texture.valid) {
        error = "texture " + std::to_string(texture.index) + " is not valid";
        return false;
    }
    if (data == nullptr || texture.blob_offset > size ||
        texture.blob_size > size - texture.blob_offset) {
        error = "texture blob is outside the input buffer";
        return false;
    }
    if (!jdlz_decompress(data + texture.blob_offset, texture.blob_size,
                         texture.data_size, payload, unused_input, error)) {
        return false;
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
    std::string& error) {
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

bool write_tga(
    std::ostream& output,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<unsigned char>& rgba,
    std::string& error) {
    if (width == 0 || height == 0 || width > 0xFFFFU || height > 0xFFFFU) {
        error = "TGA dimensions must be in 1..65535";
        return false;
    }
    if (rgba.size() != std::size_t{width} * height * 4U) {
        error = "TGA pixel buffer size does not match the dimensions";
        return false;
    }

    std::array<unsigned char, 18> header{};
    header[2] = 2; // uncompressed true-color
    put_u16(&header[12], width);
    put_u16(&header[14], height);
    header[16] = 32;
    header[17] = 0x28; // 8 alpha bits, top-left origin
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));

    std::vector<unsigned char> row(std::size_t{width} * 4U);
    for (std::size_t y = 0; y < height; ++y) {
        const unsigned char* source = &rgba[y * width * 4U];
        for (std::size_t x = 0; x < width; ++x) {
            row[x * 4U + 0] = source[x * 4U + 2];
            row[x * 4U + 1] = source[x * 4U + 1];
            row[x * 4U + 2] = source[x * 4U + 0];
            row[x * 4U + 3] = source[x * 4U + 3];
        }
        output.write(reinterpret_cast<const char*>(row.data()),
                     static_cast<std::streamsize>(row.size()));
    }
    output.flush();
    if (!output) {
        error = "failed to write TGA output";
        return false;
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

namespace {

namespace fs = std::filesystem;

bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code error;
    if (fs::equivalent(a, b, error) && !error) {
        return true;
    }
    const fs::path abs_a = fs::absolute(a, error).lexically_normal();
    if (error) {
        return false;
    }
    const fs::path abs_b = fs::absolute(b, error).lexically_normal();
    return !error && abs_a == abs_b;
}

// True unless the path is known not to exist; paths that cannot be checked
// count as existing so that they are never overwritten.
bool path_exists(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    return status.type() != fs::file_type::not_found;
}

void remove_quietly(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
}

bool write_one(
    const std::vector<unsigned char>& bytes,
    const TextureRecord& texture,
    const fs::path& target,
    ExportFormat format,
    ExportedTexture& exported,
    std::string& error) {
    std::vector<unsigned char> payload;
    std::size_t unused = 0;
    if (!texture_payload(bytes.data(), bytes.size(), texture, payload, unused,
                         error)) {
        return false;
    }

    fs::path partial = target;
    partial += ".partial";
    if (path_exists(partial)) {
        error = "temporary file already exists: " + partial.filename().string();
        return false;
    }
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "failed to open " + partial.filename().string();
        return false;
    }

    bool written = false;
    if (format == ExportFormat::dds) {
        written = write_dds(output, texture.format, texture.width,
                            texture.height, texture.mip_count, payload.data(),
                            payload.size(), error);
    } else {
        std::uint64_t top = 0;
        std::vector<unsigned char> rgba;
        written = level_size(texture.format, texture.width, texture.height, top) &&
                  top <= payload.size() &&
                  decode_level(texture.format, texture.width, texture.height,
                               payload.data(), static_cast<std::size_t>(top),
                               rgba, error) &&
                  write_tga(output, texture.width, texture.height, rgba, error);
    }
    output.close();
    if (!written || !output) {
        if (error.empty()) {
            error = "failed to write " + partial.filename().string();
        }
        remove_quietly(partial);
        return false;
    }

    std::error_code rename_error;
    fs::rename(partial, target, rename_error);
    if (rename_error) {
        error = "failed to rename " + partial.filename().string();
        remove_quietly(partial);
        return false;
    }
    exported.index = texture.index;
    exported.path = target;
    exported.jdlz_unused_bytes = unused;
    return true;
}

} // namespace

bool export_textures(
    const std::vector<unsigned char>& bytes,
    const TexturePack& pack,
    const std::filesystem::path& input,
    const std::filesystem::path& output_dir,
    ExportFormat format,
    TextureExportResult& result) {
    result = TextureExportResult{};
    if (same_file(input, output_dir)) {
        result.error = "output directory must differ from the input file";
        return false;
    }

    // Plan every target first so that nothing is written if any name
    // collides with an existing file or with the input.
    const char* extension = format == ExportFormat::dds ? ".dds" : ".tga";
    std::vector<std::pair<const TextureRecord*, fs::path>> plan;
    for (const TextureRecord& texture : pack.textures) {
        if (!texture.valid) {
            result.skipped.push_back(texture.index);
            continue;
        }
        const fs::path target =
            output_dir / (texture_file_stem(texture) + extension);
        if (path_exists(target) || same_file(input, target)) {
            result.error = "refusing to overwrite " + target.filename().string();
            return false;
        }
        for (const auto& planned : plan) {
            if (planned.second == target) {
                result.error = "duplicate output name " +
                               target.filename().string();
                return false;
            }
        }
        plan.emplace_back(&texture, target);
    }

    std::error_code error;
    fs::create_directories(output_dir, error);
    if (error || !fs::is_directory(output_dir, error)) {
        result.error = "output path is not a usable directory";
        return false;
    }

    for (const auto& [texture, target] : plan) {
        ExportedTexture exported;
        std::string message;
        if (!write_one(bytes, *texture, target, format, exported, message)) {
            result.error = "texture " + std::to_string(texture->index) + ": " +
                           message + "; removed " +
                           std::to_string(result.files.size()) +
                           " files written by this export";
            for (const ExportedTexture& file : result.files) {
                remove_quietly(file.path);
            }
            result.files.clear();
            return false;
        }
        result.files.push_back(std::move(exported));
    }
    return true;
}

} // namespace nfsu
