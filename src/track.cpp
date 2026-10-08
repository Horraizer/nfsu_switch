#include "nfsu/track.h"

#include "nfsu/chunk.h"
#include "nfsu/geometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <limits>
#include <locale>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float32 required");
static_assert(sizeof(float) == 4, "32-bit float required");

constexpr std::uint64_t kHeaderSize = 8;

static_assert(kVertexCountOffset + 4 <= kGeometryMetadataSize,
              "counts must lie inside the 0x00134900 record");
static_assert(kVertexFormatOffset + 4 <= kGeometryMetadataSize,
              "the vertex format word must lie inside the 0x00134900 record");
static_assert(kMeshMetadataNameOffset + kMeshNameSize == kMeshMetadataRecordSize,
              "the mesh name ends the 0x00134011 record");

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

// Rounds value up to a multiple of alignment. Refuses alignment 0 instead
// of dividing by it; every call site passes a compile-time constant.
bool align_up(std::uint64_t value, std::uint64_t alignment, std::uint64_t& out) {
    if (alignment == 0) {
        return false;
    }
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0) {
        out = value;
        return true;
    }
    return checked_add(value, alignment - remainder, out);
}

// A diagnostic must not depend on the process-global locale: a grouping
// numpunct would otherwise write "16_777_215" into an issue message, so
// two runs over the same bytes could disagree byte for byte. Every issue
// text in this file is built through Message.
class Message {
public:
    Message() { stream_.imbue(std::locale::classic()); }

    template <typename T>
    Message& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    std::string str() const { return stream_.str(); }

private:
    std::ostringstream stream_;
};

std::string hex_byte(unsigned char value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "0x";
    text.push_back(digits[(value >> 4U) & 0x0FU]);
    text.push_back(digits[value & 0x0FU]);
    return text;
}

std::string hex_word(std::uint32_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text.push_back(digits[(value >> shift) & 0x0FU]);
    }
    return text;
}

// Absolute file offsets throughout: a region knows where it came from, so
// every diagnostic names an offset that can be looked at in the file.
struct RawChunk {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
};

std::string chunk_label(const RawChunk& chunk) {
    Message text;
    text << "chunk " << format_chunk_id(chunk.id) << " at offset "
         << chunk.offset;
    return text.str();
}

class TrackParser {
public:
    TrackParser(
        const unsigned char* data,
        std::uint64_t size,
        std::uint64_t base,
        const TrackLimits& limits,
        TrackRegionReport& report)
        : data_(data),
          size_(size),
          base_(base),
          end_(base + size),
          limits_(limits),
          report_(report) {}

    void run() {
        if (size_ < kHeaderSize) {
            Message text;
            text << "region is " << size_ << " bytes; a chunk header needs "
                 << kHeaderSize;
            add_issue(base_, text.str());
            return;
        }

        std::vector<RawChunk> top;
        // Chunks listed before a structural error are fully in bounds, so
        // they are still processed; the error itself is already reported.
        const bool top_structure_ok = list_children(base_, end_, "region", top);

        bool found_root = false;
        bool tables_ok = top_structure_ok;
        for (const RawChunk& chunk : top) {
            if (chunk.id == kGeometryRootId) {
                found_root = true;
                ++report_.cost.geometry_roots;
                if (!parse_root(chunk)) {
                    tables_ok = false;
                }
            }
        }
        if (!found_root) {
            add_issue(base_, "no geometry root chunk " +
                                 format_chunk_id(kGeometryRootId) +
                                 " in the region");
        }

        report_.mesh_table_valid = tables_ok && !report_.meshes.empty();
        for (const TrackMesh& mesh : report_.meshes) {
            report_.mesh_table_valid =
                report_.mesh_table_valid && mesh.has_hash_record;
        }
    }

private:
    const unsigned char* at(std::uint64_t absolute) const {
        return data_ + (absolute - base_);
    }

    void add_issue(std::uint64_t offset, std::string message) {
        report_.issues.push_back(TrackIssue{offset, std::move(message)});
    }

