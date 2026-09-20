#pragma once

#include "nfsu/mesh.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace nfsu {

// Chunk IDs used by NFSU car GEOMETRY.BIN files. See docs/FORMATS.md.
constexpr std::uint32_t kGeometryRootId = 0x80134000U;
constexpr std::uint32_t kGeometryHeaderId = 0x80134001U;
constexpr std::uint32_t kMeshHashTableId = 0x00134003U;
constexpr std::uint32_t kMeshDirectoryId = 0x00134004U;
constexpr std::uint32_t kMeshContainerId = 0x80134010U;
constexpr std::uint32_t kMeshMetadataId = 0x00134011U;
constexpr std::uint32_t kTextureListId = 0x00134012U;
constexpr std::uint32_t kMaterialKeyListId = 0x00134013U;
// Attachment markers. Optional: 2860 of the 9408 retail meshes carry one,
// and 647 of those chunks hold no record. See docs/PART_PLACEMENT.md.
constexpr std::uint32_t kMarkerListId = 0x0013401AU;
constexpr std::uint32_t kGeometryContainerId = 0x80134100U;
constexpr std::uint32_t kGeometryMetadataId = 0x00134900U;
constexpr std::uint32_t kVertexBufferId = 0x00134B01U;
constexpr std::uint32_t kMaterialBufferId = 0x00134B02U;
constexpr std::uint32_t kFaceBufferId = 0x00134B03U;

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
constexpr std::size_t kMeshHashRecordSize = 8;
constexpr std::size_t kMeshDirectoryRecordSize = 20;

// Material binding. 0x00134012 contains texture hashes and 0x00134013
// contains material/effect key hashes; both use (u32 hash, u32 0) entries
// with count bytes in 0x00134011. 0x00134B02 holds material records of
// kMaterialRecordSize bytes after 0x11 padding to kMaterialDataAlignment;
// their count is the i32 at kMaterialCountOffset in the 0x00134900 record.
constexpr std::size_t kTextureListCountOffset = 25;
constexpr std::size_t kTextureListEntrySize = 8;
constexpr std::size_t kMaterialKeyListCountOffset = 26;
constexpr std::size_t kMaterialKeyListEntrySize = 8;
constexpr std::size_t kMaterialCountOffset = 16;
constexpr std::uint64_t kMaterialDataAlignment = 16;

// Marker records start at the first absolute offset in the payload that is a
// multiple of kMarkerDataAlignment, after kAlignmentByte filler, exactly as
// the other aligned records do. 0x005400EA..0x0054010E derives the count as
// (payload_size - filler) / kMarkerRecordSize and stores it in a byte, so a
// chunk can hold at most kMaxMarkersPerMesh records.
constexpr std::size_t kMarkerRecordSize = 80;
constexpr std::uint64_t kMarkerDataAlignment = 16;
constexpr std::size_t kMaxMarkersPerMesh = 255;
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
    // True only when every encountered directory validates and every parsed
    // mesh received an entry from the pair in its owning geometry root.
    bool mesh_directory_valid = false;

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
