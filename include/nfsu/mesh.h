#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

constexpr std::size_t kMeshNameSize = 28;
constexpr std::size_t kMaterialRecordSize = 60;

// One decoded material record: a run of triangle indices drawn with one
// texture. Raw offsets: +0 bounds min, +12 index count, +16 bounds max,
// +28 texture slot, +32 secondary slot, +52 first index and +56 flags.
// The raw bytes preserve every field whose meaning is still unknown.
struct MeshMaterial {
    std::uint64_t offset = 0;
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
    std::uint32_t index_count = 0;
    std::uint32_t texture_slot = 0;
    std::uint32_t secondary_slot = 0;
    std::uint32_t first_index = 0;
    std::uint32_t flags = 0;
    unsigned char raw[kMaterialRecordSize] = {};
};

// Vertex fields are named by source semantics. The car file stores them in
// the order px, pz, py, nx, nz, ny, color[4], u, v.
struct MeshVertex {
    float px = 0.0F;
    float py = 0.0F;
    float pz = 0.0F;
    float nx = 0.0F;
    float ny = 0.0F;
    float nz = 0.0F;
    std::uint8_t color[4] = {0, 0, 0, 0};
    float u = 0.0F;
    float v = 0.0F;
};

// Triangle indices in source order.
struct MeshTriangle {
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
};

// In-memory mesh data shared by geometry decoders and prepared scenes. It
// contains no file, path, archive or command-line API.
struct Mesh {
    std::size_t index = 0;
    std::string name;
    std::uint64_t offset = 0;
    // Verified 0x00134003/0x00134004 directory entry. The range repeats
    // this mesh container's absolute offset and total size.
    std::uint32_t directory_hash = 0;
    std::uint64_t directory_offset = 0;
    std::uint64_t directory_size = 0;
    bool has_directory_entry = false;
    std::int32_t declared_vertex_count = 0;
    std::int32_t declared_face_count = 0;
    // Absolute source offsets of aligned records and their padding lengths;
    // zero until the corresponding record has been decoded.
    std::uint64_t metadata_record_offset = 0;
    std::uint64_t metadata_prefix_bytes = 0;
    std::uint64_t vertex_data_offset = 0;
    std::uint64_t vertex_prefix_bytes = 0;
    std::uint64_t face_data_offset = 0;
    std::uint64_t face_prefix_bytes = 0;
    std::uint64_t face_tail_bytes = 0;
    std::uint64_t texture_list_offset = 0;
    std::uint64_t material_data_offset = 0;
    std::uint64_t material_prefix_bytes = 0;
    std::int32_t declared_material_count = 0;
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;
    std::vector<std::uint32_t> texture_hashes;
    std::vector<MeshMaterial> materials;
    bool valid = false;
};

} // namespace nfsu