    // Lists the direct children of [begin, end), both absolute. Returns
    // false after reporting an issue if the sequence is malformed; out then
    // holds the chunks that were read before the error.
    bool list_children(
        std::uint64_t begin,
        std::uint64_t end,
        const std::string& context,
        std::vector<RawChunk>& out) {
        if (begin < base_ || begin > end || end > end_) {
            add_issue(begin, context + ": invalid chunk range");
            return false;
        }

        std::uint64_t offset = begin;
        while (offset < end) {
            if (end - offset < kHeaderSize) {
                Message text;
                text << context << ": " << (end - offset)
                     << " trailing bytes are smaller than a chunk header";
                add_issue(offset, text.str());
                return false;
            }
            if (report_.cost.chunks >= limits_.max_chunks) {
                Message text;
                text << context << ": chunk limit " << limits_.max_chunks
                     << " exceeded";
                add_issue(offset, text.str());
                return false;
            }

            RawChunk chunk;
            chunk.offset = offset;
            chunk.id = read_u32_le(at(offset));
            chunk.payload_size = read_u32_le(at(offset) + 4);
            chunk.payload_offset = offset + kHeaderSize;
            if (chunk.payload_size > end - chunk.payload_offset) {
                Message text;
                text << context << ": " << chunk_label(chunk) << " declares "
                     << chunk.payload_size << " payload bytes but only "
                     << (end - chunk.payload_offset) << " remain in its parent";
                add_issue(offset, text.str());
                return false;
            }

            ++report_.cost.chunks;
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
        const std::size_t root_index =
            static_cast<std::size_t>(report_.cost.geometry_roots - 1);
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
                    Message text;
                    text << "mesh limit " << limits_.max_meshes
                         << " exceeded; remaining meshes ignored";
                    add_issue(chunk.offset, text.str());
                    mesh_limit_reported = true;
                }
                header_structure_ok = false;
                continue;
            }
            parse_mesh(chunk, root_index);
        }
        return parse_mesh_table(root, mesh_begin, report_.meshes.size(),
                               hash_tables, directories, header_count,
                               header_structure_ok);
    }

    // A track geometry root indexes its meshes with 0x00134003 alone. Each
    // 8-byte record is (u32 mesh name hash, u32 0); the records are in
    // non-decreasing hash order and there is exactly one per mesh. Cars
    // additionally carry a 0x00134004 range directory; no track root in the
    // sampled installation does, and this decoder does not read one.
    bool parse_mesh_table(
        const RawChunk& owning_root,
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
            add_issue(owning_root.offset,
                      "expected exactly one " +
                          format_chunk_id(kGeometryHeaderId) +
                          " geometry header, found " +
                          std::to_string(header_count));
            valid = false;
        }
        if (hash_tables.size() != 1) {
            add_issue(owning_root.offset,
                      "expected exactly one " + format_chunk_id(kMeshHashTableId) +
                          " mesh hash table, found " +
                          std::to_string(hash_tables.size()));
            valid = false;
        }
        if (!directories.empty()) {
            add_issue(directories.front().offset,
                      "geometry root has " + std::to_string(directories.size()) +
                          " " + format_chunk_id(kMeshDirectoryId) +
                          " mesh directory chunks; a track root has none and"
                          " this decoder does not read one");
            valid = false;
        }
        if (hash_tables.size() != 1) {
            return false;
        }

        const RawChunk& hashes = hash_tables.front();
        if (hashes.payload_size % kMeshHashRecordSize != 0) {
            add_issue(hashes.offset,
                      chunk_label(hashes) +
                          " payload is not a whole number of 8-byte records");
            return false;
        }
        const std::size_t record_count =
            static_cast<std::size_t>(hashes.payload_size / kMeshHashRecordSize);
        const std::size_t root_mesh_count = mesh_end - mesh_begin;
        if (record_count != root_mesh_count) {
            add_issue(hashes.offset,
                      "mesh hash table holds " + std::to_string(record_count) +
                          " records but the geometry root holds " +
                          std::to_string(root_mesh_count) + " mesh containers");
            // Returning here, as the car parser does, keeps the per-record
            // loop below bounded by the mesh count and so by
            // limits_.max_meshes. record_count comes from a 32-bit payload
            // size alone, so a table that claims millions of records would
            // otherwise be allowed to raise millions of issues.
            return false;
        }

        std::vector<std::uint32_t> table;
        table.reserve(record_count);
        std::uint32_t previous = 0;
        for (std::size_t i = 0; i < record_count; ++i) {
            const std::uint64_t record_at =
                hashes.payload_offset + i * kMeshHashRecordSize;
            const std::uint32_t hash = read_u32_le(at(record_at));
            if (read_u32_le(at(record_at) + 4) != 0U) {
                add_issue(record_at + 4,
                          "mesh hash table record " + std::to_string(i) +
                              " has a nonzero reserved word");
                valid = false;
            }
            if (i != 0 && hash < previous) {
                add_issue(record_at,
                          "mesh hash table record " + std::to_string(i) + " (" +
                              hex_word(hash) + ") is below record " +
                              std::to_string(i - 1) + " (" + hex_word(previous) +
                              "); the records must not decrease");
                valid = false;
            }
            previous = hash;
            table.push_back(hash);
        }
        if (!valid) {
            return false;
        }

        // Every mesh must have contributed a hash, and the two multisets
        // must be equal. Duplicate hashes occur in retail, so equal hashes
        // are matched in source order.
        std::vector<std::uint32_t> from_meshes;
        from_meshes.reserve(root_mesh_count);
        for (std::size_t i = mesh_begin; i < mesh_end; ++i) {
            if (!hash_known_[i]) {
                add_issue(report_.meshes[i].offset,
                          "mesh " + std::to_string(i) +
                              " has no readable " +
                              format_chunk_id(kMeshMetadataId) +
                              " name hash, so the mesh hash table cannot be"
                              " verified");
                return false;
            }
            from_meshes.push_back(report_.meshes[i].mesh_hash);
        }
        std::vector<std::uint32_t> sorted_table = table;
        std::vector<std::uint32_t> sorted_meshes = from_meshes;
        std::sort(sorted_table.begin(), sorted_table.end());
        std::sort(sorted_meshes.begin(), sorted_meshes.end());
        if (sorted_table != sorted_meshes) {
            add_issue(hashes.offset,
                      "the mesh hash table does not list exactly the " +
                          std::to_string(root_mesh_count) +
                          " mesh name hashes of this geometry root");
            return false;
        }

        // Assign record offsets: the k-th mesh with a hash takes the k-th
        // record with it, so the mapping is deterministic even with
        // duplicates.
        std::map<std::uint32_t, std::vector<std::uint64_t>> records;
        for (std::size_t i = 0; i < record_count; ++i) {
            records[table[i]].push_back(hashes.payload_offset +
                                        i * kMeshHashRecordSize);
        }
        std::map<std::uint32_t, std::size_t> used;
        for (std::size_t i = mesh_begin; i < mesh_end; ++i) {
            TrackMesh& mesh = report_.meshes[i];
            const auto found = records.find(mesh.mesh_hash);
            const std::size_t rank = used[mesh.mesh_hash]++;
            // The multiset comparison above already guarantees both, so a
            // failure here means that check was weakened, not that the
            // file is unusual.
            if (found == records.end() || rank >= found->second.size()) {
                add_issue(hashes.offset,
                          "mesh " + std::to_string(i) +
                              " has no mesh hash table record left to take");
                return false;
            }
            mesh.hash_record_offset = found->second[rank];
            mesh.has_hash_record = true;
        }
        return true;
    }

    std::string mesh_context(const TrackMesh& mesh) const {
        Message text;
        text << "mesh " << mesh.index;
        if (!mesh.name.empty()) {
            text << " '" << mesh.name << "'";
        }
        text << " (container at offset " << mesh.offset << ")";
        return text.str();
    }

    bool fail(TrackMesh& mesh, std::uint64_t offset, const std::string& message) {
        add_issue(offset, mesh_context(mesh) + ": " + message);
        clear_decoded_data(mesh);
        return false;
    }

    static void clear_decoded_data(TrackMesh& mesh) {
        mesh.valid = false;
        mesh.empty = false;
        mesh.vertices.clear();
        mesh.triangles.clear();
        mesh.texture_hashes.clear();
        mesh.material_key_hashes.clear();
        mesh.materials.clear();
    }

    bool find_single(
        TrackMesh& mesh,
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
            Message text;
            text << "expected exactly one " << format_chunk_id(id)
                 << " chunk, found " << count;
            return fail(mesh, parent_offset, text.str());
        }
        return true;
    }

    // 0 or 1 chunks with this id. present is set accordingly.
    bool find_optional(
        TrackMesh& mesh,
        const std::vector<RawChunk>& chunks,
        std::uint32_t id,
        std::uint64_t parent_offset,
        RawChunk& out,
        bool& present) {
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
            Message text;
            text << "expected at most one " << format_chunk_id(id)
                 << " chunk, found " << count;
            return fail(mesh, parent_offset, text.str());
        }
        present = count == 1;
        return true;
    }

    void parse_mesh(const RawChunk& container, std::size_t root_index) {
        TrackMesh mesh;
        mesh.index = report_.meshes.size();
        mesh.root_index = root_index;
        mesh.offset = container.offset;
        hash_known_.push_back(false);
        parse_mesh_contents(container, mesh);
        ++report_.cost.meshes;
        if (mesh.valid) {
            account(mesh);
        } else if (mesh.empty) {
            ++report_.cost.empty_meshes;
        }
        report_.meshes.push_back(std::move(mesh));
    }

    // Region totals and bounds. Called once per valid mesh, in source
    // order, so the result does not depend on container iteration order.
    void account(const TrackMesh& mesh) {
        TrackRegionCost& cost = report_.cost;
        ++cost.valid_meshes;
        const std::uint64_t vertices = mesh.vertices.size();
        const std::uint64_t triangles = mesh.triangles.size();
        cost.vertices += vertices;
        cost.triangles += triangles;
        cost.materials += mesh.materials.size();
        cost.source_vertex_bytes += vertices * kTrackVertexStride;
        cost.source_index_bytes += triangles * kFaceStride;
        cost.decoded_vertex_bytes += vertices * sizeof(TrackVertex);
        cost.decoded_index_bytes += triangles * sizeof(MeshTriangle);
        if (vertices > cost.largest_mesh_vertices) {
            cost.largest_mesh_vertices = vertices;
        }
        if (triangles > cost.largest_mesh_triangles) {
            cost.largest_mesh_triangles = triangles;
        }
        for (const TrackVertex& vertex : mesh.vertices) {
            const float position[3] = {vertex.px, vertex.py, vertex.pz};
            if (!cost.has_bounds) {
                cost.has_bounds = true;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    cost.bounds_min[axis] = position[axis];
                    cost.bounds_max[axis] = position[axis];
                }
                continue;
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (position[axis] < cost.bounds_min[axis]) {
                    cost.bounds_min[axis] = position[axis];
                }
                if (position[axis] > cost.bounds_max[axis]) {
                    cost.bounds_max[axis] = position[axis];
                }
            }
        }
        for (const MeshMaterial& material : mesh.materials) {
            if (material.texture_slot < mesh.texture_hashes.size()) {
                texture_hashes_.insert(
                    mesh.texture_hashes[material.texture_slot]);
            }
        }
        cost.distinct_texture_hashes = texture_hashes_.size();
    }

    bool parse_mesh_contents(const RawChunk& container, TrackMesh& mesh) {
        std::vector<RawChunk> children;
        const bool structure_ok = list_children(
            container.payload_offset,
            container.payload_offset + container.payload_size,
            mesh_context(mesh),
            children);

        RawChunk metadata;
        if (!find_single(mesh, children, kMeshMetadataId, container.offset,
                         metadata) ||
            !parse_mesh_metadata(mesh, metadata)) {
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
        if (!list_children(geometry.payload_offset,
                           geometry.payload_offset + geometry.payload_size,
                           mesh_context(mesh) + " geometry container", parts)) {
            return fail(mesh, geometry.offset,
                        "geometry container is malformed; mesh skipped");
        }

        // Unknown chunks are intentionally skipped. The two hash lists are
        // present only when their count byte is non-zero.
        RawChunk texture_list;
        RawChunk material_key_list;
        bool has_texture_list = false;
        bool has_material_key_list = false;
        RawChunk geometry_metadata;
        RawChunk vertex_buffer;
        RawChunk material_buffer;
        RawChunk face_buffer;
        if (!find_optional(mesh, children, kTextureListId, container.offset,
                           texture_list, has_texture_list) ||
            !find_optional(mesh, children, kMaterialKeyListId, container.offset,
                           material_key_list, has_material_key_list) ||
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

        if (!parse_counts(mesh, geometry_metadata) ||
            !parse_vertices(mesh, vertex_buffer) ||
            !parse_faces(mesh, face_buffer) ||
            !validate_indices(mesh, face_buffer) ||
            !parse_hash_list(mesh, "texture", kTextureListId,
                             mesh.declared_texture_count, has_texture_list, texture_list,
                             mesh.texture_list_offset, mesh.texture_hashes) ||
            !parse_hash_list(mesh, "material-key", kMaterialKeyListId,
                             mesh.declared_material_key_count, has_material_key_list,
                             material_key_list, mesh.material_key_list_offset,
                             mesh.material_key_hashes) ||
            !parse_materials(mesh, material_buffer)) {
            return false;
        }

        // An empty mesh passed every structural check above with all four
        // data chunks holding nothing but their alignment prefix. It is a
        // retail placeholder, not an error, and it carries no geometry.
        if (mesh.declared_vertex_count == 0) {
            clear_decoded_data(mesh);
            mesh.empty = true;
            return false;
        }
        mesh.valid = true;
        return true;
    }

    // 0x00134011: one 176-byte record at the 16-byte aligned offset inside
    // the payload, preceded by 0x11 filler. It carries the mesh name hash,
    // the two hash-list counts and the NUL-terminated name.
    bool parse_mesh_metadata(TrackMesh& mesh, const RawChunk& metadata) {
        std::uint64_t record = 0;
        std::uint64_t prefix = 0;
        if (!locate_aligned(mesh, metadata, kMeshMetadataAlignment,
                            kMeshMetadataRecordSize, 0,
                            "one 176-byte mesh metadata record", record,
                            prefix)) {
            return false;
        }
        mesh.mesh_metadata_record_offset = record;
        mesh.mesh_metadata_prefix_bytes = prefix;

        const unsigned char* payload = at(record);
        const unsigned char* name = payload + kMeshMetadataNameOffset;
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
                Message text;
                text << "mesh name byte " << i << " is " << hex_byte(name[i])
                     << "; expected printable ASCII";
                return fail(mesh, metadata.offset, text.str());
            }
        }
        mesh.name.assign(reinterpret_cast<const char*>(name), length);
        mesh.mesh_hash = read_u32_le(payload + kMeshMetadataHashOffset);
        mesh.declared_texture_count = payload[kMeshMetadataTextureCountOffset];
        mesh.declared_material_key_count = payload[kMeshMetadataKeyCountOffset];
        hash_known_[mesh.index] = true;
        return true;
    }

    // One count word of the 0x00134900 record. The retail exporter stores
    // these counts in 16 bits; the high half is zero in every sampled mesh
    // but one, and that one sign-extends a count above 32767 (see
    // docs/TRACK.md). A word this decoder cannot read as a plain count is
    // refused rather than guessed.
    bool read_count(
        TrackMesh& mesh,
        const RawChunk& metadata,
        const unsigned char* record,
        std::size_t offset,
        const char* what,
        std::uint32_t limit,
        std::uint32_t& out) {
        const std::uint32_t raw = read_u32_le(record + offset);
        if ((raw >> 16U) != 0U) {
            Message text;
            text << what << " count word at file offset "
                 << (mesh.geometry_metadata_record_offset + offset) << " is "
                 << hex_word(raw) << "; its high half must be zero";
            return fail(mesh, metadata.offset, text.str());
        }
        out = raw & 0xFFFFU;
        if (out > limit) {
            Message text;
            text << what << " count " << out << " in " << chunk_label(metadata)
                 << " exceeds the limit of " << limit;
            return fail(mesh, metadata.offset, text.str());
        }
        return true;
    }

    bool parse_counts(TrackMesh& mesh, const RawChunk& metadata) {
        std::uint64_t record = 0;
        std::uint64_t prefix = 0;
        if (!locate_aligned(mesh, metadata, kGeometryMetadataAlignment,
                            kGeometryMetadataSize, 0,
                            "one 64-byte geometry metadata record", record,
                            prefix)) {
            return false;
        }
        mesh.geometry_metadata_record_offset = record;
        mesh.geometry_metadata_prefix_bytes = prefix;

        const unsigned char* payload = at(record);
        mesh.vertex_format_flags = read_u32_le(payload + kVertexFormatOffset);
        const TrackVertexFormat format =
            track_vertex_format(mesh.vertex_format_flags);
        mesh.vertex_format = format.format;
        mesh.vertex_stride = format.stride;
        if (format.stride != kTrackVertexStride) {
            Message text;
            text << "vertex format word " << hex_word(mesh.vertex_format_flags)
                 << " at file offset " << (record + kVertexFormatOffset)
                 << " selects retail vertex format " << format.format
                 << " with a stride of " << format.stride
                 << " bytes (selector row mask " << hex_word(format.mask)
                 << "); this decoder only reads the "
                 << kTrackVertexStride << "-byte track layout";
            return fail(mesh, metadata.offset, text.str());
        }

        if (!read_count(mesh, metadata, payload, kMaterialCountOffset, "material",
                        limits_.max_materials_per_mesh,
                        mesh.declared_material_count) ||
            !read_count(mesh, metadata, payload, kFaceCountOffset, "face",
                        limits_.max_faces_per_mesh, mesh.declared_face_count) ||
            !read_count(mesh, metadata, payload, kVertexCountOffset, "vertex",
                        limits_.max_vertices_per_mesh,
                        mesh.declared_vertex_count)) {
            return false;
        }

        const bool all_zero = mesh.declared_vertex_count == 0 &&
                              mesh.declared_face_count == 0 &&
                              mesh.declared_material_count == 0;
        const bool all_positive = mesh.declared_vertex_count != 0 &&
                                  mesh.declared_face_count != 0 &&
                                  mesh.declared_material_count != 0;
        if (!all_zero && !all_positive) {
            Message text;
            text << "vertex, face and material counts ("
                 << mesh.declared_vertex_count << ", "
                 << mesh.declared_face_count << ", "
                 << mesh.declared_material_count
                 << ") in " << chunk_label(metadata)
                 << " are neither all zero, which is an empty mesh, nor all"
                    " positive";
            return fail(mesh, metadata.offset, text.str());
        }
        return true;
    }

    // 0x00134012 and 0x00134013: (u32 hash, u32 0) entries. The entry count
    // is a byte of the 0x00134011 record. A non-zero count needs the chunk;
    // a zero count allows either shape, and retail uses both: every track
    // mesh carries a 0x00134012, empty ones with a zero-byte payload, while
    // a mesh with no material keys carries no 0x00134013 at all.
    bool parse_hash_list(
        TrackMesh& mesh,
        const char* what,
        std::uint32_t id,
        std::uint32_t declared,
        bool present,
        const RawChunk& list,
        std::uint64_t& list_offset,
        std::vector<std::uint32_t>& out) {
        Message text;
        if (!present) {
            if (declared != 0) {
                text << "the " << what << " count byte of "
                     << format_chunk_id(kMeshMetadataId) << " is " << declared
                     << " but the mesh has no " << format_chunk_id(id)
                     << " chunk";
                return fail(mesh, mesh.offset, text.str());
            }
            return true;
        }
        std::uint64_t expected = 0;
        if (!checked_mul(declared, kTextureListEntrySize, expected) ||
            list.payload_size != expected) {
            text << chunk_label(list) << " payload is " << list.payload_size
                 << " bytes but the " << what << " count byte of "
                 << format_chunk_id(kMeshMetadataId) << " is " << declared
                 << " (" << expected << " bytes)";
            return fail(mesh, list.offset, text.str());
        }
        list_offset = list.payload_offset;
        const unsigned char* cursor = at(list.payload_offset);
        out.reserve(declared);
        for (std::uint64_t i = 0; i < declared;
             ++i, cursor += kTextureListEntrySize) {
            if (read_u32_le(cursor + 4) != 0U) {
                text << chunk_label(list) << " entry " << i
                     << " has a non-zero second word at file offset "
                     << (list.payload_offset + i * kTextureListEntrySize + 4);
                return fail(mesh, list.offset, text.str());
            }
            out.push_back(read_u32_le(cursor));
        }
        return true;
    }

    // 0x00134B02: material records. Index runs must be contiguous from 0,
    // multiples of 3, cover every face exactly once, and reference an
    // existing texture list entry.
    bool parse_materials(TrackMesh& mesh, const RawChunk& buffer) {
        const auto count = static_cast<std::size_t>(mesh.declared_material_count);
        std::uint64_t data_size = 0;
        std::uint64_t data_offset = 0;
        std::uint64_t prefix = 0;
        if (!checked_mul(count, kMaterialRecordSize, data_size)) {
            return fail(mesh, buffer.offset, "material byte size overflows");
        }
        if (!locate_aligned(mesh, buffer, kMaterialDataAlignment, data_size, 0,
                            std::to_string(count) +
                                " material records of 60 bytes",
                            data_offset, prefix)) {
            return false;
        }
        mesh.material_data_offset = data_offset;
        mesh.material_prefix_bytes = prefix;

        const std::uint64_t index_total =
            static_cast<std::uint64_t>(mesh.declared_face_count) * 3U;
        std::uint64_t next_index = 0;
        const unsigned char* cursor = at(data_offset);
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

            Message text;
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
            Message text;
            text << chunk_label(buffer) << " material records cover "
                 << next_index << " indices; the face count needs "
                 << index_total;
            return fail(mesh, buffer.offset, text.str());
        }
        return true;
    }

    // The data of a buffer starts at the first file offset inside its payload
    // that is a multiple of alignment. The bytes between the payload start
    // and that offset must all be 0x11. The payload must then hold exactly
    // data_size bytes of data followed by tail_size bytes of tail padding.
    // Positions are never inferred by scanning for 0x11 bytes.
    bool locate_aligned(
        TrackMesh& mesh,
        const RawChunk& buffer,
        std::uint64_t alignment,
        std::uint64_t data_size,
        std::uint64_t tail_size,
        const std::string& description,
        std::uint64_t& data_offset,
        std::uint64_t& prefix,
        const std::string& note = std::string()) {
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
            Message text;
            text << chunk_label(buffer) << " payload is " << buffer.payload_size
                 << " bytes but " << expected << " are expected: " << prefix
                 << " bytes of 0x11 up to the " << alignment
                 << "-byte aligned file offset " << data_offset << ", "
                 << description << " (" << data_size << " bytes)";
            if (tail_size != 0) {
                text << " and " << tail_size << " zero tail bytes";
            }
            text << "; the declared count may be wrong" << note;
            return fail(mesh, buffer.offset, text.str());
        }

        const unsigned char* payload = at(buffer.payload_offset);
        for (std::uint64_t i = 0; i < prefix; ++i) {
            if (payload[i] != kAlignmentByte) {
                Message text;
                text << chunk_label(buffer) << " alignment prefix byte " << i
                     << " (file offset " << (buffer.payload_offset + i) << ") is "
                     << hex_byte(payload[i]) << ", expected 0x11";
                return fail(mesh, buffer.offset, text.str());
            }
        }
        return true;
    }

    bool parse_vertices(TrackMesh& mesh, const RawChunk& buffer) {
        const auto count = static_cast<std::size_t>(mesh.declared_vertex_count);
        if (static_cast<std::uint64_t>(count) >
            limits_.max_vertices - report_.cost.vertices) {
            Message text;
            text << "the region already holds " << report_.cost.vertices
                 << " vertices; " << count << " more exceed the limit of "
                 << limits_.max_vertices;
            return fail(mesh, buffer.offset, text.str());
        }
        std::uint64_t data_size = 0;
        std::uint64_t data_offset = 0;
        std::uint64_t prefix = 0;
        if (!checked_mul(count, kTrackVertexStride, data_size)) {
            return fail(mesh, buffer.offset, "vertex byte size overflows");
        }
        if (!locate_aligned(mesh, buffer, kVertexDataAlignment, data_size, 0,
                            std::to_string(count) + " vertices of " +
                                std::to_string(kTrackVertexStride) + " bytes",
                            data_offset, prefix,
                            describe_stride(buffer, count))) {
            return false;
        }
        mesh.vertex_data_offset = data_offset;
        mesh.vertex_prefix_bytes = prefix;

        const unsigned char* cursor = at(data_offset);
        mesh.vertices.resize(count);
        for (std::size_t i = 0; i < count; ++i, cursor += kTrackVertexStride) {
            TrackVertex& vertex = mesh.vertices[i];
            vertex.px = read_f32_le(cursor + 0);
            vertex.pz = read_f32_le(cursor + 4);
            vertex.py = read_f32_le(cursor + 8);
            std::memcpy(vertex.color, cursor + 12, sizeof(vertex.color));
            vertex.u = read_f32_le(cursor + 16);
            vertex.v = read_f32_le(cursor + 20);

            const float values[] = {vertex.px, vertex.py, vertex.pz, vertex.u,
                                    vertex.v};
            for (float value : values) {
                if (!std::isfinite(value)) {
                    Message text;
                    text << "vertex " << i << " in " << chunk_label(buffer)
                         << " has a non-finite float";
                    return fail(mesh, buffer.offset, text.str());
                }
            }
        }
        return true;
    }

    // Appended to a rejected vertex buffer: the stride the payload would
    // need, when the payload divides evenly by the declared count. It is a
    // diagnostic only. The parser never decodes another stride. It returns
    // an empty note when the buffer is exactly the right size, so a sound
    // mesh never builds the string.
    std::string describe_stride(const RawChunk& buffer, std::size_t count) {
        if (count == 0) {
            return std::string();
        }
        std::uint64_t aligned = 0;
        if (!align_up(buffer.payload_offset, kVertexDataAlignment, aligned)) {
            return std::string();
        }
        const std::uint64_t prefix = aligned - buffer.payload_offset;
        if (buffer.payload_size < prefix) {
            return std::string();
        }
        const std::uint64_t data = buffer.payload_size - prefix;
        if (data == static_cast<std::uint64_t>(count) * kTrackVertexStride) {
            return std::string();
        }
        Message text;
        text << "; the payload holds " << data << " data bytes for " << count
             << " vertices";
        if (data % count == 0) {
            text << ", a stride of " << (data / count) << " bytes";
        } else {
            text << ", not a whole stride";
        }
        return text.str();
    }

    bool parse_faces(TrackMesh& mesh, const RawChunk& buffer) {
        const auto count = static_cast<std::size_t>(mesh.declared_face_count);
        if (static_cast<std::uint64_t>(count) >
            limits_.max_triangles - report_.cost.triangles) {
            Message text;
            text << "the region already holds " << report_.cost.triangles
                 << " triangles; " << count << " more exceed the limit of "
                 << limits_.max_triangles;
            return fail(mesh, buffer.offset, text.str());
        }
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

        const unsigned char* tail_bytes = at(data_offset + data_size);
        for (std::uint64_t i = 0; i < tail; ++i) {
            if (tail_bytes[i] != 0U) {
                Message text;
                text << chunk_label(buffer) << " tail padding byte " << i
                     << " (file offset " << (data_offset + data_size + i)
                     << ") is " << hex_byte(tail_bytes[i]) << ", expected 0x00";
                return fail(mesh, buffer.offset, text.str());
            }
        }
        mesh.face_data_offset = data_offset;
        mesh.face_prefix_bytes = prefix;
        mesh.face_tail_bytes = tail;

        const unsigned char* cursor = at(data_offset);
        mesh.triangles.resize(count);
        for (std::size_t i = 0; i < count; ++i, cursor += kFaceStride) {
            MeshTriangle& triangle = mesh.triangles[i];
            triangle.a = read_u16_le(cursor + 0);
            triangle.b = read_u16_le(cursor + 2);
            triangle.c = read_u16_le(cursor + 4);
        }
        return true;
    }

    bool validate_indices(TrackMesh& mesh, const RawChunk& buffer) {
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
            Message text;
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
    std::uint64_t base_;
    std::uint64_t end_;
    const TrackLimits& limits_;
    TrackRegionReport& report_;
    std::set<std::uint32_t> texture_hashes_;
    // Parallel to report_.meshes: whether the mesh got a name hash before
    // it failed, so the hash table can say which meshes it must cover.
    std::vector<char> hash_known_;
};

} // namespace

