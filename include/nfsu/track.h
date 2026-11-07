#pragma once

#include "nfsu/mesh.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Bounded decoding of one explicitly selected region of a track STREAM
// container. Track geometry reuses the car geometry chunk ids of
// include/nfsu/geometry.h, but the container is not identical: a track
// geometry root carries no 0x00134004 mesh directory, the 0x00134011
// metadata is an aligned 176-byte record, a per-mesh hash list may be
// absent when its count byte is zero, and the vertex buffer holds 24-byte
// vertices instead of 36-byte ones. See docs/TRACK.md for the byte
// evidence and for what is still unknown.
//
// Nothing here scans a directory, follows a streaming table or reads a
// whole installation. A caller names one file, one absolute byte offset
// and one byte length; exactly those bytes are read and parsed.

// The only vertex stride this decoder accepts. A mesh whose 0x00134900
// vertex-format word selects another stride is refused with a diagnostic
// that names the flags, the selected format and that stride; nothing is
// decoded from a layout this project has not proven.
constexpr std::size_t kTrackVertexStride = 24;

// 0x00134900 +12, a vertex-format flags word, little-endian.
constexpr std::size_t kVertexFormatOffset = 12;

// 0x00134011 holds one 176-byte record at the first absolute file offset
// inside its payload that is a multiple of 16; the bytes before it are
// 0x11, as in the geometry chunks. Offsets below are inside that record.
constexpr std::size_t kMeshMetadataAlignment = 16;
constexpr std::size_t kMeshMetadataRecordSize = 176;
constexpr std::size_t kMeshMetadataHashOffset = 16;
constexpr std::size_t kMeshMetadataTextureCountOffset = 25;
constexpr std::size_t kMeshMetadataKeyCountOffset = 26;
constexpr std::size_t kMeshMetadataNameOffset =
    kMeshMetadataRecordSize - kMeshNameSize;

// One row of the retail vertex-format selector. speed.exe 0x0040C890
// walks the four-row table at 0x00701440 and takes the first row whose
// mask shares a bit with the 0x00134900 flags word at +12; the last row
// has mask 0 and always matches. Each row names a vertex declaration
// built at startup, whose stride is the sum of its element sizes.
struct TrackVertexFormat {
    // The row that matched. 0 is the default row.
    std::uint32_t mask = 0;
    // Index into the runtime vertex-declaration array.
    std::uint32_t format = 0;
    std::size_t stride = 0;
};

// Applies that table. Total: never fails, because the last row matches
// every value.
TrackVertexFormat track_vertex_format(std::uint32_t flags) noexcept;

// One 24-byte track vertex, little-endian. Fields are named by source
// semantics, exactly as MeshVertex names the 36-byte car vertex: the file
// slot order is px, pz, py, so slot 1 is py and slot 2 is pz here too.
// The car vertex is this record with one more float triple, the normal,
// inserted after the position; a track vertex carries no normal.
struct TrackVertex {
    float px = 0.0F;
    float py = 0.0F;
    float pz = 0.0F;
    // Bytes 12..15 as stored. The retail declaration calls them one
    // D3DCOLOR. Its little-endian bytes are BGRA; the original declaration
    // expands them to RGBA. Retain the stored order in this raw structure.
    std::uint8_t color[4] = {0, 0, 0, 0};
    float u = 0.0F;
    float v = 0.0F;
};

struct TrackIssue {
    std::uint64_t offset = 0;
    std::string message;
};

