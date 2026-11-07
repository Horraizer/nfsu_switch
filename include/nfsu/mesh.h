#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

constexpr std::size_t kMeshNameSize = 28;
constexpr std::size_t kMaterialRecordSize = 60;

// One decoded material record: a run of triangle indices drawn with one
// texture. Raw offsets: +0 bounds min, +12 index count, +16 bounds max,
// +28 texture slot, +32 material-key slot, +52 first index and +56 flags.
// The raw bytes preserve every field whose meaning is still unknown.
struct MeshMaterial {
    std::uint64_t offset = 0;
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
    std::uint32_t index_count = 0;
    std::uint32_t texture_slot = 0;
    // Index into Mesh::material_key_hashes, or 0xFF for no entry. The game
    // reads the low byte; the other three bytes are zero in the sample.
    std::uint32_t material_key_slot = 0;
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

// One 80-byte attachment marker of chunk 0x0013401A. The record layout and
// every rule are in docs/PART_PLACEMENT.md; the loader is speed.exe
// 0x005400E3, which stores the aligned table at solid +0x80 and the record
// count in the byte at solid +0x1B.
struct MeshMarker {
    // Absolute source offset of the record.
    std::uint64_t offset = 0;
    // +0x00: bin_hash of the marker name. nfsu::car_marker_name resolves the
    // hashes whose names were recovered.
    std::uint32_t name_hash = 0;
    // +0x04..+0x0F. Zero in every one of the 4216 markers the 37
    // retail-original car files hold. The other 245 markers in the installation
    // are all of Cars/LANCER and Cars/PEUGOT, which carry an NFS-CarToolkit
    // header instead of the retail source path and hold uninitialised bytes
    // here. No traced instruction reads these words, and refusing them would
    // refuse files the game itself loads, so they are kept raw and never
    // interpreted. See docs/PART_PLACEMENT.md.
    std::array<std::uint32_t, 3> reserved{};
    // +0x10: a row-major 4x4 matrix. Row 3 is the translation, in the same
    // raw slot order as MeshVertex (longitudinal, lateral, vertical).
    float matrix[16] = {};

    // Row 3 of the matrix.
    float translation(std::size_t axis) const noexcept {
        return matrix[12 + axis];
    }
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
    // Original 0x134011 solid-header bounds, retained after source bytes free.
    bool has_solid_bounds = false;
    float solid_low[3]{}, solid_high[3]{};
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
    std::uint64_t material_key_list_offset = 0;
    std::uint64_t material_data_offset = 0;
    std::uint64_t material_prefix_bytes = 0;
    std::int32_t declared_material_count = 0;
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;
    std::vector<std::uint32_t> texture_hashes;
    // 0x00134013 material/effect keys resolved to runtime objects by the game.
    std::vector<std::uint32_t> material_key_hashes;
    // 0x0013401A attachment markers, in source order. Empty when the mesh has
    // no marker chunk, which is the common case, and also when the mesh
    // failed validation: a mesh with valid == false reports no markers even
    // if its marker chunk parsed before the failing check.
    std::vector<MeshMarker> markers;
    // Absolute offset of the aligned marker record array, and the 0x11 filler
    // before it. Zero when has_marker_chunk is false.
    std::uint64_t marker_data_offset = 0;
    std::uint64_t marker_prefix_bytes = 0;
    // Set only on a mesh that both has a 0x0013401A chunk and validated.
    bool has_marker_chunk = false;
    std::vector<MeshMaterial> materials;
    bool valid = false;
};

} // namespace nfsu