TrackVertexFormat track_vertex_format(std::uint32_t flags) noexcept {
    // speed.exe table at 0x00701440, four rows of (mask, format index).
    // Strides come from the declarations the startup builder assembles at
    // 0x0040E180 out of the element table at 0x007010C0.
    struct Row {
        std::uint32_t mask;
        std::uint32_t format;
        std::size_t stride;
    };
    static const Row rows[] = {
        {0x00000001U, 4U, 60U},
        {0x00000040U, 2U, 24U},
        {0x00000080U, 5U, 36U},
        {0x00000000U, 0U, 24U},
    };
    for (const Row& row : rows) {
        if (row.mask == 0U || (flags & row.mask) != 0U) {
            return TrackVertexFormat{row.mask, row.format, row.stride};
        }
    }
    // Unreachable: the last row matches every value.
    return TrackVertexFormat{0U, 0U, 24U};
}

std::size_t TrackRegionReport::valid_mesh_count() const noexcept {
    std::size_t count = 0;
    for (const TrackMesh& mesh : meshes) {
        if (mesh.valid) {
            ++count;
        }
    }
    return count;
}

TrackRegionReport parse_track_region(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackLimits& limits) {
    TrackRegionReport report;
    report.region.offset = base_offset;
    report.region.size = size;
    report.cost.region_bytes = size;
    if (data == nullptr && size != 0) {
        report.issues.push_back(TrackIssue{base_offset, "null region buffer"});
        return report;
    }
    if (size > limits.max_region_bytes) {
        Message text;
        text << "region is " << size << " bytes; limit is "
             << limits.max_region_bytes;
        report.issues.push_back(TrackIssue{base_offset, text.str()});
        return report;
    }
    if (size > std::numeric_limits<std::uint64_t>::max() - base_offset) {
        report.issues.push_back(
            TrackIssue{base_offset, "region end offset overflows"});
        return report;
    }
    try {
        TrackParser parser(data, size, base_offset, limits, report);
        parser.run();
    } catch (const std::bad_alloc&) {
        report.issues.push_back(
            TrackIssue{base_offset, "out of memory while parsing the region"});
    } catch (const std::exception& error) {
        report.issues.push_back(TrackIssue{
            base_offset, std::string("region parse failed: ") + error.what()});
    }
    return report;
}

