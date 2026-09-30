#include "nfsu/geometry.h"

#include "nfsu/chunk.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <sstream>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float32 required");
static_assert(sizeof(float) == 4, "32-bit float required");

constexpr std::uint64_t kHeaderSize = 8;

static_assert(kVertexCountOffset + 4 <= kGeometryMetadataSize,
              "counts must lie inside the metadata record");

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

std::int32_t read_i32_le(const unsigned char* bytes) {
    const std::uint32_t bits = read_u32_le(bytes);
    std::int32_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

float read_f32_le(const unsigned char* bytes) {
    const std::uint32_t bits = read_u32_le(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
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

// Rounds value up to a multiple of alignment (alignment > 0).
bool align_up(std::uint64_t value, std::uint64_t alignment, std::uint64_t& out) {
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0) {
        out = value;
        return true;
    }
    return checked_add(value, alignment - remainder, out);
}

std::string hex_byte(unsigned char value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "0x";
    text.push_back(digits[(value >> 4U) & 0x0FU]);
    text.push_back(digits[value & 0x0FU]);
    return text;
}

std::string chunk_label(const RawChunk& chunk) {
    std::ostringstream text;
    text << "chunk " << format_chunk_id(chunk.id) << " at offset "
         << chunk.offset;
    return text.str();
}

class GeometryParser {
public:
    GeometryParser(
        const unsigned char* data,
        std::uint64_t size,
        const GeometryLimits& limits,
        GeometryReport& report)
        : data_(data), size_(size), limits_(limits), report_(report) {}

    void run() {
        if (size_ < kHeaderSize) {
            add_issue(0, "file is smaller than a chunk header (8 bytes)");
            return;
        }

        std::vector<RawChunk> top;
        // Chunks listed before a structural error are fully in bounds, so
        // they are still processed; the error itself is already reported.
        const bool top_structure_ok = list_children(0, size_, "top level", top);

        bool found_root = false;
        bool directories_ok = top_structure_ok;
        for (const RawChunk& chunk : top) {
            if (chunk.id == kGeometryRootId) {
                found_root = true;
                if (!parse_root(chunk)) {
                    directories_ok = false;
                }
            }
        }
        if (!found_root) {
            add_issue(0, "no geometry root chunk " +
                             format_chunk_id(kGeometryRootId) +
                             " found at top level");
        }

        report_.mesh_directory_valid = directories_ok && !report_.meshes.empty();
        for (const Mesh& mesh : report_.meshes) {
            report_.mesh_directory_valid =
                report_.mesh_directory_valid && mesh.has_directory_entry;
        }
    }

private:
    void add_issue(std::uint64_t offset, std::string message) {
        report_.issues.push_back(GeometryIssue{offset, std::move(message)});
    }

    // Lists the direct children of [begin, end). Returns false after
    // reporting an issue if the sequence is malformed; out then holds the
    // chunks that were read before the error.
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
                text << context << ": " << chunk_label(chunk)
                     << " declares " << chunk.payload_size
                     << " payload bytes but only "
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

    bool parse_root(const RawChunk& root) {
        std::vector<RawChunk> children;
        const bool root_structure_ok = list_children(
            root.payload_offset,
            root.payload_offset + root.payload_size,
            "geometry root at offset " + std::to_string(root.offset),
            children);

        const std::size_t mesh_begin = report_.meshes.size();
        std::vector<RawChunk> hash_tables;
        std::vector<RawChunk> directories;
        std::size_t header_count = 0;
        bool header_structure_ok = root_structure_ok;
        bool mesh_limit_reported = false;

        for (const RawChunk& chunk : children) {
            if (chunk.id == kGeometryHeaderId) {
                ++header_count;
                std::vector<RawChunk> header_children;
                if (!list_children(chunk.payload_offset,
                                   chunk.payload_offset + chunk.payload_size,
                                   "geometry header at offset " +
                                       std::to_string(chunk.offset),
                                   header_children)) {
                    header_structure_ok = false;
                }
                for (const RawChunk& child : header_children) {
                    if (child.id == kMeshHashTableId) {
                        hash_tables.push_back(child);
                    } else if (child.id == kMeshDirectoryId) {
                        directories.push_back(child);
                    }
                }
                continue;
            }
            if (chunk.id != kMeshContainerId) {
                continue;
            }
            if (report_.meshes.size() >= limits_.max_meshes) {
                if (!mesh_limit_reported) {
                    std::ostringstream text;
                    text << "mesh limit " << limits_.max_meshes
                         << " exceeded; remaining meshes ignored";
                    add_issue(chunk.offset, text.str());
                    mesh_limit_reported = true;
                }
                header_structure_ok = false;
                continue;
            }
            parse_mesh(chunk);
        }
        return parse_mesh_directory(root, mesh_begin, report_.meshes.size(),
                                    hash_tables, directories, header_count,
                                    header_structure_ok);
    }

    bool parse_mesh_directory(const RawChunk& owning_root,
                              std::size_t mesh_begin,
                              std::size_t mesh_end,
                              const std::vector<RawChunk>& hash_tables,
                              const std::vector<RawChunk>& directories,
                              std::size_t header_count,
                              bool structure_ok) {
        if (header_count == 0 && hash_tables.empty() && directories.empty()) {
            return structure_ok;
        }
        bool valid = structure_ok;
        if (header_count != 1) {
            add_issue(owning_root.offset, "expected exactly one " +
                             format_chunk_id(kGeometryHeaderId) +
                             " geometry header, found " +
                             std::to_string(header_count));
            valid = false;
        }
        if (hash_tables.size() != 1) {
            add_issue(owning_root.offset, "expected exactly one " +
                             format_chunk_id(kMeshHashTableId) +
                             " mesh hash table, found " +
                             std::to_string(hash_tables.size()));
            valid = false;
        }
        if (directories.size() != 1) {
            add_issue(owning_root.offset, "expected exactly one " +
                             format_chunk_id(kMeshDirectoryId) +
                             " mesh directory, found " +
                             std::to_string(directories.size()));
            valid = false;
        }
        if (hash_tables.size() != 1 || directories.size() != 1) {
            return false;
        }

        const RawChunk& hashes = hash_tables.front();
        const RawChunk& directory = directories.front();
        if (hashes.payload_size % kMeshHashRecordSize != 0) {
            add_issue(hashes.offset, chunk_label(hashes) +
                                         " payload is not a whole number of 8-byte records");
            valid = false;
        }
        if (directory.payload_size % kMeshDirectoryRecordSize != 0) {
            add_issue(directory.offset,
                      chunk_label(directory) +
                          " payload is not a whole number of 20-byte records");
            valid = false;
        }
        if (!valid) {
            return false;
        }

        const std::size_t hash_count = static_cast<std::size_t>(
            hashes.payload_size / kMeshHashRecordSize);
        const std::size_t directory_count = static_cast<std::size_t>(
            directory.payload_size / kMeshDirectoryRecordSize);
        if (hash_count != directory_count) {
            add_issue(directory.offset,
                      "mesh directory record count " +
                          std::to_string(directory_count) +
                          " does not match mesh hash table record count " +
                          std::to_string(hash_count));
            valid = false;
        }
        const std::size_t root_mesh_count = mesh_end - mesh_begin;
        if (directory_count != root_mesh_count) {
            add_issue(directory.offset,
                      "mesh directory record count " +
                          std::to_string(directory_count) +
                          " does not match geometry root mesh count " +
                          std::to_string(root_mesh_count));
            valid = false;
        }
        if (!valid) {
            return false;
        }

        std::map<std::uint64_t, std::size_t> mesh_by_offset;
        for (std::size_t i = mesh_begin; i < mesh_end; ++i) {
            mesh_by_offset.emplace(report_.meshes[i].offset, i);
        }
        struct Entry {
            std::uint32_t hash = 0;
            std::uint64_t offset = 0;
            std::uint64_t size = 0;
            std::size_t mesh = 0;
        };
        std::vector<Entry> entries;
        entries.reserve(directory_count);
        std::vector<bool> covered(root_mesh_count, false);
        const std::uint64_t root_end =
            owning_root.payload_offset + owning_root.payload_size;
        for (std::size_t i = 0; i < directory_count; ++i) {
            const std::uint64_t hash_at =
                hashes.payload_offset + i * kMeshHashRecordSize;
            const std::uint64_t entry_at =
                directory.payload_offset + i * kMeshDirectoryRecordSize;
            const std::uint32_t hash = read_u32_le(data_ + hash_at);
            const std::uint32_t directory_hash = read_u32_le(data_ + entry_at);
            const std::uint64_t offset = read_u32_le(data_ + entry_at + 4);
            const std::uint64_t total_size = read_u32_le(data_ + entry_at + 8);
            if (read_u32_le(data_ + hash_at + 4) != 0) {
                add_issue(hash_at + 4,
                          "mesh hash table record " + std::to_string(i) +
                              " has a nonzero reserved word");
                valid = false;
            }
            if (read_u32_le(data_ + entry_at + 12) != 0 ||
                read_u32_le(data_ + entry_at + 16) != 0) {
                add_issue(entry_at + 12,
                          "mesh directory record " + std::to_string(i) +
                              " has a nonzero reserved word");
                valid = false;
            }
            if (hash != directory_hash) {
                add_issue(entry_at,
                          "mesh directory record " + std::to_string(i) +
                              " hash does not match the mesh hash table");
                valid = false;
            }
            if (total_size < kHeaderSize || offset > size_ ||
                total_size > size_ - offset) {
                add_issue(entry_at + 4,
                          "mesh directory record " + std::to_string(i) +
                              " range is outside the file");
                valid = false;
                continue;
            }
            if (offset < owning_root.payload_offset || offset > root_end ||
                total_size > root_end - offset) {
                add_issue(entry_at + 4,
                          "mesh directory record " + std::to_string(i) +
                              " range is outside its geometry root");
                valid = false;
                continue;
            }
            if (read_u32_le(data_ + offset) != kMeshContainerId) {
                add_issue(entry_at + 4,
                          "mesh directory record " + std::to_string(i) +
                              " does not point to a mesh container");
                valid = false;
                continue;
            }
            const std::uint64_t actual_size =
                kHeaderSize + read_u32_le(data_ + offset + 4);
            if (actual_size != total_size) {
                add_issue(entry_at + 8,
                          "mesh directory record " + std::to_string(i) +
                              " size does not match the mesh container");
                valid = false;
                continue;
            }
            const auto found = mesh_by_offset.find(offset);
            if (found == mesh_by_offset.end()) {
                add_issue(entry_at + 4,
                          "mesh directory record " + std::to_string(i) +
                              " points to an unparsed mesh container");
                valid = false;
                continue;
            }
            const std::size_t local_mesh = found->second - mesh_begin;
            if (covered[local_mesh]) {
                add_issue(entry_at + 4,
                          "mesh directory records point to mesh " +
                              std::to_string(found->second) + " more than once");
                valid = false;
                continue;
            }
            covered[local_mesh] = true;
            entries.push_back(Entry{hash, offset, total_size, found->second});
        }
        for (std::size_t i = 0; i < covered.size(); ++i) {
            if (!covered[i]) {
                const std::size_t mesh = mesh_begin + i;
                add_issue(report_.meshes[mesh].offset,
                          "mesh " + std::to_string(mesh) +
                              " has no mesh directory record");
                valid = false;
            }
        }
        if (!valid) {
            return false;
        }
        for (const Entry& entry : entries) {
            Mesh& mesh = report_.meshes[entry.mesh];
            mesh.directory_hash = entry.hash;
            mesh.directory_offset = entry.offset;
            mesh.directory_size = entry.size;
            mesh.has_directory_entry = true;
        }
        return true;
    }

    std::string mesh_context(const Mesh& mesh) const {
        std::ostringstream text;
        text << "mesh " << mesh.index;
        if (!mesh.name.empty()) {
            text << " '" << mesh.name << "'";
        }
        text << " (container at offset " << mesh.offset << ")";
        return text.str();
    }

    // A skipped mesh must carry no decoded data at all. The marker chunk is
    // read before the counts, the buffers and the materials (see
    // parse_mesh_contents), so a mesh can reach a later failure with its
    // markers already decoded; retail CIVIC mesh 286
    // 'CIVIC_KIT13_REAR_BUMPER_A' is exactly that case, failing material
    // validation after two markers parsed. The marker offsets and the
    // has_marker_chunk flag are reset with the records, unlike the other
    // record offsets, because they describe records this mesh no longer
    // reports rather than locating where it went wrong.
    static void clear_decoded_data(Mesh& mesh) {
        mesh.vertices.clear();
        mesh.triangles.clear();
        mesh.texture_hashes.clear();
        mesh.material_key_hashes.clear();
        mesh.materials.clear();
        mesh.markers.clear();
        mesh.marker_data_offset = 0;
        mesh.marker_prefix_bytes = 0;
        mesh.has_marker_chunk = false;
    }

    bool fail(Mesh& mesh, std::uint64_t offset, const std::string& message) {
        add_issue(offset, mesh_context(mesh) + ": " + message);
        mesh.valid = false;
        clear_decoded_data(mesh);
        return false;
    }

    bool find_single(
        Mesh& mesh,
        const std::vector<RawChunk>& chunks,
        std::uint32_t id,
        std::uint64_t parent_offset,
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
            text << "expected exactly one " << format_chunk_id(id)
                 << " chunk, found " << count;
            return fail(mesh, parent_offset, text.str());
        }
        return true;
    }

    // Returns false only when the id occurs more than once. `found` stays
    // false when it does not occur at all, which is the normal case for the
    // optional marker chunk.
    bool find_optional(
        Mesh& mesh,
        const std::vector<RawChunk>& chunks,
        std::uint32_t id,
        std::uint64_t parent_offset,
        RawChunk& out,
        bool& found) {
        std::size_t count = 0;
        for (const RawChunk& chunk : chunks) {
            if (chunk.id == id) {
                if (count == 0) {
                    out = chunk;
                }
                ++count;
            }
        }
        if (count > 1) {
            std::ostringstream text;
            text << "expected at most one " << format_chunk_id(id)
                 << " chunk, found " << count;
            return fail(mesh, parent_offset, text.str());
        }
        found = count == 1;
        return true;
    }

    // 0x0013401A. The game aligns the payload up to 16 bytes, stores that
    // address as the marker table and derives the record count as
    // (payload_size - filler) / 80 into a byte (0x005400EA..0x0054010E).
    bool parse_markers(Mesh& mesh, const RawChunk& list) {
        std::uint64_t data_offset = 0;
        if (!align_up(list.payload_offset, kMarkerDataAlignment, data_offset)) {
            return fail(mesh, list.offset,
                        chunk_label(list) + ": aligned offset overflows");
        }
        const std::uint64_t prefix = data_offset - list.payload_offset;
        if (list.payload_size < prefix) {
            std::ostringstream text;
            text << chunk_label(list) << " payload is " << list.payload_size
                 << " bytes, fewer than the " << prefix
                 << " needed to reach the " << kMarkerDataAlignment
                 << "-byte aligned file offset " << data_offset;
            return fail(mesh, list.offset, text.str());
        }
        const std::uint64_t record_bytes = list.payload_size - prefix;
        if (record_bytes % kMarkerRecordSize != 0) {
            std::ostringstream text;
            text << chunk_label(list) << " holds " << record_bytes
                 << " bytes after " << prefix << " of 0x11, which is not a "
                 << "multiple of the " << kMarkerRecordSize
                 << "-byte marker record";
            return fail(mesh, list.offset, text.str());
        }
        const unsigned char* payload = data_ + list.payload_offset;
        for (std::uint64_t i = 0; i < prefix; ++i) {
            if (payload[i] != kAlignmentByte) {
                std::ostringstream text;
                text << chunk_label(list) << " alignment prefix byte " << i
                     << " (file offset " << (list.payload_offset + i)
                     << ") is " << hex_byte(payload[i]) << ", expected 0x11";
                return fail(mesh, list.offset, text.str());
            }
        }
        const std::uint64_t count = record_bytes / kMarkerRecordSize;
        if (count > kMaxMarkersPerMesh) {
            std::ostringstream text;
            text << chunk_label(list) << " declares " << count
                 << " markers; the game stores the count in a byte and reads it"
                 << " back sign-extended, so at most " << kMaxMarkersPerMesh
                 << " are reachable";
            return fail(mesh, list.offset, text.str());
        }

        mesh.has_marker_chunk = true;
        mesh.marker_data_offset = data_offset;
        mesh.marker_prefix_bytes = prefix;
        mesh.markers.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const unsigned char* record =
                data_ + data_offset + i * kMarkerRecordSize;
            MeshMarker marker;
            marker.offset = data_offset + i * kMarkerRecordSize;
            marker.name_hash = read_u32_le(record);
            for (std::size_t w = 0; w < marker.reserved.size(); ++w) {
                marker.reserved[w] = read_u32_le(record + 4 + 4 * w);
            }
            for (std::size_t e = 0; e < 16; ++e) {
                marker.matrix[e] = read_f32_le(record + 16 + 4 * e);
                if (!std::isfinite(marker.matrix[e])) {
                    std::ostringstream text;
                    text << chunk_label(list) << " marker " << i
                         << " matrix element " << e << " is not finite";
                    return fail(mesh, marker.offset, text.str());
                }
            }
            mesh.markers.push_back(marker);
        }
        return true;
    }

    void parse_mesh(const RawChunk& container) {
        Mesh mesh;
        mesh.index = report_.meshes.size();
        mesh.offset = container.offset;
        if (!parse_mesh_contents(container, mesh)) {
            // Every failure path runs through fail(), which has already done
            // this. Repeating it here makes "an invalid mesh holds no decoded
            // data" an invariant of report_.meshes rather than of each call
            // site, so a future check added ahead of a fail() cannot leak
            // half-decoded state to a consumer.
            mesh.valid = false;
            clear_decoded_data(mesh);
        }
        if (limits_.retain_mesh && !limits_.retain_mesh(mesh)) {
            mesh.valid = false;
            clear_decoded_data(mesh);
            std::vector<MeshVertex>().swap(mesh.vertices);
            std::vector<MeshTriangle>().swap(mesh.triangles);
            std::vector<std::uint32_t>().swap(mesh.texture_hashes);
            std::vector<std::uint32_t>().swap(mesh.material_key_hashes);
            std::vector<MeshMaterial>().swap(mesh.materials);
            std::vector<MeshMarker>().swap(mesh.markers);
        }
        report_.meshes.push_back(std::move(mesh));
    }

    bool parse_mesh_contents(const RawChunk& container, Mesh& mesh) {
        std::vector<RawChunk> children;
        const bool structure_ok = list_children(
            container.payload_offset,
            container.payload_offset + container.payload_size,
            mesh_context(mesh),
            children);

        RawChunk metadata;
        if (!find_single(mesh, children, kMeshMetadataId, container.offset,
                         metadata) ||
            !parse_name(mesh, metadata)) {
            return false;
        }
        if (!structure_ok) {
            return fail(mesh, container.offset,
                        "mesh container is malformed; mesh skipped");
        }

        RawChunk geometry;
        if (!find_single(mesh, children, kGeometryContainerId, container.offset,
                         geometry)) {
            return false;
        }

        std::vector<RawChunk> parts;
        if (!list_children(
                geometry.payload_offset,
                geometry.payload_offset + geometry.payload_size,
                mesh_context(mesh) + " geometry container",
                parts)) {
            return fail(mesh, geometry.offset,
                        "geometry container is malformed; mesh skipped");
        }

        // Remaining unknown chunks are intentionally skipped.
        RawChunk texture_list;
        RawChunk material_key_list;
        RawChunk geometry_metadata;
        RawChunk vertex_buffer;
        RawChunk material_buffer;
        RawChunk face_buffer;
        if (!find_single(mesh, children, kTextureListId, container.offset,
                         texture_list) ||
            !find_single(mesh, children, kMaterialKeyListId, container.offset,
                         material_key_list) ||
            !find_single(mesh, parts, kGeometryMetadataId, geometry.offset,
                         geometry_metadata) ||
            !find_single(mesh, parts, kVertexBufferId, geometry.offset,
                         vertex_buffer) ||
            !find_single(mesh, parts, kMaterialBufferId, geometry.offset,
                         material_buffer) ||
            !find_single(mesh, parts, kFaceBufferId, geometry.offset,
                         face_buffer)) {
            return false;
        }

        RawChunk marker_list;
        bool has_markers = false;
        if (!find_optional(mesh, children, kMarkerListId, container.offset,
                           marker_list, has_markers)) {
            return false;
        }
        if (has_markers && !parse_markers(mesh, marker_list)) {
            return false;
        }

        if (!parse_counts(mesh, geometry_metadata) ||
            !parse_vertices(mesh, vertex_buffer) ||
            !parse_faces(mesh, face_buffer) ||
            !validate_indices(mesh, face_buffer) ||
            !parse_texture_list(mesh, metadata, texture_list) ||
            !parse_material_key_list(mesh, metadata, material_key_list) ||
            !parse_materials(mesh, material_buffer)) {
            return false;
        }

        mesh.valid = true;
        return true;
    }

    bool parse_name(Mesh& mesh, const RawChunk& metadata) {
        if (metadata.payload_size < kMeshNameSize) {
            std::ostringstream text;
            text << chunk_label(metadata) << " payload is "
                 << metadata.payload_size << " bytes; need at least "
                 << kMeshNameSize << " for the mesh name";
            return fail(mesh, metadata.offset, text.str());
        }

        const unsigned char* name = data_ + metadata.payload_offset +
                                    metadata.payload_size - kMeshNameSize;
        std::size_t length = 0;
        while (length < kMeshNameSize && name[length] != 0) {
            ++length;
        }
        if (length == kMeshNameSize) {
            return fail(mesh, metadata.offset,
                        "mesh name in the final 28 bytes of " +
                            chunk_label(metadata) + " is not NUL-terminated");
        }
        for (std::size_t i = 0; i < length; ++i) {
            if (name[i] < 0x20U || name[i] > 0x7EU) {
                std::ostringstream text;
                text << "mesh name byte " << i << " is " << hex_byte(name[i])
                     << "; expected printable ASCII";
                return fail(mesh, metadata.offset, text.str());
            }
        }
        mesh.name.assign(reinterpret_cast<const char*>(name), length);
        return true;
    }

    bool parse_counts(Mesh& mesh, const RawChunk& metadata) {
        std::uint64_t record = 0;
        std::uint64_t prefix = 0;
        if (!locate_aligned(mesh, metadata, kGeometryMetadataAlignment,
                            kGeometryMetadataSize, 0,
                            "one 64-byte geometry metadata record", record,
                            prefix)) {
            return false;
        }
        mesh.metadata_record_offset = record;
        mesh.metadata_prefix_bytes = prefix;

        const unsigned char* payload = data_ + record;
        mesh.declared_face_count = read_i32_le(payload + kFaceCountOffset);
        mesh.declared_vertex_count = read_i32_le(payload + kVertexCountOffset);
        mesh.declared_material_count =
            read_i32_le(payload + kMaterialCountOffset);

        return check_count(mesh, metadata, "vertex", mesh.declared_vertex_count,
                           limits_.max_vertices_per_mesh) &&
               check_count(mesh, metadata, "face", mesh.declared_face_count,
                           limits_.max_faces_per_mesh) &&
               check_count(mesh, metadata, "material",
                           mesh.declared_material_count,
                           limits_.max_materials_per_mesh);
    }

    // 0x00134012: (u32 texture hash, u32 0) entries. The entry count is also
    // stored as a byte in the 0x00134011 payload; both must agree.
    bool parse_texture_list(
        Mesh& mesh,
        const RawChunk& mesh_metadata,
        const RawChunk& list) {
        std::ostringstream text;
        if (mesh_metadata.payload_size <= kTextureListCountOffset) {
            text << chunk_label(mesh_metadata) << " is too small to hold the"
                 << " texture count byte at payload offset "
                 << kTextureListCountOffset;
            return fail(mesh, mesh_metadata.offset, text.str());
        }
        const std::uint64_t declared =
            data_[mesh_metadata.payload_offset + kTextureListCountOffset];
        std::uint64_t expected = 0;
        if (!checked_mul(declared, kTextureListEntrySize, expected) ||
            list.payload_size != expected) {
            text << chunk_label(list) << " payload is " << list.payload_size
                 << " bytes but the texture count byte at file offset "
                 << (mesh_metadata.payload_offset + kTextureListCountOffset)
                 << " is " << declared << " (" << expected << " bytes)";
            return fail(mesh, list.offset, text.str());
        }
        mesh.texture_list_offset = list.payload_offset;
        const unsigned char* cursor = data_ + list.payload_offset;
        mesh.texture_hashes.reserve(static_cast<std::size_t>(declared));
        for (std::uint64_t i = 0; i < declared;
             ++i, cursor += kTextureListEntrySize) {
            if (read_u32_le(cursor + 4) != 0U) {
                text << chunk_label(list) << " entry " << i
                     << " has a non-zero second word at file offset "
                     << (list.payload_offset + i * kTextureListEntrySize + 4);
                return fail(mesh, list.offset, text.str());
            }
            mesh.texture_hashes.push_back(read_u32_le(cursor));
        }
        return true;
    }

    // 0x00134013: (u32 hash, u32 0) entries. Material record byte +32
    // selects one entry; 0xFF means that the material has no entry.
    bool parse_material_key_list(
        Mesh& mesh,
        const RawChunk& mesh_metadata,
        const RawChunk& list) {
        std::ostringstream text;
        if (mesh_metadata.payload_size <= kMaterialKeyListCountOffset) {
            text << chunk_label(mesh_metadata) << " is too small to hold the"
                 << " material-key count byte at payload offset "
                 << kMaterialKeyListCountOffset;
            return fail(mesh, mesh_metadata.offset, text.str());
        }
        const std::uint64_t declared =
            data_[mesh_metadata.payload_offset + kMaterialKeyListCountOffset];
        if (list.payload_size % kMaterialKeyListEntrySize != 0) {
            text << chunk_label(list) << " payload is " << list.payload_size
                 << " bytes, not a multiple of " << kMaterialKeyListEntrySize;
            return fail(mesh, list.offset, text.str());
        }
        std::uint64_t expected = 0;
        if (!checked_mul(declared, kMaterialKeyListEntrySize, expected) ||
            list.payload_size != expected) {
            text << chunk_label(list) << " payload is " << list.payload_size
                 << " bytes but the material-key count byte at file offset "
                 << (mesh_metadata.payload_offset +
                     kMaterialKeyListCountOffset)
                 << " is " << declared << " (" << expected << " bytes)";
            return fail(mesh, list.offset, text.str());
        }
        mesh.material_key_list_offset = list.payload_offset;
        const unsigned char* cursor = data_ + list.payload_offset;
        mesh.material_key_hashes.reserve(static_cast<std::size_t>(declared));
        for (std::uint64_t i = 0; i < declared;
             ++i, cursor += kMaterialKeyListEntrySize) {
            if (read_u32_le(cursor + 4) != 0U) {
                text << chunk_label(list) << " entry " << i
                     << " has a non-zero second word at file offset "
                     << (list.payload_offset +
                         i * kMaterialKeyListEntrySize + 4);
                return fail(mesh, list.offset, text.str());
            }
            mesh.material_key_hashes.push_back(read_u32_le(cursor));
        }
        return true;
    }

    // 0x00134B02: material records. Index runs must be contiguous from 0,
    // multiples of 3, cover every face exactly once, and reference an
    // existing texture list entry.
    bool parse_materials(Mesh& mesh, const RawChunk& buffer) {
        const auto count =
            static_cast<std::size_t>(mesh.declared_material_count);
        std::uint64_t data_size = 0;
        std::uint64_t data_offset = 0;
        std::uint64_t prefix = 0;
        if (!checked_mul(count, kMaterialRecordSize, data_size)) {
            return fail(mesh, buffer.offset, "material byte size overflows");
        }
        if (!locate_aligned(mesh, buffer, kMaterialDataAlignment, data_size, 0,
                            std::to_string(count) + " material records of 60 bytes",
                            data_offset, prefix)) {
            return false;
        }
        mesh.material_data_offset = data_offset;
        mesh.material_prefix_bytes = prefix;

        const std::uint64_t index_total =
            static_cast<std::uint64_t>(mesh.declared_face_count) * 3U;
        std::uint64_t next_index = 0;
        const unsigned char* cursor = data_ + data_offset;
        mesh.materials.resize(count);
        for (std::size_t i = 0; i < count; ++i, cursor += kMaterialRecordSize) {
            MeshMaterial& material = mesh.materials[i];
            material.offset = data_offset + i * kMaterialRecordSize;
            std::memcpy(material.raw, cursor, kMaterialRecordSize);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                material.bounds_min[axis] = read_f32_le(cursor + axis * 4);
                material.bounds_max[axis] = read_f32_le(cursor + 16 + axis * 4);
            }
            material.index_count = read_u32_le(cursor + 12);
            material.texture_slot = read_u32_le(cursor + 28);
            material.material_key_slot = read_u32_le(cursor + 32);
            material.first_index = read_u32_le(cursor + 52);
            material.flags = read_u32_le(cursor + 56);

            std::ostringstream text;
            text << "material record " << i << " at file offset "
                 << material.offset << " in " << chunk_label(buffer) << ": ";
            if (material.first_index != next_index) {
                text << "first index " << material.first_index
                     << " is not the running total " << next_index;
                return fail(mesh, buffer.offset, text.str());
            }
            if (material.index_count == 0 || material.index_count % 3U != 0 ||
                material.index_count > index_total - next_index) {
                text << "index count " << material.index_count
                     << " is zero, not a multiple of 3, or exceeds the "
                     << (index_total - next_index) << " remaining indices";
                return fail(mesh, buffer.offset, text.str());
            }
            if (material.texture_slot >= mesh.texture_hashes.size()) {
                text << "texture slot " << material.texture_slot
                     << " is outside the " << mesh.texture_hashes.size()
                     << "-entry texture list";
                return fail(mesh, buffer.offset, text.str());
            }
            if (material.material_key_slot != 0xFFU &&
                material.material_key_slot >= mesh.material_key_hashes.size()) {
                text << "material-key slot " << material.material_key_slot
                     << " is neither 0xFF nor inside the "
                     << mesh.material_key_hashes.size()
                     << "-entry material-key list";
                return fail(mesh, buffer.offset, text.str());
            }
            next_index += material.index_count;
        }
        if (next_index != index_total) {
            std::ostringstream text;
            text << chunk_label(buffer) << " material records cover "
                 << next_index << " indices; the face count needs "
                 << index_total;
            return fail(mesh, buffer.offset, text.str());
        }
        return true;
    }

    bool check_count(
        Mesh& mesh,
        const RawChunk& metadata,
        const char* what,
        std::int32_t count,
        std::int32_t limit) {
        std::ostringstream text;
        if (count <= 0) {
            text << what << " count " << count << " in " << chunk_label(metadata)
                 << " is not positive";
            return fail(mesh, metadata.offset, text.str());
        }
        if (count > limit) {
            text << what << " count " << count << " in " << chunk_label(metadata)
                 << " exceeds the limit of " << limit;
            return fail(mesh, metadata.offset, text.str());
        }
        return true;
    }

    // The data of a buffer starts at the first file offset inside its payload
    // that is a multiple of alignment. The bytes between the payload start
    // and that offset must all be 0x11. The payload must then hold exactly
    // data_size bytes of data followed by tail_size bytes of tail padding.
    // Positions are never inferred by scanning for 0x11 bytes.
    bool locate_aligned(
        Mesh& mesh,
        const RawChunk& buffer,
        std::uint64_t alignment,
        std::uint64_t data_size,
        std::uint64_t tail_size,
        const std::string& description,
        std::uint64_t& data_offset,
        std::uint64_t& prefix) {
        std::uint64_t expected = 0;
        if (!align_up(buffer.payload_offset, alignment, data_offset) ||
            !checked_add(data_size, tail_size, expected) ||
            !checked_add(expected, data_offset - buffer.payload_offset,
                         expected)) {
            return fail(mesh, buffer.offset,
                        chunk_label(buffer) + ": expected size overflows");
        }
        prefix = data_offset - buffer.payload_offset;

        if (buffer.payload_size != expected) {
            std::ostringstream text;
            text << chunk_label(buffer) << " payload is "
                 << buffer.payload_size << " bytes but " << expected
                 << " are expected: " << prefix
                 << " bytes of 0x11 up to the " << alignment
                 << "-byte aligned file offset " << data_offset << ", "
                 << description << " (" << data_size << " bytes)";
            if (tail_size != 0) {
                text << " and " << tail_size << " zero tail bytes";
            }
            text << "; the declared count may be wrong";
            return fail(mesh, buffer.offset, text.str());
        }

        const unsigned char* payload = data_ + buffer.payload_offset;
        for (std::uint64_t i = 0; i < prefix; ++i) {
            if (payload[i] != kAlignmentByte) {
                std::ostringstream text;
                text << chunk_label(buffer) << " alignment prefix byte " << i
                     << " (file offset " << (buffer.payload_offset + i)
                     << ") is " << hex_byte(payload[i]) << ", expected 0x11";
                return fail(mesh, buffer.offset, text.str());
            }
        }
        return true;
    }

    bool parse_vertices(Mesh& mesh, const RawChunk& buffer) {
        const auto count = static_cast<std::size_t>(mesh.declared_vertex_count);
        std::uint64_t data_size = 0;
        std::uint64_t data_offset = 0;
        std::uint64_t prefix = 0;
        if (!checked_mul(count, kVertexStride, data_size)) {
            return fail(mesh, buffer.offset, "vertex byte size overflows");
        }
        if (!locate_aligned(mesh, buffer, kVertexDataAlignment, data_size, 0,
                            std::to_string(count) + " vertices of 36 bytes",
                            data_offset, prefix)) {
            return false;
        }
        mesh.vertex_data_offset = data_offset;
        mesh.vertex_prefix_bytes = prefix;

        const unsigned char* cursor = data_ + data_offset;
        mesh.vertices.resize(count);
        for (std::size_t i = 0; i < count; ++i, cursor += kVertexStride) {
            MeshVertex& vertex = mesh.vertices[i];
            vertex.px = read_f32_le(cursor + 0);
            vertex.pz = read_f32_le(cursor + 4);
            vertex.py = read_f32_le(cursor + 8);
            vertex.nx = read_f32_le(cursor + 12);
            vertex.nz = read_f32_le(cursor + 16);
            vertex.ny = read_f32_le(cursor + 20);
            std::memcpy(vertex.color, cursor + 24, sizeof(vertex.color));
            vertex.u = read_f32_le(cursor + 28);
            vertex.v = read_f32_le(cursor + 32);

            const float values[] = {vertex.px, vertex.py, vertex.pz,
                                    vertex.nx, vertex.ny, vertex.nz,
                                    vertex.u,  vertex.v};
            for (float value : values) {
                if (!std::isfinite(value)) {
                    std::ostringstream text;
                    text << "vertex " << i << " in " << chunk_label(buffer)
                         << " has a non-finite float";
                    return fail(mesh, buffer.offset, text.str());
                }
            }
        }
        return true;
    }

    bool parse_faces(Mesh& mesh, const RawChunk& buffer) {
        const auto count = static_cast<std::size_t>(mesh.declared_face_count);
        std::uint64_t data_size = 0;
        std::uint64_t data_offset = 0;
        std::uint64_t prefix = 0;
        if (!checked_mul(count, kFaceStride, data_size)) {
            return fail(mesh, buffer.offset, "face byte size overflows");
        }
        // Face data is padded with zeros to a multiple of 4 bytes; with a
        // 6-byte stride that is 2 bytes for odd face counts, else none.
        const std::uint64_t remainder = data_size % kFaceTailAlignment;
        const std::uint64_t tail =
            remainder == 0 ? 0 : kFaceTailAlignment - remainder;
        if (!locate_aligned(mesh, buffer, kFaceDataAlignment, data_size, tail,
                            std::to_string(count) + " triangles of 6 bytes",
                            data_offset, prefix)) {
            return false;
        }

        const unsigned char* tail_bytes = data_ + data_offset + data_size;
        for (std::uint64_t i = 0; i < tail; ++i) {
            if (tail_bytes[i] != 0U) {
                std::ostringstream text;
                text << chunk_label(buffer) << " tail padding byte " << i
                     << " (file offset " << (data_offset + data_size + i)
                     << ") is " << hex_byte(tail_bytes[i]) << ", expected 0x00";
                return fail(mesh, buffer.offset, text.str());
            }
        }
        mesh.face_data_offset = data_offset;
        mesh.face_prefix_bytes = prefix;
        mesh.face_tail_bytes = tail;

        const unsigned char* cursor = data_ + data_offset;
        mesh.triangles.resize(count);
        for (std::size_t i = 0; i < count; ++i, cursor += kFaceStride) {
            MeshTriangle& triangle = mesh.triangles[i];
            triangle.a = read_u16_le(cursor + 0);
            triangle.b = read_u16_le(cursor + 2);
            triangle.c = read_u16_le(cursor + 4);
        }
        return true;
    }

    bool validate_indices(Mesh& mesh, const RawChunk& buffer) {
        const std::size_t vertex_count = mesh.vertices.size();
        std::size_t bad = 0;
        std::size_t first_triangle = 0;
        std::uint16_t first_value = 0;
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            const MeshTriangle& t = mesh.triangles[i];
            for (std::uint16_t index : {t.a, t.b, t.c}) {
                if (index >= vertex_count) {
                    if (bad == 0) {
                        first_triangle = i;
                        first_value = index;
                    }
                    ++bad;
                }
            }
        }
        if (bad != 0) {
            std::ostringstream text;
            text << bad << " face indices in " << chunk_label(buffer)
                 << " are out of range for " << vertex_count
                 << " vertices; first is triangle " << first_triangle
                 << " index " << first_value;
            return fail(mesh, buffer.offset, text.str());
        }
        return true;
    }

    const unsigned char* data_;
    std::uint64_t size_;
    const GeometryLimits& limits_;
    GeometryReport& report_;
    std::size_t chunk_count_ = 0;
};