// The explicitly selected region: an absolute byte offset in the file and
// a byte length, both given by the caller. The region must start on a
// chunk header and end exactly on a chunk boundary, so a region is always
// a whole number of top-level chunks.
struct TrackRegionSelector {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct TrackLimits {
    // Bytes the loader will read for one region. It is deliberately far
    // below a whole STREAM file: the point of a region is not to read one.
    std::uint64_t max_region_bytes = 64ULL * 1024ULL * 1024ULL;
    std::size_t max_chunks = 200000;
    std::size_t max_meshes = 16384;
    // uint16 indices cannot address more than 65536 vertices.
    std::uint32_t max_vertices_per_mesh = 65536;
    std::uint32_t max_faces_per_mesh = 1U << 20;
    std::uint32_t max_materials_per_mesh = 4096;
    // Over the whole region, checked while the meshes are decoded.
    std::uint64_t max_vertices = 1ULL << 22;
    std::uint64_t max_triangles = 1ULL << 22;
};

// One decoded track mesh. Every provenance field is an absolute file
// offset, not a region-relative one.
constexpr std::size_t kUnknownTrackGeometryRoot = static_cast<std::size_t>(-1);
struct TrackMesh {
    std::size_t index = 0;
    // Report-local geometry-root identity, preserved across stream merges.
    // Unknown for hand-built reports that did not parse an owning root.
    std::size_t root_index = kUnknownTrackGeometryRoot;
    std::string name;
    std::uint64_t offset = 0;
    // 0x00134011 +16, the mesh name hash, matched against the
    // 0x00134003 table of the owning geometry root. has_hash_record says
    // the match was made and hash_record_offset is that record's absolute
    // file offset.
    std::uint32_t mesh_hash = 0;
    std::uint64_t hash_record_offset = 0;
    bool has_hash_record = false;
    // 0x00134011 +25 and +26: the entry counts of 0x00134012 and
    // 0x00134013. A non-zero count requires its chunk; a zero count allows
    // the chunk to be absent or to hold nothing. Retail uses both shapes.
    std::uint32_t declared_texture_count = 0;
    std::uint32_t declared_material_key_count = 0;
    // Raw 0x00134900 fields. The counts are the low halves of their
    // 32-bit words; the parser refuses a word whose high half is set.
    std::uint32_t vertex_format_flags = 0;
    std::uint32_t vertex_format = 0;
    std::size_t vertex_stride = 0;
    std::uint32_t declared_vertex_count = 0;
    std::uint32_t declared_face_count = 0;
    std::uint32_t declared_material_count = 0;
    // 0x00134011, the 176-byte record.
    std::uint64_t mesh_metadata_record_offset = 0;
    std::uint64_t mesh_metadata_prefix_bytes = 0;
    // 0x00134900, the 64-byte record.
    std::uint64_t geometry_metadata_record_offset = 0;
    std::uint64_t geometry_metadata_prefix_bytes = 0;
    std::uint64_t vertex_data_offset = 0;
    std::uint64_t vertex_prefix_bytes = 0;
    std::uint64_t face_data_offset = 0;
    std::uint64_t face_prefix_bytes = 0;
    std::uint64_t face_tail_bytes = 0;
    std::uint64_t texture_list_offset = 0;
    std::uint64_t material_key_list_offset = 0;
    std::uint64_t material_data_offset = 0;
    std::uint64_t material_prefix_bytes = 0;
    std::vector<TrackVertex> vertices;
    std::vector<MeshTriangle> triangles;
    std::vector<std::uint32_t> texture_hashes;
    std::vector<std::uint32_t> material_key_hashes;
    std::vector<MeshMaterial> materials;
    // A retail placeholder: all three 0x00134900 counts are zero and
    // every one of the four data chunks holds nothing but its alignment
    // prefix. It is structurally sound, so it raises no issue, but it
    // carries no geometry and is never valid.
    bool empty = false;
    bool valid = false;
};

// Deterministic parse cost of one region. Every field is a byte or item
// count derived from the region alone; none of it depends on iteration
// order, allocation addresses or wall-clock time.
struct TrackRegionCost {
    // Bytes the region occupies, which is what a loader reads.
    std::uint64_t region_bytes = 0;
    // Chunk headers walked, at every depth.
    std::uint64_t chunks = 0;
    std::uint64_t geometry_roots = 0;
    std::uint64_t meshes = 0;
    std::uint64_t valid_meshes = 0;
    std::uint64_t empty_meshes = 0;
    // The counts below cover the valid meshes only.
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
    std::uint64_t materials = 0;
    // Vertex and index bytes as stored: vertices * kTrackVertexStride and
    // triangles * 6.
    std::uint64_t source_vertex_bytes = 0;
    std::uint64_t source_index_bytes = 0;
    // What the decoded meshes hold in memory instead.
    std::uint64_t decoded_vertex_bytes = 0;
    std::uint64_t decoded_index_bytes = 0;
    // Distinct texture hashes named by the valid meshes' material records.
    std::uint64_t distinct_texture_hashes = 0;
    std::uint64_t largest_mesh_vertices = 0;
    std::uint64_t largest_mesh_triangles = 0;
    // Axis-aligned bounds over every vertex of every valid mesh, in the
    // TrackVertex field names. Zero when the region has no valid vertex;
    // has_bounds says which.
    bool has_bounds = false;
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
};

struct TrackRegionReport {
    // Size of the file the region was read from; 0 for an in-memory parse.
    std::uint64_t file_size = 0;
    TrackRegionSelector region;
    std::vector<TrackMesh> meshes;
    std::vector<TrackIssue> issues;
    TrackRegionCost cost;
    // True only when every geometry root in the region carries a
    // 0x00134003 hash table that validates and every parsed mesh was
    // matched to exactly one of its records.
    bool mesh_table_valid = false;

    bool ok() const noexcept { return issues.empty(); }
    std::size_t valid_mesh_count() const noexcept;
};

// Parses one region image. data must be the region's bytes and nothing
// else; base_offset must be the absolute file offset those bytes came
// from, because the alignment rules of 0x00134011, 0x00134900,
// 0x00134B01, 0x00134B02 and 0x00134B03 are relative to the start of the
// file. Never throws for malformed input: every problem is reported in
// TrackRegionReport::issues, and a mesh that fails validation is kept
// with valid == false and no vertex, index or material data.
TrackRegionReport parse_track_region(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackLimits& limits = TrackLimits{});

// Reads exactly region.size bytes at region.offset of path, read-only,
// into bytes, then parses them. The rest of the file is never read. The
// region must lie inside the file and be at most limits.max_region_bytes
// long; bytes is left empty when it is not.
TrackRegionReport load_track_region(
    const std::filesystem::path& path,
    const TrackRegionSelector& region,
    std::vector<unsigned char>& bytes,
    const TrackLimits& limits = TrackLimits{});

// The distinct texture hashes named by the material records of the valid
// meshes, ascending. Deterministic, and directly usable as the "wanted"
// list of a RenderTextureSource so that only this region's textures are
// decoded.
std::vector<std::uint32_t> track_region_texture_hashes(
    const TrackRegionReport& report);

} // namespace nfsu