TrackRegionReport load_track_region(
    const std::filesystem::path& path,
    const TrackRegionSelector& region,
    std::vector<unsigned char>& bytes,
    const TrackLimits& limits) {
    bytes.clear();
    TrackRegionReport report;
    report.region = region;

    std::error_code size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        report.issues.push_back(
            TrackIssue{region.offset, "failed to read file size"});
        return report;
    }
    report.file_size = static_cast<std::uint64_t>(file_size);

    if (region.size == 0) {
        report.issues.push_back(TrackIssue{region.offset, "region size is zero"});
        return report;
    }
    if (region.size > limits.max_region_bytes) {
        Message text;
        text << "region is " << region.size << " bytes; limit is "
             << limits.max_region_bytes;
        report.issues.push_back(TrackIssue{region.offset, text.str()});
        return report;
    }
    if (region.offset > report.file_size ||
        region.size > report.file_size - region.offset) {
        Message text;
        text << "region at offset " << region.offset << " with " << region.size
             << " bytes is outside the " << report.file_size << "-byte file";
        report.issues.push_back(TrackIssue{region.offset, text.str()});
        return report;
    }
    if (region.size > std::numeric_limits<std::size_t>::max() ||
        region.offset > static_cast<std::uint64_t>(
                            std::numeric_limits<std::streamoff>::max())) {
        report.issues.push_back(
            TrackIssue{region.offset, "region is too large for this build"});
        return report;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        report.issues.push_back(TrackIssue{region.offset, "failed to open file"});
        return report;
    }
    stream.seekg(static_cast<std::streamoff>(region.offset), std::ios::beg);
    if (!stream) {
        report.issues.push_back(
            TrackIssue{region.offset, "failed to seek to the region"});
        return report;
    }

    try {
        bytes.resize(static_cast<std::size_t>(region.size));
    } catch (const std::exception&) {
        bytes.clear();
        report.issues.push_back(
            TrackIssue{region.offset, "out of memory while reading the region"});
        return report;
    }
    if (!stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        report.issues.push_back(
            TrackIssue{region.offset, "failed to read the whole region"});
        return report;
    }

    TrackRegionReport parsed =
        parse_track_region(bytes.data(), bytes.size(), region.offset, limits);
    parsed.file_size = report.file_size;
    return parsed;
}

std::vector<std::uint32_t> track_region_texture_hashes(
    const TrackRegionReport& report) {
    std::set<std::uint32_t> hashes;
    for (const TrackMesh& mesh : report.meshes) {
        if (!mesh.valid) {
            continue;
        }
        for (const MeshMaterial& material : mesh.materials) {
            if (material.texture_slot < mesh.texture_hashes.size()) {
                hashes.insert(mesh.texture_hashes[material.texture_slot]);
            }
        }
    }
    return std::vector<std::uint32_t>(hashes.begin(), hashes.end());
}

} // namespace nfsu