// Returns -value, but +0 instead of -0 so that zero prints as "0".
float negate(float value) {
    return 0.0F - value;
}

void append_float(std::string& line, float value) {
    char buffer[64];
    const std::to_chars_result result =
        std::to_chars(buffer, buffer + sizeof(buffer), value);
    line.push_back(' ');
    line.append(buffer, result.ptr);
}

void append_index(std::string& line, std::uint64_t index) {
    const std::string text = std::to_string(index);
    line.push_back(' ');
    line += text;
    line.push_back('/');
    line += text;
    line.push_back('/');
    line += text;
}

} // namespace

std::size_t GeometryReport::valid_mesh_count() const noexcept {
    std::size_t count = 0;
    for (const Mesh& mesh : meshes) {
        if (mesh.valid) {
            ++count;
        }
    }
    return count;
}

GeometryReport parse_geometry(
    const unsigned char* data,
    std::size_t size,
    const GeometryLimits& limits) {
    GeometryReport report;
    report.file_size = size;
    if (data == nullptr && size != 0) {
        report.issues.push_back(GeometryIssue{0, "null input buffer"});
        return report;
    }
    if (size > limits.max_file_size) {
        std::ostringstream text;
        text << "buffer is " << size << " bytes; limit is "
             << limits.max_file_size;
        report.issues.push_back(GeometryIssue{0, text.str()});
        return report;
    }
    try {
        GeometryParser parser(data, size, limits, report);
        parser.run();
    } catch (const std::bad_alloc&) {
        report.issues.push_back(
            GeometryIssue{0, "out of memory while parsing geometry"});
    } catch (const std::exception& error) {
        report.issues.push_back(
            GeometryIssue{0, std::string("geometry parse failed: ") + error.what()});
    }
    return report;
}

