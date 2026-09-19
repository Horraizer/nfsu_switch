#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace nfsu {

// Chunk IDs used by NFSU car GEOMETRY.BIN files. See docs/FORMATS.md.
constexpr std::uint32_t kGeometryRootId = 0x80134000U;
constexpr std::uint32_t kMeshContainerId = 0x80134010U;
constexpr std::uint32_t kMeshMetadataId = 0x00134011U;
constexpr std::uint32_t kTextureListId = 0x00134012U;
constexpr std::uint32_t kGeometryContainerId = 0x80134100U;
constexpr std::uint32_t kGeometryMetadataId = 0x00134900U;
constexpr std::uint32_t kVertexBufferId = 0x00134B01U;
constexpr std::uint32_t kMaterialBufferId = 0x00134B02U;
constexpr std::uint32_t kFaceBufferId = 0x00134B03U;

constexpr std::size_t kMeshNameSize = 28;

// Records inside 0x00134900, 0x00134B01 and 0x00134B03 start at the first
// absolute file offset in the payload that is a multiple of the alignment
// below. Every payload byte before that offset must be kAlignmentByte.
constexpr unsigned char kAlignmentByte = 0x11U;
constexpr std::uint64_t kGeometryMetadataAlignment = 16;
constexpr std::uint64_t kVertexDataAlignment = 128;
constexpr std::uint64_t kFaceDataAlignment = 16;
// Face data is followed by zero bytes up to a multiple of this size.
constexpr std::uint64_t kFaceTailAlignment = 4;

// The geometry metadata record is exactly this long; count offsets are
// relative to the aligned record start.
constexpr std::size_t kGeometryMetadataSize = 64;
constexpr std::size_t kFaceCountOffset = 36;
constexpr std::size_t kVertexCountOffset = 52;
constexpr std::size_t kVertexStride = 36;
constexpr std::size_t kFaceStride = 6;

// Texture binding. 0x00134012 is a list of (u32 texture hash, u32 0)
// entries whose count is the byte at kTextureListCountOffset in the
// 0x00134011 payload. 0x00134B02 holds material records of
// kMaterialRecordSize bytes after 0x11 padding to kMaterialDataAlignment;
// their count is the i32 at kMaterialCountOffset in the 0x00134900 record.
constexpr std::size_t kTextureListCountOffset = 25;
constexpr std::size_t kTextureListEntrySize = 8;
constexpr std::size_t kMaterialCountOffset = 16;
constexpr std::uint64_t kMaterialDataAlignment = 16;
constexpr std::size_t kMaterialRecordSize = 60;

// One 0x00134B02 record: a run of triangle indices drawn with one texture.
// Offsets: +0 f32[3] bounds min, +12 u32 index count, +16 f32[3] bounds max,
// +28 u32 texture slot, +32 u32 unknown slot, +52 u32 first index,
// +56 u32 flags. The raw record keeps every other byte.
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

// Vertex fields are named by source semantics. The file stores them in the
// order px, pz, py, nx, nz, ny, color[4], u, v.
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

// Indices in source order.
struct MeshTriangle {
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;
};

struct Mesh {
    std::size_t index = 0;
    std::string name;
    std::uint64_t offset = 0;
    std::int32_t declared_vertex_count = 0;
    std::int32_t declared_face_count = 0;
    // Absolute file offsets of the aligned records and the length of the
    // 0x11 prefix in front of each; zero until that part is parsed.
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
    // 0x00134012 texture hashes, indexed by MeshMaterial::texture_slot.
    std::vector<std::uint32_t> texture_hashes;
    std::vector<MeshMaterial> materials;
    bool valid = false;
};

struct GeometryIssue {
    std::uint64_t offset = 0;
    std::string message;
};

struct GeometryLimits {
    std::uint64_t max_file_size = 256ULL * 1024ULL * 1024ULL;
    std::size_t max_chunks = 200000;
    std::size_t max_meshes = 16384;
    // uint16 indices cannot address more than 65536 vertices.
    std::int32_t max_vertices_per_mesh = 65536;
    std::int32_t max_faces_per_mesh = 1 << 20;
    std::int32_t max_materials_per_mesh = 4096;
};

struct GeometryReport {
    std::uint64_t file_size = 0;
    std::vector<Mesh> meshes;
    std::vector<GeometryIssue> issues;

    bool ok() const noexcept { return issues.empty(); }
    std::size_t valid_mesh_count() const noexcept;
};

// Parses an in-memory GEOMETRY.BIN image. Never throws for malformed input;
// every problem is reported in GeometryReport::issues. Meshes that fail
// validation are kept with valid == false and no vertex or face data.
GeometryReport parse_geometry(
    const unsigned char* data,
    std::size_t size,
    const GeometryLimits& limits = GeometryLimits{});

// Reads the file read-only and parses it.
GeometryReport load_geometry_file(
    const std::filesystem::path& path,
    const GeometryLimits& limits = GeometryLimits{});

// Returns a deterministic OBJ-safe object name such as "m0003_BODY_A".
std::string obj_object_name(const Mesh& mesh);

struct ObjExportStats {
    std::size_t meshes = 0;
    std::uint64_t vertices = 0;
    std::uint64_t triangles = 0;
};

// Writes every mesh with valid == true as a diagnostic right-handed, Y-up
// OBJ: x = px, y = py, z = -pz, same for normals, source triangle order,
// raw u v texture coordinates.
// Indices are validated against the owning mesh again before writing.
// Returns false and sets error if validation or the stream fails.
bool write_obj(
    std::ostream& output,
    const GeometryReport& report,
    ObjExportStats& stats,
    std::string& error);

} // namespace nfsu