GeometryReport load_geometry_file(
    const std::filesystem::path& path,
    const GeometryLimits& limits) {
    GeometryReport report;

    std::error_code size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        report.issues.push_back(GeometryIssue{0, "failed to read file size"});
        return report;
    }
    report.file_size = file_size;
    if (file_size > limits.max_file_size ||
        file_size > std::numeric_limits<std::size_t>::max() ||
        file_size > static_cast<std::uintmax_t>(
                        std::numeric_limits<std::streamsize>::max())) {
        std::ostringstream text;
        text << "file is " << file_size << " bytes; limit is "
             << limits.max_file_size;
        report.issues.push_back(GeometryIssue{0, text.str()});
        return report;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        report.issues.push_back(GeometryIssue{0, "failed to open file"});
        return report;
    }

    std::vector<unsigned char> bytes;
    try {
        bytes.resize(static_cast<std::size_t>(file_size));
    } catch (const std::exception&) {
        report.issues.push_back(
            GeometryIssue{0, "out of memory while reading file"});
        return report;
    }
    if (!bytes.empty() &&
        !stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        report.issues.push_back(GeometryIssue{0, "failed to read whole file"});
        return report;
    }

    return parse_geometry(bytes.data(), bytes.size(), limits);
}

std::string obj_object_name(const Mesh& mesh) {
    std::string index = std::to_string(mesh.index);
    if (index.size() < 4) {
        index.insert(0, 4 - index.size(), '0');
    }
    std::string name = "m" + index + "_";
    if (mesh.name.empty()) {
        return name + "unnamed";
    }
    for (char ch : mesh.name) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
                          ch == '.';
        name.push_back(safe ? ch : '_');
    }
    return name;
}

bool write_obj(
    std::ostream& output,
    const GeometryReport& report,
    ObjExportStats& stats,
    std::string& error) {
    stats = ObjExportStats{};

    // Validate everything first so that no partial OBJ is written for
    // inconsistent input.
    for (const Mesh& mesh : report.meshes) {
        if (!mesh.valid) {
            continue;
        }
        const std::size_t vertex_count = mesh.vertices.size();
        if (vertex_count == 0 || mesh.triangles.empty()) {
            error = "mesh " + std::to_string(mesh.index) +
                    " is marked valid but has no vertices or faces";
            return false;
        }
        for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
            const MeshTriangle& t = mesh.triangles[i];
            if (t.a >= vertex_count || t.b >= vertex_count ||
                t.c >= vertex_count) {
                error = "mesh " + std::to_string(mesh.index) + " triangle " +
                        std::to_string(i) + " references a vertex outside 0.." +
                        std::to_string(vertex_count - 1);
                return false;
            }
        }
    }

    // See docs/FORMATS.md: z is negated to convert the left-handed px, py, pz
    // frame to a right-handed one without mirroring the car. That reflection
    // alone turns the source front faces counter-clockwise, so the source
    // index order is kept.
    output << "# nfsu_probe diagnostic OBJ export\n"
           << "# axes: x = source px, y = source py, z = -source pz"
              " (source float order px, pz, py)\n"
           << "# winding: source triangle order (a, b, c) is kept\n"
           << "# uv: vt u v = raw source u v (origin not confirmed)\n";

    std::uint64_t base = 0;
    std::string line;
    for (const Mesh& mesh : report.meshes) {
        if (!mesh.valid) {
            continue;
        }
        const std::string name = obj_object_name(mesh);
        output << "o " << name << "\ng " << name << '\n';

        for (const MeshVertex& vertex : mesh.vertices) {
            line = "v";
            append_float(line, vertex.px);
            append_float(line, vertex.py);
            append_float(line, negate(vertex.pz));
            output << line << '\n';
        }
        for (const MeshVertex& vertex : mesh.vertices) {
            line = "vn";
            append_float(line, vertex.nx);
            append_float(line, vertex.ny);
            append_float(line, negate(vertex.nz));
            output << line << '\n';
        }
        for (const MeshVertex& vertex : mesh.vertices) {
            line = "vt";
            append_float(line, vertex.u);
            append_float(line, vertex.v);
            output << line << '\n';
        }

        for (const MeshTriangle& t : mesh.triangles) {
            std::uint64_t a = 0;
            std::uint64_t b = 0;
            std::uint64_t c = 0;
            if (!checked_add(base, std::uint64_t{t.a} + 1U, a) ||
                !checked_add(base, std::uint64_t{t.b} + 1U, b) ||
                !checked_add(base, std::uint64_t{t.c} + 1U, c)) {
                error = "OBJ index overflow";
                return false;
            }
            line = "f";
            append_index(line, a);
            append_index(line, b);
            append_index(line, c);
            output << line << '\n';
        }

        if (!checked_add(base, mesh.vertices.size(), base)) {
            error = "OBJ vertex offset overflow";
            return false;
        }
        ++stats.meshes;
        stats.vertices = base;
        stats.triangles += mesh.triangles.size();

        if (!output) {
            error = "failed to write OBJ output";
            return false;
        }
    }

    output.flush();
    if (!output) {
        error = "failed to write OBJ output";
        return false;
    }
    return true;
}

} // namespace nfsu
