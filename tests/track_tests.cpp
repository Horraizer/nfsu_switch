#include "nfsu/track.h"
#include "nfsu/track_scene.h"

#include "nfsu/geometry.h"
#include "nfsu/texture.h"
#include "nfsu/texture_decode.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void check_issue_count(
    const nfsu::TrackRegionReport& report,
    std::size_t expected,
    const std::string& what) {
    check(report.issues.size() == expected,
          what + ": expected " + std::to_string(expected) + " issues, got " +
              std::to_string(report.issues.size()) +
              (report.issues.empty() ? std::string()
                                     : " (first: " + report.issues[0].message + ")"));
}

bool has_issue_with(const nfsu::TrackRegionReport& report, const std::string& text) {
    for (const nfsu::TrackIssue& issue : report.issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFFU));
    }
}

void append_f32(Bytes& bytes, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes, bits);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

float read_f32(const unsigned char* bytes) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, bytes, sizeof(bits));
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// Writes a chunk tree. Positions are absolute file offsets (base plus the
// bytes written so far), so alignment prefixes are computed exactly as the
// format does and a region can be placed anywhere in a file.
class Writer {
public:
    explicit Writer(std::uint64_t base) : base_(base) {}

    std::uint64_t at() const { return base_ + bytes_.size(); }
    std::size_t size() const { return bytes_.size(); }
    Bytes& bytes() { return bytes_; }
    const Bytes& bytes() const { return bytes_; }
    std::uint64_t base() const { return base_; }

    void begin(std::uint32_t id) {
        open_.push_back(bytes_.size());
        append_u32(bytes_, id);
        append_u32(bytes_, 0);
    }

    void end() {
        const std::size_t header = open_.back();
        open_.pop_back();
        put_u32(bytes_, header + 4,
                static_cast<std::uint32_t>(bytes_.size() - header - 8));
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append(payload);
        end();
    }

    void append(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    // Appends 0x11 bytes until the absolute position is a multiple of
    // alignment; returns how many were written.
    std::size_t pad_to(std::uint64_t alignment) {
        std::size_t count = 0;
        while (at() % alignment != 0) {
            bytes_.push_back(0x11U);
            ++count;
        }
        return count;
    }

private:
    std::uint64_t base_;
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

struct MaterialSpec {
    std::uint32_t index_count = 3;
    std::uint32_t texture_slot = 0;
    std::uint32_t material_key_slot = 0xFFU;
    std::uint32_t flags = 0;
    std::uint32_t first_index_override = 0;
    bool have_first_index_override = false;
};

struct MeshSpec {
    std::string name = "MESH";
    std::uint32_t hash = 0x11111111U;
    std::uint32_t vertex_format_flags = 0;
    std::uint32_t vertex_count = 3;
    std::uint32_t face_count = 1;
    std::vector<MaterialSpec> materials{MaterialSpec{}};
    std::vector<std::uint32_t> texture_hashes{0xAAAA0001U};
    std::vector<std::uint32_t> material_key_hashes;
    // Deliberate corruptions, applied while the mesh is written.
    bool break_vertex_prefix = false;
    bool break_metadata_prefix = false;
    bool break_geometry_prefix = false;
    bool break_material_prefix = false;
    bool break_face_prefix = false;
    bool force_empty_texture_chunk = false;
    bool drop_texture_chunk = false;
    bool duplicate_texture_chunk = false;
    bool duplicate_mesh_metadata_chunk = false;
    bool duplicate_vertex_chunk = false;
    // A 0x00134B01 whose whole payload is shorter than the 128-byte
    // alignment prefix would be.
    bool tiny_vertex_payload = false;
    // A child chunk of the mesh container whose declared payload runs past
    // the container, written after 0x00134011 so the name still reads.
    bool malformed_child_chunk = false;
    // Values written into the 0x00134011 count bytes instead of the entry
    // counts the chunks really hold. -1 leaves the true count.
    int texture_count_byte_override = -1;
    int key_count_byte_override = -1;
    // Which list entry gets a non-zero second word, when >= 0.
    int bad_list_entry = -1;
    bool unterminated_name = false;
    // Byte written into the name, and where. 0 leaves the name alone.
    unsigned char bad_name_byte = 0;
    std::size_t bad_name_index = 0;
    bool non_finite_vertex = false;
    bool out_of_range_index = false;
    // Which tail byte of an odd face count is made non-zero, when >= 0.
    int bad_face_tail_byte = -1;
    // Override of the byte size actually written for the vertex payload,
    // plus extra bytes appended after it.
    std::uint32_t vertex_stride_override = 0;
    std::uint32_t vertex_extra_bytes = 0;
    // Written into the count words instead of the true counts.
    std::uint32_t vertex_count_word_override = 0;
    bool have_vertex_count_word_override = false;
    std::uint32_t face_count_word_override = 0;
    bool have_face_count_word_override = false;
    std::uint32_t material_count_word_override = 0;
    bool have_material_count_word_override = false;
};

// The retail shape: all three counts zero, and a 0x00134012 chunk that is
// present with a zero-byte payload while its count byte is zero.
MeshSpec empty_mesh_spec(const std::string& name, std::uint32_t hash) {
    MeshSpec spec;
    spec.name = name;
    spec.hash = hash;
    spec.vertex_count = 0;
    spec.face_count = 0;
    spec.materials.clear();
    spec.texture_hashes.clear();
    spec.force_empty_texture_chunk = true;
    return spec;
}

void write_mesh(Writer& writer, const MeshSpec& spec) {
    writer.begin(nfsu::kMeshContainerId);

    // An empty padding chunk is 8 bytes, which shifts the next payload off
    // a 16- or 128-byte grid, so that an aligned record really has a
    // prefix to corrupt. Every prefix corruption asserts it found one.
    const auto ensure_prefix = [&writer](std::uint64_t alignment) {
        if ((writer.at() + 8) % alignment == 0) {
            writer.leaf(0x00000000U, Bytes{});
        }
    };
    const auto break_prefix = [&writer](bool wanted, std::size_t prefix,
                                        const char* what) {
        if (!wanted) {
            return;
        }
        check(prefix != 0,
              std::string("the ") + what +
                  " prefix fixture needs a non-empty prefix");
        if (prefix != 0) {
            writer.bytes().back() = 0x22U;
        }
    };

    // 0x00134011: 176-byte record at the 16-byte aligned offset.
    if (spec.break_metadata_prefix) {
        ensure_prefix(nfsu::kMeshMetadataAlignment);
    }
    Bytes record(nfsu::kMeshMetadataRecordSize, 0);
    put_u32(record, nfsu::kMeshMetadataHashOffset, spec.hash);
    record[nfsu::kMeshMetadataTextureCountOffset] = static_cast<unsigned char>(
        spec.texture_count_byte_override >= 0
            ? spec.texture_count_byte_override
            : static_cast<int>(spec.texture_hashes.size()));
    record[nfsu::kMeshMetadataKeyCountOffset] = static_cast<unsigned char>(
        spec.key_count_byte_override >= 0
            ? spec.key_count_byte_override
            : static_cast<int>(spec.material_key_hashes.size()));
    for (std::size_t i = 0; i < spec.name.size() && i < nfsu::kMeshNameSize; ++i) {
        record[nfsu::kMeshMetadataNameOffset + i] =
            static_cast<unsigned char>(spec.name[i]);
    }
    if (spec.unterminated_name) {
        for (std::size_t i = 0; i < nfsu::kMeshNameSize; ++i) {
            record[nfsu::kMeshMetadataNameOffset + i] = 'A';
        }
    }
    if (spec.bad_name_byte != 0) {
        record[nfsu::kMeshMetadataNameOffset + spec.bad_name_index] =
            spec.bad_name_byte;
    }
    const int metadata_copies = spec.duplicate_mesh_metadata_chunk ? 2 : 1;
    for (int copy = 0; copy < metadata_copies; ++copy) {
        writer.begin(nfsu::kMeshMetadataId);
        const std::size_t prefix = writer.pad_to(nfsu::kMeshMetadataAlignment);
        break_prefix(spec.break_metadata_prefix && copy == 0, prefix, "metadata");
        writer.append(record);
        writer.end();
    }

    // A child whose declared payload reaches past the mesh container. It
    // comes after 0x00134011 so the mesh still has a readable name.
    if (spec.malformed_child_chunk) {
        append_u32(writer.bytes(), 0x00134017U);
        append_u32(writer.bytes(), 0x00FFFFFFU);
    }

    if ((!spec.texture_hashes.empty() || spec.force_empty_texture_chunk) &&
        !spec.drop_texture_chunk) {
        Bytes payload;
        for (std::size_t i = 0; i < spec.texture_hashes.size(); ++i) {
            append_u32(payload, spec.texture_hashes[i]);
            append_u32(payload,
                       spec.bad_list_entry >= 0 &&
                               static_cast<std::size_t>(spec.bad_list_entry) == i
                           ? 7U
                           : 0U);
        }
        const int copies = spec.duplicate_texture_chunk ? 2 : 1;
        for (int copy = 0; copy < copies; ++copy) {
            writer.leaf(nfsu::kTextureListId, payload);
        }
    }
    if (!spec.material_key_hashes.empty()) {
        Bytes payload;
        for (const std::uint32_t hash : spec.material_key_hashes) {
            append_u32(payload, hash);
            append_u32(payload, 0U);
        }
        writer.leaf(nfsu::kMaterialKeyListId, payload);
    }

    writer.begin(nfsu::kGeometryContainerId);

    // 0x00134900: 64-byte record at the 16-byte aligned offset.
    if (spec.break_geometry_prefix) {
        ensure_prefix(nfsu::kGeometryMetadataAlignment);
    }
    writer.begin(nfsu::kGeometryMetadataId);
    const std::size_t geometry_prefix =
        writer.pad_to(nfsu::kGeometryMetadataAlignment);
    break_prefix(spec.break_geometry_prefix, geometry_prefix,
                 "geometry metadata");
    Bytes metadata(nfsu::kGeometryMetadataSize, 0);
    put_u32(metadata, nfsu::kVertexFormatOffset, spec.vertex_format_flags);
    put_u32(metadata, nfsu::kMaterialCountOffset,
            spec.have_material_count_word_override
                ? spec.material_count_word_override
                : static_cast<std::uint32_t>(spec.materials.size()));
    put_u32(metadata, nfsu::kFaceCountOffset,
            spec.have_face_count_word_override ? spec.face_count_word_override
                                               : spec.face_count);
    put_u32(metadata, nfsu::kVertexCountOffset,
            spec.have_vertex_count_word_override ? spec.vertex_count_word_override
                                                 : spec.vertex_count);
    writer.append(metadata);
    writer.end();

    // 0x00134B01: vertex_count vertices of 24 bytes at the 128-byte
    // aligned offset. Positions are i, i + 0.5, i + 0.25 so that every
    // slot is distinguishable; colors and UVs likewise.
    const std::uint32_t stride = spec.vertex_stride_override != 0
                                     ? spec.vertex_stride_override
                                     : static_cast<std::uint32_t>(
                                           nfsu::kTrackVertexStride);
    Bytes vertices;
    for (std::uint32_t i = 0; i < spec.vertex_count; ++i) {
        Bytes vertex;
        append_f32(vertex, static_cast<float>(i));               // px
        append_f32(vertex, static_cast<float>(i) + 0.5F);        // pz
        append_f32(vertex, static_cast<float>(i) + 0.25F);       // py
        vertex.push_back(static_cast<unsigned char>(10 + i));
        vertex.push_back(static_cast<unsigned char>(20 + i));
        vertex.push_back(static_cast<unsigned char>(30 + i));
        vertex.push_back(0xFFU);
        append_f32(vertex, static_cast<float>(i) * 0.125F);      // u
        append_f32(vertex, static_cast<float>(i) * 0.0625F);     // v
        if (spec.non_finite_vertex && i == 0) {
            put_u32(vertex, 0, 0x7F800000U);  // +inf
        }
        vertex.resize(stride, 0x5AU);
        vertices.insert(vertices.end(), vertex.begin(), vertex.end());
    }
    if (spec.vertex_extra_bytes != 0) {
        vertices.insert(vertices.end(), spec.vertex_extra_bytes, 0x5AU);
    }
    const int vertex_copies = spec.duplicate_vertex_chunk ? 2 : 1;
    for (int copy = 0; copy < vertex_copies; ++copy) {
        if (spec.tiny_vertex_payload) {
            writer.leaf(nfsu::kVertexBufferId, Bytes(4, 0x11U));
            continue;
        }
        writer.begin(nfsu::kVertexBufferId);
        const std::size_t prefix = writer.pad_to(nfsu::kVertexDataAlignment);
        break_prefix(spec.break_vertex_prefix && copy == 0, prefix, "vertex");
        writer.append(vertices);
        writer.end();
    }

    // 0x00134B02: material records of 60 bytes at the 16-byte aligned offset.
    if (spec.break_material_prefix) {
        ensure_prefix(nfsu::kMaterialDataAlignment);
    }
    writer.begin(nfsu::kMaterialBufferId);
    const std::size_t material_prefix =
        writer.pad_to(nfsu::kMaterialDataAlignment);
    break_prefix(spec.break_material_prefix, material_prefix, "material");
    std::uint32_t first_index = 0;
    for (const MaterialSpec& material : spec.materials) {
        Bytes material_record;
        append_f32(material_record, -1.0F);
        append_f32(material_record, -2.0F);
        append_f32(material_record, -3.0F);
        append_u32(material_record, material.index_count);
        append_f32(material_record, 1.0F);
        append_f32(material_record, 2.0F);
        append_f32(material_record, 3.0F);
        append_u32(material_record, material.texture_slot);
        append_u32(material_record, material.material_key_slot);
        material_record.resize(52, 0);
        append_u32(material_record, material.have_first_index_override
                                        ? material.first_index_override
                                        : first_index);
        append_u32(material_record, material.flags);
        writer.append(material_record);
        first_index += material.index_count;
    }
    writer.end();

    // 0x00134B03: face_count triangles of 6 bytes at the 16-byte aligned
    // offset, then zero padding to a multiple of 4.
    if (spec.break_face_prefix) {
        ensure_prefix(nfsu::kFaceDataAlignment);
    }
    writer.begin(nfsu::kFaceBufferId);
    const std::size_t face_prefix = writer.pad_to(nfsu::kFaceDataAlignment);
    break_prefix(spec.break_face_prefix, face_prefix, "face");
    for (std::uint32_t i = 0; i < spec.face_count; ++i) {
        Bytes triangle;
        const std::uint32_t count = spec.vertex_count == 0 ? 1 : spec.vertex_count;
        append_u16(triangle, static_cast<std::uint16_t>((3 * i) % count));
        append_u16(triangle, static_cast<std::uint16_t>((3 * i + 1) % count));
        append_u16(triangle, static_cast<std::uint16_t>((3 * i + 2) % count));
        if (spec.out_of_range_index && i == 0) {
            put_u32(triangle, 0, spec.vertex_count);
        }
        writer.append(triangle);
    }
    if ((spec.face_count * 6U) % nfsu::kFaceTailAlignment != 0) {
        check(spec.bad_face_tail_byte < 2, "the face tail is only two bytes");
        writer.bytes().push_back(spec.bad_face_tail_byte == 0 ? 0x09U : 0x00U);
        writer.bytes().push_back(spec.bad_face_tail_byte == 1 ? 0x09U : 0x00U);
    }
    writer.end();

    writer.end();  // 0x80134100
    writer.end();  // 0x80134010
}

struct RegionSpec {
    std::vector<MeshSpec> meshes;
    bool write_header = true;
    bool write_hash_table = true;
    bool write_mesh_directory = false;
    bool decreasing_hashes = false;
    bool nonzero_hash_reserved = false;
    bool extra_hash_record = false;
    bool wrong_hash_value = false;
    bool leading_padding_chunk = true;
};

Bytes build_region(const RegionSpec& spec, std::uint64_t base) {
    Writer writer(base);
    writer.begin(nfsu::kGeometryRootId);
    if (spec.leading_padding_chunk) {
        writer.leaf(0x00000000U, Bytes{});
    }
    if (spec.write_header) {
        writer.begin(nfsu::kGeometryHeaderId);
        writer.leaf(0x00134002U, Bytes(128, 0));
        if (spec.write_hash_table) {
            std::vector<std::uint32_t> hashes;
            for (const MeshSpec& mesh : spec.meshes) {
                hashes.push_back(mesh.hash);
            }
            std::sort(hashes.begin(), hashes.end());
            if (spec.decreasing_hashes && hashes.size() >= 2) {
                std::swap(hashes.front(), hashes.back());
            }
            if (spec.wrong_hash_value && !hashes.empty()) {
                hashes.front() ^= 0x5A5A5A5AU;
                std::sort(hashes.begin(), hashes.end());
            }
            if (spec.extra_hash_record) {
                hashes.push_back(0xFFFFFFFFU);
            }
            Bytes payload;
            for (std::size_t i = 0; i < hashes.size(); ++i) {
                append_u32(payload, hashes[i]);
                append_u32(payload, spec.nonzero_hash_reserved && i == 0 ? 3U : 0U);
            }
            writer.leaf(nfsu::kMeshHashTableId, payload);
        }
        if (spec.write_mesh_directory) {
            writer.leaf(nfsu::kMeshDirectoryId, Bytes(20, 0));
        }
        writer.leaf(0x80134008U, Bytes{});
        writer.end();
    }
    for (const MeshSpec& mesh : spec.meshes) {
        write_mesh(writer, mesh);
    }
    writer.end();
    return writer.bytes();
}

nfsu::TrackRegionReport parse(const Bytes& region, std::uint64_t base) {
    return nfsu::parse_track_region(region.data(), region.size(), base);
}

nfsu::TrackRegionReport parse(
    const Bytes& region,
    std::uint64_t base,
    const nfsu::TrackLimits& limits) {
    return nfsu::parse_track_region(region.data(), region.size(), base, limits);
}

MeshSpec good_mesh(const std::string& name, std::uint32_t hash) {
    MeshSpec spec;
    spec.name = name;
    spec.hash = hash;
    spec.vertex_count = 6;
    spec.face_count = 3;
    spec.texture_hashes = {0xAAAA0001U, 0xAAAA0002U};
    spec.materials = {MaterialSpec{3, 0, 0xFFU, 0x4180U},
                      MaterialSpec{6, 1, 0xFFU, 0U}};
    return spec;
}

// ---------------------------------------------------------------------------

void test_vertex_format_table() {
    struct Case {
        std::uint32_t flags;
        std::uint32_t mask;
        std::uint32_t format;
        std::size_t stride;
    };
    // The retail rows are tried in order, so bit 0x01 wins over 0x40 and
    // 0x80, and bit 0x40 wins over 0x80. The last row has mask 0 and
    // matches everything left.
    const Case cases[] = {
        {0x00000000U, 0x00000000U, 0U, 24U},
        {0x00000040U, 0x00000040U, 2U, 24U},
        {0x00000080U, 0x00000080U, 5U, 36U},
        {0x00000001U, 0x00000001U, 4U, 60U},
        {0x00004081U, 0x00000001U, 4U, 60U},
        {0x00004180U, 0x00000080U, 5U, 36U},
        {0x0000C180U, 0x00000080U, 5U, 36U},
        {0x00001000U, 0x00000000U, 0U, 24U},
        {0x00008000U, 0x00000000U, 0U, 24U},
        {0x000000C0U, 0x00000040U, 2U, 24U},
        {0x000000C1U, 0x00000001U, 4U, 60U},
        {0xFFFFFFFFU, 0x00000001U, 4U, 60U},
    };
    for (const Case& item : cases) {
        const nfsu::TrackVertexFormat format = nfsu::track_vertex_format(item.flags);
        check(format.mask == item.mask && format.format == item.format &&
                  format.stride == item.stride,
              "vertex format for flags " + std::to_string(item.flags) +
                  ": got mask " + std::to_string(format.mask) + " format " +
                  std::to_string(format.format) + " stride " +
                  std::to_string(format.stride));
    }
    // Every 32-bit value selects a row, and only the three observed strides
    // can come out of the table.
    for (unsigned bit = 0; bit < 32; ++bit) {
        const nfsu::TrackVertexFormat format =
            nfsu::track_vertex_format(1U << bit);
        check(format.stride == 24U || format.stride == 36U || format.stride == 60U,
              "vertex format stride for a single bit is not 24, 36 or 60");
    }
}

void test_well_formed_region() {
    RegionSpec spec;
    spec.meshes = {good_mesh("ROAD_A", 0x00001111U),
                   good_mesh("WALL_B", 0x00002222U)};
    spec.meshes[1].material_key_hashes = {0xBBBB0001U};
    spec.meshes[1].materials[1].material_key_slot = 0;
    spec.meshes[1].vertex_format_flags = 0x00000040U;

    // Two different base offsets: the alignment rule is on absolute file
    // offsets, so a correct parser gives the same decoded values at both.
    for (const std::uint64_t base : {std::uint64_t{0}, std::uint64_t{23015540}}) {
        const Bytes region = build_region(spec, base);
        const nfsu::TrackRegionReport report = parse(region, base);
        const std::string tag = "base " + std::to_string(base) + ": ";
        check_issue_count(report, 0, tag + "well-formed region");
        check(report.ok(), tag + "report should be ok");
        check(report.mesh_table_valid, tag + "hash table should verify");
        check(report.meshes.size() == 2, tag + "two meshes");
        check(report.valid_mesh_count() == 2, tag + "two valid meshes");
        check(report.region.offset == base && report.region.size == region.size(),
              tag + "region selector round-trips");
        if (report.meshes.size() != 2) {
            return;
        }

        const nfsu::TrackMesh& mesh = report.meshes[0];
        check(mesh.name == "ROAD_A", tag + "mesh 0 name");
        check(mesh.mesh_hash == 0x00001111U, tag + "mesh 0 hash");
        check(mesh.has_hash_record, tag + "mesh 0 has a hash record");
        check(mesh.vertex_stride == nfsu::kTrackVertexStride,
              tag + "mesh 0 stride is 24");
        check(mesh.vertex_format == 0U, tag + "mesh 0 uses retail format 0");
        check(mesh.declared_vertex_count == 6 && mesh.declared_face_count == 3 &&
                  mesh.declared_material_count == 2,
              tag + "mesh 0 counts");
        check(mesh.declared_texture_count == 2 &&
                  mesh.declared_material_key_count == 0,
              tag + "mesh 0 list counts");
        check(mesh.vertices.size() == 6 && mesh.triangles.size() == 3,
              tag + "mesh 0 decoded sizes");
        check(mesh.vertex_data_offset % nfsu::kVertexDataAlignment == 0,
              tag + "vertex data is 128-byte aligned in the file");
        check(mesh.geometry_metadata_record_offset %
                      nfsu::kGeometryMetadataAlignment == 0,
              tag + "geometry metadata is 16-byte aligned in the file");
        check(mesh.mesh_metadata_record_offset % nfsu::kMeshMetadataAlignment == 0,
              tag + "mesh metadata is 16-byte aligned in the file");
        check(mesh.vertex_data_offset >= base &&
                  mesh.vertex_data_offset < base + region.size(),
              tag + "vertex data offset is absolute and inside the region");

        if (mesh.vertices.size() == 6) {
            for (std::size_t i = 0; i < 6; ++i) {
                const nfsu::TrackVertex& vertex = mesh.vertices[i];
                const float f = static_cast<float>(i);
                check(vertex.px == f, tag + "vertex px");
                check(vertex.pz == f + 0.5F, tag + "vertex pz (file slot 1)");
                check(vertex.py == f + 0.25F, tag + "vertex py (file slot 2)");
                check(vertex.color[0] == 10 + i && vertex.color[1] == 20 + i &&
                          vertex.color[2] == 30 + i && vertex.color[3] == 0xFFU,
                      tag + "vertex color bytes in stored order");
                check(vertex.u == f * 0.125F && vertex.v == f * 0.0625F,
                      tag + "vertex uv");
            }
        }
        check(mesh.triangles.size() == 3 && mesh.triangles[0].a == 0 &&
                  mesh.triangles[0].b == 1 && mesh.triangles[0].c == 2 &&
                  mesh.triangles[2].a == 0,
              tag + "triangle indices in source order");
        check(mesh.texture_hashes.size() == 2 &&
                  mesh.texture_hashes[0] == 0xAAAA0001U &&
                  mesh.texture_hashes[1] == 0xAAAA0002U,
              tag + "texture hash list");
        check(mesh.materials.size() == 2 && mesh.materials[0].index_count == 3 &&
                  mesh.materials[0].first_index == 0 &&
                  mesh.materials[1].first_index == 3 &&
                  mesh.materials[1].index_count == 6 &&
                  mesh.materials[0].flags == 0x4180U,
              tag + "material records");
        check(mesh.materials.size() == 2 &&
                  read_f32(mesh.materials[0].raw + 0) == -1.0F &&
                  read_f32(mesh.materials[0].raw + 16) == 1.0F,
              tag + "raw material record is preserved");

        const nfsu::TrackMesh& second = report.meshes[1];
        check(second.material_key_hashes.size() == 1 &&
                  second.material_key_hashes[0] == 0xBBBB0001U,
              tag + "material-key list is decoded when present");
        check(second.vertex_format_flags == 0x00000040U && second.vertex_format == 2U,
              tag + "flag bit 0x40 selects retail format 2, also 24 bytes");

        const nfsu::TrackRegionCost& cost = report.cost;
        check(cost.region_bytes == region.size(), tag + "cost region bytes");
        check(cost.geometry_roots == 1, tag + "one geometry root");
        check(cost.meshes == 2 && cost.valid_meshes == 2 && cost.empty_meshes == 0,
              tag + "cost mesh counts");
        check(cost.vertices == 12 && cost.triangles == 6 && cost.materials == 4,
              tag + "cost geometry totals");
        check(cost.source_vertex_bytes == 12 * 24 && cost.source_index_bytes == 36,
              tag + "cost source byte totals");
        check(cost.decoded_vertex_bytes == 12 * sizeof(nfsu::TrackVertex),
              tag + "cost decoded vertex bytes");
        check(cost.decoded_index_bytes == 6 * sizeof(nfsu::MeshTriangle),
              tag + "cost decoded index bytes");
        check(cost.distinct_texture_hashes == 2, tag + "cost distinct hashes");
        check(cost.largest_mesh_vertices == 6 && cost.largest_mesh_triangles == 3,
              tag + "cost largest mesh");
        check(cost.has_bounds && cost.bounds_min[0] == 0.0F &&
                  cost.bounds_max[0] == 5.0F && cost.bounds_min[1] == 0.25F &&
                  cost.bounds_max[1] == 5.25F && cost.bounds_min[2] == 0.5F &&
                  cost.bounds_max[2] == 5.5F,
              tag + "cost bounds over every valid vertex");
        check(cost.chunks == 23,
              tag + "the fixture walks exactly 23 chunks, got " +
                  std::to_string(cost.chunks));

        const std::vector<std::uint32_t> wanted =
            nfsu::track_region_texture_hashes(report);
        check(wanted.size() == 2 && wanted[0] == 0xAAAA0001U &&
                  wanted[1] == 0xAAAA0002U,
              tag + "wanted hashes are ascending and unique");
    }
}

// A region may hold more than one geometry root, with unrelated top-level
// chunks between them. Mesh positions keep running across roots, and each
// root's hash table must cover exactly its own meshes.
void test_multiple_geometry_roots() {
    const std::uint64_t base = 512;
    RegionSpec first;
    first.meshes = {good_mesh("ROOT0_A", 0x10U), good_mesh("ROOT0_B", 0x20U)};
    Bytes region = build_region(first, base);

    // An unrelated top-level chunk between the roots.
    Bytes filler;
    append_u32(filler, 0x00000000U);
    append_u32(filler, 8U);
    filler.insert(filler.end(), 8, 0x00U);
    region.insert(region.end(), filler.begin(), filler.end());

    RegionSpec second;
    second.meshes = {good_mesh("ROOT1_A", 0x30U), good_mesh("ROOT1_B", 0x40U),
                     good_mesh("ROOT1_C", 0x50U)};
    const Bytes tail = build_region(second, base + region.size());
    region.insert(region.end(), tail.begin(), tail.end());

    const nfsu::TrackRegionReport report = parse(region, base);
    check_issue_count(report, 0, "two geometry roots in one region");
    check(report.cost.geometry_roots == 2, "both roots are counted");
    check(report.meshes.size() == 5 && report.valid_mesh_count() == 5,
          "every mesh of both roots decodes");
    check(report.mesh_table_valid, "both hash tables verify");
    for (std::size_t i = 0; i < report.meshes.size(); ++i) {
        check(report.meshes[i].index == i,
              "mesh positions run across roots at " + std::to_string(i));
        check(report.meshes[i].has_hash_record,
              "mesh " + std::to_string(i) + " matched a hash record");
    }
    check(report.meshes.size() == 5 && report.meshes[1].name == "ROOT0_B" &&
              report.meshes[2].name == "ROOT1_A",
          "meshes stay in source order across the root boundary");
    check(report.cost.vertices == 30 && report.cost.triangles == 15,
          "cost totals cover both roots");

    // Break only the second root's table: the first must stay verified in
    // its own right, and the region must stop claiming a verified table.
    RegionSpec broken = second;
    broken.wrong_hash_value = true;
    Bytes mixed = build_region(first, base);
    mixed.insert(mixed.end(), filler.begin(), filler.end());
    const Bytes broken_tail = build_region(broken, base + mixed.size());
    mixed.insert(mixed.end(), broken_tail.begin(), broken_tail.end());
    const nfsu::TrackRegionReport mixed_report = parse(mixed, base);
    check(!mixed_report.ok(), "a broken table in the second root is reported");
    check(!mixed_report.mesh_table_valid, "the region table is not verified");
    check(mixed_report.valid_mesh_count() == 5,
          "the meshes themselves still decode");
    check(mixed_report.meshes.size() == 5 &&
              mixed_report.meshes[0].has_hash_record &&
              mixed_report.meshes[1].has_hash_record &&
              !mixed_report.meshes[4].has_hash_record,
          "only the sound root's meshes get hash records");
}

// A mesh that fails before its name hash is read leaves the owning root's
// hash table unverifiable, and the parser must say so instead of matching
// the wrong records.
void test_hash_table_needs_every_mesh_hash() {
    RegionSpec spec;
    MeshSpec broken = good_mesh("NO_HASH", 0x10U);
    broken.unterminated_name = true;
    spec.meshes = {broken, good_mesh("FINE", 0x20U)};
    const Bytes region = build_region(spec, 0);
    const nfsu::TrackRegionReport report = parse(region, 0);
    check(!report.ok(), "the unreadable mesh is reported");
    check(has_issue_with(report, "has no readable"),
          "the table diagnostic says the name hash could not be read");
    check(!report.mesh_table_valid, "the table is not verified");
    check(report.meshes.size() == 2 && !report.meshes[0].has_hash_record &&
              !report.meshes[1].has_hash_record,
          "no mesh of that root gets a hash record");
}

// The 0x00134003 records are in hash order, the meshes are in file order,
// and the two orders differ here. Each mesh must take the record that
// carries its own hash, not the record at its own position.
void test_hash_records_match_by_hash_not_position() {
    {
        RegionSpec spec;
        spec.meshes = {good_mesh("A", 0x30U), good_mesh("B", 0x20U),
                       good_mesh("C", 0x10U)};
        const Bytes region = build_region(spec, 4096);
        const nfsu::TrackRegionReport report = parse(region, 4096);
        check_issue_count(report, 0, "hash-order fixture");
        check(report.mesh_table_valid && report.meshes.size() == 3,
              "the table verifies");
        if (report.meshes.size() != 3) {
            return;
        }
        // Sorted table: 0x10, 0x20, 0x30 at table + 0, + 8, + 16.
        const std::uint64_t table = report.meshes[2].hash_record_offset;
        check(report.meshes[2].mesh_hash == 0x10U &&
                  report.meshes[1].hash_record_offset == table + 8 &&
                  report.meshes[0].hash_record_offset == table + 16,
              "each mesh takes the record holding its own hash");
        check(report.meshes[0].hash_record_offset >
                  report.meshes[1].hash_record_offset,
              "the first mesh takes the last record, because its hash is"
              " the largest");
    }
    {
        // Two meshes share a hash, and a third sits between them in hash
        // order: equal hashes go to equal records in source order.
        RegionSpec spec;
        spec.meshes = {good_mesh("X", 0x20U), good_mesh("Y", 0x10U),
                       good_mesh("Z", 0x20U)};
        const Bytes region = build_region(spec, 4096);
        const nfsu::TrackRegionReport report = parse(region, 4096);
        check_issue_count(report, 0, "duplicate-hash fixture");
        check(report.mesh_table_valid && report.meshes.size() == 3,
              "the duplicate table verifies");
        if (report.meshes.size() != 3) {
            return;
        }
        const std::uint64_t table = report.meshes[1].hash_record_offset;
        check(report.meshes[1].mesh_hash == 0x10U &&
                  report.meshes[0].hash_record_offset == table + 8 &&
                  report.meshes[2].hash_record_offset == table + 16,
              "the two 0x20 meshes take the two 0x20 records in source order");
    }
}

// A geometry root may hold a header and a hash table with no mesh at all.
// It is structurally sound, but there is nothing for the table to index,
// so the region must not claim a verified table.
void test_root_without_meshes() {
    RegionSpec good;
    good.meshes = {good_mesh("A", 0x10U)};
    Bytes region = build_region(good, 0);
    RegionSpec bare;  // no meshes, header and empty hash table only
    const Bytes tail = build_region(bare, region.size());
    region.insert(region.end(), tail.begin(), tail.end());

    const nfsu::TrackRegionReport report = parse(region, 0);
    check_issue_count(report, 0, "a root with no meshes");
    check(report.cost.geometry_roots == 2, "both roots are counted");
    check(report.meshes.size() == 1 && report.valid_mesh_count() == 1,
          "only the populated root contributes a mesh");
    check(report.mesh_table_valid,
          "an empty root indexes nothing, so it does not spoil the region");

    // A region whose only root has no mesh at all has nothing to verify.
    const Bytes only_bare = build_region(bare, 0);
    const nfsu::TrackRegionReport empty = parse(only_bare, 0);
    check_issue_count(empty, 0, "a region whose only root has no mesh");
    check(empty.meshes.empty(), "no mesh is decoded");
    check(!empty.mesh_table_valid,
          "a region with no mesh never reports a verified table");
}

void test_header_without_a_hash_table() {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x10U)};
    spec.write_hash_table = false;
    const Bytes region = build_region(spec, 0);
    const nfsu::TrackRegionReport report = parse(region, 0);
    check(!report.ok(), "a header with no hash table is reported");
    check_issue_count(report, 1, "header without a hash table");
    check(has_issue_with(report, "expected exactly one 0x00134003"),
          "the diagnostic names the missing table");
    check(report.valid_mesh_count() == 1, "the mesh itself still decodes");
    check(!report.mesh_table_valid, "the table is not verified");
}

void test_root_without_a_leading_padding_chunk() {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x10U)};
    spec.leading_padding_chunk = false;
    const Bytes region = build_region(spec, 64);
    const nfsu::TrackRegionReport report = parse(region, 64);
    check_issue_count(report, 0, "a root whose first child is its header");
    check(report.valid_mesh_count() == 1 && report.mesh_table_valid,
          "the mesh decodes without a leading padding chunk");
}

void test_parse_is_deterministic() {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x30U), good_mesh("B", 0x20U),
                   good_mesh("C", 0x10U)};
    const Bytes region = build_region(spec, 4096);
    const nfsu::TrackRegionReport first = parse(region, 4096);
    const nfsu::TrackRegionReport second = parse(region, 4096);
    check(first.ok() && second.ok(), "deterministic fixture parses cleanly");
    check(first.meshes.size() == second.meshes.size(), "same mesh count");
    bool same = first.cost.vertices == second.cost.vertices &&
                first.cost.triangles == second.cost.triangles &&
                first.cost.distinct_texture_hashes ==
                    second.cost.distinct_texture_hashes;
    for (std::size_t i = 0; i < first.meshes.size() && same; ++i) {
        same = first.meshes[i].name == second.meshes[i].name &&
               first.meshes[i].hash_record_offset ==
                   second.meshes[i].hash_record_offset &&
               first.meshes[i].vertex_data_offset ==
                   second.meshes[i].vertex_data_offset;
    }
    check(same, "two parses of the same bytes agree field for field");
}

void test_empty_mesh_is_not_an_error() {
    RegionSpec spec;
    spec.meshes = {good_mesh("REAL", 0x10U), empty_mesh_spec("PLACEHOLDER", 0x20U)};
    const Bytes region = build_region(spec, 128);
    const nfsu::TrackRegionReport report = parse(region, 128);
    check_issue_count(report, 0, "region with an empty mesh");
    check(report.ok(), "an empty mesh raises no issue");
    check(report.meshes.size() == 2, "both meshes are listed");
    if (report.meshes.size() != 2) {
        return;
    }
    check(report.meshes[1].empty && !report.meshes[1].valid,
          "the empty mesh is marked empty and not valid");
    check(report.meshes[1].name == "PLACEHOLDER", "the empty mesh keeps its name");
    check(report.meshes[1].vertices.empty() && report.meshes[1].materials.empty(),
          "the empty mesh carries no geometry");
    check(report.cost.empty_meshes == 1 && report.cost.valid_meshes == 1,
          "empty meshes are counted apart from valid ones");
    check(report.mesh_table_valid, "an empty mesh still gets a hash record");
    check(report.meshes[1].declared_texture_count == 0 &&
              report.meshes[1].texture_list_offset != 0,
          "a zero-byte 0x00134012 with a zero count byte is accepted, which"
          " is the shape every retail empty mesh has");
}

void test_mixed_counts_are_rejected() {
    RegionSpec spec;
    MeshSpec mesh = empty_mesh_spec("HALF_EMPTY", 0x10U);
    mesh.have_face_count_word_override = true;
    mesh.face_count_word_override = 4;
    spec.meshes = {mesh};
    const Bytes region = build_region(spec, 0);
    const nfsu::TrackRegionReport report = parse(region, 0);
    check(!report.ok(), "counts that are neither all zero nor all positive fail");
    check(has_issue_with(report, "neither all zero"),
          "the mixed-count diagnostic names the rule");
    check(!report.meshes.empty() && !report.meshes[0].valid &&
              !report.meshes[0].empty,
          "a mixed-count mesh is invalid, not empty");
}

void test_other_strides_are_refused() {
    struct Case {
        std::uint32_t flags;
        const char* stride;
        const char* format;
    };
    const Case cases[] = {
        {0x00004180U, "36", "5"},
        {0x00000080U, "36", "5"},
        {0x00004081U, "60", "4"},
        {0x00000001U, "60", "4"},
    };
    for (const Case& item : cases) {
        RegionSpec spec;
        MeshSpec mesh = good_mesh("OTHER_FORMAT", 0x10U);
        mesh.vertex_format_flags = item.flags;
        spec.meshes = {mesh};
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        const std::string tag =
            "flags " + std::to_string(item.flags) + ": ";
        check(!report.ok(), tag + "a non-24-byte format is refused");
        check(has_issue_with(report, std::string("stride of ") + item.stride),
              tag + "the diagnostic names the selected stride");
        check(has_issue_with(report, std::string("vertex format ") + item.format),
              tag + "the diagnostic names the selected retail format");
        check(!report.meshes.empty() && !report.meshes[0].valid,
              tag + "the mesh is not valid");
        check(!report.meshes.empty() &&
                  report.meshes[0].vertex_stride == std::stoul(item.stride),
              tag + "the mesh records the stride the game would use");
        check(report.cost.vertices == 0,
              tag + "no vertex of a refused mesh is counted");
    }
}

void test_wrong_vertex_payload_size() {
    RegionSpec spec;
    MeshSpec mesh = good_mesh("WRONG_STRIDE", 0x10U);
    mesh.vertex_stride_override = 36;  // car-sized payload, track flags
    spec.meshes = {mesh};
    const Bytes region = build_region(spec, 0);
    const nfsu::TrackRegionReport report = parse(region, 0);
    check(!report.ok(), "a vertex payload of the wrong size is refused");
    check(has_issue_with(report, "a stride of 36 bytes"),
          "the diagnostic derives the stride the payload would need");
    check(has_issue_with(report, "vertices of 24 bytes"),
          "the diagnostic names the stride the decoder wanted");
}

void test_vertex_payload_not_a_whole_stride() {
    RegionSpec spec;
    MeshSpec mesh = good_mesh("RAGGED", 0x10U);
    mesh.vertex_extra_bytes = 1;
    spec.meshes = {mesh};
    const Bytes region = build_region(spec, 0);
    const nfsu::TrackRegionReport report = parse(region, 0);
    check(!report.ok(), "a ragged vertex payload is refused");
    check(has_issue_with(report, "not a whole stride"),
          "the diagnostic says the payload has no whole stride");
}

void test_count_word_high_half() {
    RegionSpec spec;
    MeshSpec mesh = good_mesh("SIGN_EXTENDED", 0x10U);
    mesh.have_vertex_count_word_override = true;
    mesh.vertex_count_word_override = 0xFFFF0006U;
    spec.meshes = {mesh};
    const Bytes region = build_region(spec, 0);
    const nfsu::TrackRegionReport report = parse(region, 0);
    check(!report.ok(), "a count word with a non-zero high half is refused");
    check(has_issue_with(report, "high half must be zero"),
          "the diagnostic names the high half rule");
    check(has_issue_with(report, "0xFFFF0006"),
          "the diagnostic quotes the raw word");
}

void test_per_mesh_count_limits() {
    RegionSpec spec;
    spec.meshes = {good_mesh("BIG", 0x10U)};  // 6 vertices, 3 faces, 2 materials
    const Bytes region = build_region(spec, 0);
    struct Case {
        const char* what;
        std::uint32_t nfsu::TrackLimits::*field;
        std::uint32_t refuse;
        std::uint32_t accept;
        const char* text;
    };
    const Case cases[] = {
        {"vertex", &nfsu::TrackLimits::max_vertices_per_mesh, 5, 6,
         "vertex count 6 in chunk 0x00134900"},
        {"face", &nfsu::TrackLimits::max_faces_per_mesh, 2, 3,
         "face count 3 in chunk 0x00134900"},
        {"material", &nfsu::TrackLimits::max_materials_per_mesh, 1, 2,
         "material count 2 in chunk 0x00134900"},
    };
    for (const Case& item : cases) {
        const std::string tag = std::string(item.what) + " limit: ";
        nfsu::TrackLimits refusing;
        refusing.*item.field = item.refuse;
        const nfsu::TrackRegionReport refused = parse(region, 0, refusing);
        check(!refused.ok(), tag + "a count above the limit is refused");
        check(has_issue_with(refused, item.text),
              tag + "the diagnostic names the count and the chunk");
        check(has_issue_with(refused,
                             "exceeds the limit of " + std::to_string(item.refuse)),
              tag + "the diagnostic names the limit");
        // Exactly on the limit is still accepted.
        nfsu::TrackLimits accepting;
        accepting.*item.field = item.accept;
        const nfsu::TrackRegionReport accepted = parse(region, 0, accepting);
        check_issue_count(accepted, 0, tag + "a count exactly on the limit");
        check(accepted.valid_mesh_count() == 1,
              tag + "the mesh decodes at the limit");
    }
}

void test_region_vertex_and_triangle_limits() {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x10U), good_mesh("B", 0x20U)};
    const Bytes region = build_region(spec, 0);
    {
        // A budget smaller than a single mesh: the very first mesh must be
        // refused, not accepted through an underflowed subtraction.
        nfsu::TrackLimits limits;
        limits.max_vertices = 5;
        const nfsu::TrackRegionReport report = parse(region, 0, limits);
        check(!report.ok(), "a budget below one mesh refuses that mesh");
        check(report.valid_mesh_count() == 0,
              "no mesh is decoded under a budget below one mesh");
        check(has_issue_with(report, "exceed the limit of 5"),
              "the diagnostic names the budget");
    }
    {
        nfsu::TrackLimits limits;
        limits.max_triangles = 2;
        const nfsu::TrackRegionReport report = parse(region, 0, limits);
        check(!report.ok(), "a triangle budget below one mesh refuses it");
        check(report.valid_mesh_count() == 0,
              "no mesh is decoded under a triangle budget below one mesh");
    }
    {
        nfsu::TrackLimits limits;
        limits.max_vertices = 6;
        const nfsu::TrackRegionReport report = parse(region, 0, limits);
        check(!report.ok(), "the region vertex budget is enforced");
        check(has_issue_with(report, "exceed the limit of 6"),
              "the region vertex diagnostic names the budget");
        check(report.valid_mesh_count() == 1,
              "the mesh that fits is still decoded");
    }
    {
        nfsu::TrackLimits limits;
        limits.max_triangles = 3;
        const nfsu::TrackRegionReport report = parse(region, 0, limits);
        check(!report.ok(), "the region triangle budget is enforced");
        check(has_issue_with(report, "exceed the limit of 3"),
              "the region triangle diagnostic names the budget");
    }
}

void test_mesh_and_chunk_limits() {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x10U), good_mesh("B", 0x20U),
                   good_mesh("C", 0x30U)};
    const Bytes region = build_region(spec, 0);
    {
        nfsu::TrackLimits limits;
        limits.max_meshes = 2;
        const nfsu::TrackRegionReport report = parse(region, 0, limits);
        check(!report.ok(), "the mesh limit is enforced");
        check(has_issue_with(report, "mesh limit 2 exceeded"),
              "the mesh limit diagnostic names the limit");
        check(report.meshes.size() == 2, "no mesh past the limit is decoded");
    }
    {
        nfsu::TrackLimits limits;
        limits.max_chunks = 4;
        const nfsu::TrackRegionReport report = parse(region, 0, limits);
        check(!report.ok(), "the chunk limit is enforced");
        check(has_issue_with(report, "chunk limit 4 exceeded"),
              "the chunk limit diagnostic names the limit");
    }
}

void test_alignment_prefix_bytes() {
    {
        RegionSpec spec;
        MeshSpec mesh = good_mesh("BAD_VERTEX_PREFIX", 0x10U);
        mesh.break_vertex_prefix = true;
        spec.meshes = {mesh};
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check(!report.ok(), "a non-0x11 vertex prefix byte is refused");
        check(has_issue_with(report, "alignment prefix byte"),
              "the diagnostic names the prefix byte");
    }
    {
        RegionSpec spec;
        MeshSpec mesh = good_mesh("BAD_META_PREFIX", 0x10U);
        mesh.break_metadata_prefix = true;
        spec.meshes = {mesh};
        const Bytes region = build_region(spec, 8);
        const nfsu::TrackRegionReport report = parse(region, 8);
        check(!report.ok(), "a non-0x11 metadata prefix byte is refused");
        check(has_issue_with(report, "expected 0x11"),
              "the metadata prefix diagnostic names the expected byte");
    }
}

void test_index_and_material_validation() {
    struct Case {
        const char* what;
        const char* text;
        // Issues the whole region must raise. A mesh that fails before its
        // name hash is read also leaves the hash table unverifiable, which
        // is a second issue.
        std::size_t issues;
        MeshSpec (*make)();
    };
    // Each lambda returns a mesh with exactly one defect.
    const auto out_of_range = []() {
        MeshSpec mesh = good_mesh("OUT_OF_RANGE", 0x10U);
        mesh.out_of_range_index = true;
        return mesh;
    };
    const auto bad_first_index = []() {
        MeshSpec mesh = good_mesh("BAD_FIRST", 0x10U);
        mesh.materials[1].have_first_index_override = true;
        mesh.materials[1].first_index_override = 0;
        return mesh;
    };
    const auto not_multiple_of_three = []() {
        MeshSpec mesh = good_mesh("NOT_TRIPLE", 0x10U);
        mesh.materials = {MaterialSpec{4, 0, 0xFFU, 0U},
                          MaterialSpec{5, 1, 0xFFU, 0U}};
        return mesh;
    };
    const auto bad_texture_slot = []() {
        MeshSpec mesh = good_mesh("BAD_SLOT", 0x10U);
        mesh.materials[1].texture_slot = 7;
        return mesh;
    };
    const auto bad_key_slot = []() {
        MeshSpec mesh = good_mesh("BAD_KEY", 0x10U);
        mesh.materials[1].material_key_slot = 3;
        return mesh;
    };
    const auto short_coverage = []() {
        MeshSpec mesh = good_mesh("SHORT", 0x10U);
        mesh.materials = {MaterialSpec{3, 0, 0xFFU, 0U}};
        return mesh;
    };
    const auto non_finite = []() {
        MeshSpec mesh = good_mesh("NON_FINITE", 0x10U);
        mesh.non_finite_vertex = true;
        return mesh;
    };
    const auto bad_tail = []() {
        MeshSpec mesh = good_mesh("BAD_TAIL", 0x10U);
        mesh.bad_face_tail_byte = 0;
        return mesh;
    };
    const auto bad_second_tail = []() {
        MeshSpec mesh = good_mesh("BAD_SECOND_TAIL", 0x10U);
        mesh.bad_face_tail_byte = 1;
        return mesh;
    };
    const auto bad_name = []() {
        MeshSpec mesh = good_mesh("BAD_NAME", 0x10U);
        mesh.bad_name_byte = 0x01U;
        mesh.bad_name_index = 1;
        return mesh;
    };
    const auto high_byte_name = []() {
        MeshSpec mesh = good_mesh("HIGH_BYTE_NAME", 0x10U);
        mesh.bad_name_byte = 0xE9U;
        mesh.bad_name_index = 3;
        return mesh;
    };
    const auto long_name = []() {
        MeshSpec mesh = good_mesh("LONG_NAME", 0x10U);
        mesh.unterminated_name = true;
        return mesh;
    };
    const auto second_word = []() {
        MeshSpec mesh = good_mesh("SECOND_WORD", 0x10U);
        mesh.bad_list_entry = 0;
        return mesh;
    };
    const auto later_second_word = []() {
        MeshSpec mesh = good_mesh("LATER_SECOND_WORD", 0x10U);
        mesh.bad_list_entry = 1;
        return mesh;
    };
    const auto index_count_too_large = []() {
        MeshSpec mesh = good_mesh("TOO_MANY_INDICES", 0x10U);
        mesh.materials = {MaterialSpec{3, 0, 0xFFU, 0U},
                          MaterialSpec{12, 1, 0xFFU, 0U}};
        return mesh;
    };
    const auto duplicate_texture_list = []() {
        MeshSpec mesh = good_mesh("TWO_TEXTURE_LISTS", 0x10U);
        mesh.duplicate_texture_chunk = true;
        return mesh;
    };
    const auto duplicate_metadata = []() {
        MeshSpec mesh = good_mesh("TWO_METADATA", 0x10U);
        mesh.duplicate_mesh_metadata_chunk = true;
        return mesh;
    };
    const auto duplicate_vertices = []() {
        MeshSpec mesh = good_mesh("TWO_VERTEX_BUFFERS", 0x10U);
        mesh.duplicate_vertex_chunk = true;
        return mesh;
    };
    const auto malformed_child = []() {
        MeshSpec mesh = good_mesh("MALFORMED_CHILD", 0x10U);
        mesh.malformed_child_chunk = true;
        return mesh;
    };
    const auto broken_geometry_prefix = []() {
        MeshSpec mesh = good_mesh("BAD_GEOMETRY_PREFIX", 0x10U);
        mesh.break_geometry_prefix = true;
        return mesh;
    };
    const auto broken_material_prefix = []() {
        MeshSpec mesh = good_mesh("BAD_MATERIAL_PREFIX", 0x10U);
        mesh.break_material_prefix = true;
        return mesh;
    };
    const auto broken_face_prefix = []() {
        MeshSpec mesh = good_mesh("BAD_FACE_PREFIX", 0x10U);
        mesh.break_face_prefix = true;
        return mesh;
    };
    const auto tiny_vertex_buffer = []() {
        MeshSpec mesh = good_mesh("TINY_VERTEX_BUFFER", 0x10U);
        mesh.tiny_vertex_payload = true;
        return mesh;
    };
    const auto material_count_high_half = []() {
        MeshSpec mesh = good_mesh("MATERIAL_HIGH_HALF", 0x10U);
        mesh.have_material_count_word_override = true;
        mesh.material_count_word_override = 0x00010002U;
        return mesh;
    };
    const auto material_count_disagrees = []() {
        MeshSpec mesh = good_mesh("MATERIAL_COUNT_LIES", 0x10U);
        mesh.have_material_count_word_override = true;
        mesh.material_count_word_override = 3;
        return mesh;
    };
    const auto face_count_high_half = []() {
        MeshSpec mesh = good_mesh("FACE_HIGH_HALF", 0x10U);
        mesh.have_face_count_word_override = true;
        mesh.face_count_word_override = 0xFFFF0003U;
        return mesh;
    };
    const auto missing_list = []() {
        MeshSpec mesh = good_mesh("NO_TEXTURE_LIST", 0x10U);
        mesh.drop_texture_chunk = true;
        return mesh;
    };
    const auto key_count_disagrees = []() {
        MeshSpec mesh = good_mesh("KEY_COUNT_LIES", 0x10U);
        mesh.material_key_hashes = {0xBBBB0001U};
        mesh.key_count_byte_override = 0;
        return mesh;
    };
    const auto texture_count_too_high = []() {
        MeshSpec mesh = good_mesh("TEX_COUNT_HIGH", 0x10U);
        mesh.texture_count_byte_override = 3;
        return mesh;
    };
    const auto texture_count_zero_with_entries = []() {
        MeshSpec mesh = good_mesh("TEX_COUNT_ZERO", 0x10U);
        mesh.texture_count_byte_override = 0;
        return mesh;
    };

    const Case cases[] = {
        {"out of range index", "are out of range for", 1, out_of_range},
        {"first index not the running total", "is not the running total", 1,
         bad_first_index},
        {"index count not a multiple of 3", "not a multiple of 3", 1,
         not_multiple_of_three},
        {"index count past the remaining indices", "remaining indices", 1,
         index_count_too_large},
        {"texture slot outside the list", "is outside the", 1, bad_texture_slot},
        {"material-key slot outside the list", "neither 0xFF nor inside", 1,
         bad_key_slot},
        {"materials do not cover the faces", "the face count needs", 1,
         short_coverage},
        {"non-finite vertex float", "non-finite float", 1, non_finite},
        {"non-zero face tail", "tail padding byte 0", 1, bad_tail},
        {"non-zero second face tail byte", "tail padding byte 1", 1,
         bad_second_tail},
        {"non-ASCII mesh name", "expected printable ASCII", 2, bad_name},
        {"mesh name byte above 0x7E", "mesh name byte 3 is 0xE9", 2,
         high_byte_name},
        {"unterminated mesh name", "is not NUL-terminated", 2, long_name},
        {"non-zero list second word", "entry 0 has a non-zero second word", 1,
         second_word},
        {"non-zero second word in a later entry",
         "entry 1 has a non-zero second word", 1, later_second_word},
        {"texture list missing", "but the mesh has no 0x00134012", 1,
         missing_list},
        {"material-key count byte disagrees with the chunk",
         "count byte of 0x00134011 is 0 (0 bytes)", 1, key_count_disagrees},
        {"texture count byte above the chunk",
         "count byte of 0x00134011 is 3 (24 bytes)", 1, texture_count_too_high},
        {"texture count byte zero with entries present",
         "count byte of 0x00134011 is 0 (0 bytes)", 1,
         texture_count_zero_with_entries},
        {"two texture lists", "expected at most one 0x00134012 chunk, found 2",
         1, duplicate_texture_list},
        {"two mesh metadata chunks",
         "expected exactly one 0x00134011 chunk, found 2", 2, duplicate_metadata},
        {"two vertex buffers", "expected exactly one 0x00134B01 chunk, found 2",
         1, duplicate_vertices},
        {"malformed mesh container child", "mesh container is malformed", 2,
         malformed_child},
        {"broken geometry metadata prefix", "alignment prefix byte", 1,
         broken_geometry_prefix},
        {"broken material prefix", "alignment prefix byte", 1,
         broken_material_prefix},
        {"broken face prefix", "alignment prefix byte", 1, broken_face_prefix},
        {"vertex payload shorter than its alignment prefix",
         "vertices of 24 bytes", 1, tiny_vertex_buffer},
        {"material count word with a high half",
         "material count word at file offset", 1, material_count_high_half},
        {"face count word with a high half", "face count word at file offset",
         1, face_count_high_half},
        {"material count disagrees with the buffer",
         "3 material records of 60 bytes", 1, material_count_disagrees},
    };
    for (const Case& item : cases) {
        RegionSpec spec;
        spec.meshes = {item.make()};
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        const std::string tag = std::string(item.what) + ": ";
        check(!report.ok(), tag + "the defect is reported");
        const std::string seen = report.issues.empty()
                                     ? std::string("<none>")
                                     : report.issues[0].message;
        check_issue_count(report, item.issues, tag + "issue count");
        check(has_issue_with(report, item.text),
              tag + "the diagnostic contains " + item.text + " (got: " + seen +
                  ")");
        check(!report.meshes.empty() && !report.meshes[0].valid,
              tag + "the mesh is not valid");
        check(report.cost.valid_meshes == 0 && report.cost.vertices == 0,
              tag + "nothing of a rejected mesh reaches the cost statistics");
        if (!report.meshes.empty()) {
            const nfsu::TrackMesh& mesh = report.meshes[0];
            check(mesh.vertices.empty() && mesh.triangles.empty() &&
                      mesh.materials.empty() && mesh.texture_hashes.empty() &&
                      mesh.material_key_hashes.empty(),
                  tag + "a rejected mesh keeps no decoded data");
        }
    }
}

void test_hash_table_rules() {
    {
        RegionSpec spec;
        spec.meshes = {good_mesh("A", 0x10U), good_mesh("B", 0x20U)};
        spec.extra_hash_record = true;
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check(!report.ok(), "a hash table with too many records is refused");
        check(has_issue_with(report, "mesh containers"),
              "the count diagnostic names the mesh containers");
        check(!report.mesh_table_valid, "the table is not verified");
    }
    {
        RegionSpec spec;
        spec.meshes = {good_mesh("A", 0x10U), good_mesh("B", 0x20U),
                       good_mesh("C", 0x30U)};
        spec.decreasing_hashes = true;
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check(!report.ok(), "a decreasing hash table is refused");
        check(has_issue_with(report, "must not decrease"),
              "the ordering diagnostic names the rule");
    }
    {
        RegionSpec spec;
        spec.meshes = {good_mesh("A", 0x10U), good_mesh("B", 0x20U)};
        spec.wrong_hash_value = true;
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check(!report.ok(), "a hash table that lists another hash is refused");
        check(has_issue_with(report, "does not list exactly"),
              "the multiset diagnostic names the rule");
        check(report.valid_mesh_count() == 2,
              "the meshes themselves still decode");
        check(!report.mesh_table_valid, "the table is not verified");
    }
    {
        RegionSpec spec;
        spec.meshes = {good_mesh("A", 0x10U)};
        spec.nonzero_hash_reserved = true;
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check(!report.ok(), "a non-zero reserved word is refused");
        check(has_issue_with(report, "nonzero reserved word"),
              "the reserved-word diagnostic names the field");
    }
    {
        RegionSpec spec;
        spec.meshes = {good_mesh("A", 0x10U)};
        spec.write_mesh_directory = true;
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check(!report.ok(), "a 0x00134004 mesh directory is refused");
        check(has_issue_with(report, "does not read one"),
              "the directory diagnostic says the decoder does not read one");
    }
    {
        // Two meshes with the same name hash: retail track roots do this.
        RegionSpec spec;
        spec.meshes = {good_mesh("TWIN_A", 0x40U), good_mesh("TWIN_B", 0x40U)};
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check_issue_count(report, 0, "duplicate mesh hashes");
        check(report.mesh_table_valid, "duplicate hashes still verify");
        check(report.meshes.size() == 2 &&
                  report.meshes[0].hash_record_offset !=
                      report.meshes[1].hash_record_offset,
              "equal hashes take different records, in source order");
        check(report.meshes.size() == 2 &&
                  report.meshes[0].hash_record_offset <
                      report.meshes[1].hash_record_offset,
              "the first mesh takes the first record");
    }
    {
        // No header at all: the meshes still decode, and the table simply
        // cannot be verified.
        RegionSpec spec;
        spec.meshes = {good_mesh("A", 0x10U)};
        spec.write_header = false;
        const Bytes region = build_region(spec, 0);
        const nfsu::TrackRegionReport report = parse(region, 0);
        check_issue_count(report, 0, "region without a geometry header");
        check(report.valid_mesh_count() == 1, "the mesh decodes");
        check(!report.mesh_table_valid, "without a table nothing is verified");
    }
}

void test_region_boundaries() {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x10U)};
    const Bytes region = build_region(spec, 0);

    {
        const nfsu::TrackRegionReport report =
            nfsu::parse_track_region(region.data(), region.size() - 4, 0);
        check(!report.ok(), "a region that cuts a chunk short is refused");
        check(has_issue_with(report, "remain in its parent") ||
                  has_issue_with(report, "trailing bytes"),
              "the truncation diagnostic names the boundary");
    }
    {
        Bytes padded = region;
        padded.push_back(0);
        padded.push_back(0);
        const nfsu::TrackRegionReport report =
            nfsu::parse_track_region(padded.data(), padded.size(), 0);
        check(!report.ok(), "trailing bytes shorter than a header are refused");
        check(has_issue_with(report, "trailing bytes are smaller"),
              "the trailing-byte diagnostic names the header size");
    }
    {
        const nfsu::TrackRegionReport report =
            nfsu::parse_track_region(region.data(), 4, 0);
        check(!report.ok(), "a region shorter than a chunk header is refused");
        check(has_issue_with(report, "a chunk header needs"),
              "the short-region diagnostic names the header size");
    }
    {
        Bytes only_padding;
        append_u32(only_padding, 0x00000000U);
        append_u32(only_padding, 0U);
        const nfsu::TrackRegionReport report =
            nfsu::parse_track_region(only_padding.data(), only_padding.size(), 0);
        check(!report.ok(), "a region without a geometry root is refused");
        check(has_issue_with(report, "no geometry root chunk"),
              "the missing-root diagnostic names the chunk id");
    }
    {
        const nfsu::TrackRegionReport report =
            nfsu::parse_track_region(nullptr, 16, 0);
        check(!report.ok(), "a null buffer with a non-zero size is refused");
        check(has_issue_with(report, "null region buffer"),
              "the null-buffer diagnostic is explicit");
    }
    {
        nfsu::TrackLimits limits;
        limits.max_region_bytes = 16;
        const nfsu::TrackRegionReport report = parse(region, 0, limits);
        check(!report.ok(), "a region above the byte budget is refused");
        check(has_issue_with(report, "limit is 16"),
              "the region budget diagnostic names the limit");
        check(report.meshes.empty(), "nothing is parsed past the budget");
    }
    {
        // A base offset so close to the top of the address space that the
        // region end would wrap.
        const nfsu::TrackRegionReport report = nfsu::parse_track_region(
            region.data(), region.size(),
            std::numeric_limits<std::uint64_t>::max() - 8);
        check(!report.ok(), "a region whose end offset overflows is refused");
        check(has_issue_with(report, "overflows"),
              "the overflow diagnostic is explicit");
    }
    {
        // A chunk whose declared payload reaches past the region end.
        Bytes broken = region;
        put_u32(broken, 4, 0xFFFFFF00U);
        const nfsu::TrackRegionReport report =
            nfsu::parse_track_region(broken.data(), broken.size(), 0);
        check(!report.ok(), "a chunk that overruns the region is refused");
        check(has_issue_with(report, "remain in its parent"),
              "the overrun diagnostic names the parent");
    }
}

void test_load_track_region_reads_only_the_region() {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x10U), good_mesh("B", 0x20U)};

    const std::uint64_t base = 4096;
    const Bytes region = build_region(spec, base);
    Bytes file(static_cast<std::size_t>(base), 0xCDU);
    file.insert(file.end(), region.begin(), region.end());
    // Trailing bytes that are not a valid chunk tree at all: a correct
    // loader must never look at them.
    file.insert(file.end(), 1024, 0xEEU);

    const fs::path path =
        fs::temp_directory_path() / "nfsu_track_region_test.bin";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.data()),
                  static_cast<std::streamsize>(file.size()));
        check(static_cast<bool>(out), "the fixture file is written");
    }

    nfsu::TrackRegionSelector selector;
    selector.offset = base;
    selector.size = region.size();
    std::vector<unsigned char> bytes;
    nfsu::TrackRegionReport report =
        nfsu::load_track_region(path, selector, bytes, nfsu::TrackLimits{});
    check_issue_count(report, 0, "loading the exact region");
    check(bytes.size() == region.size(),
          "exactly the region bytes are read, not the whole file");
    check(bytes == region, "the bytes read are the region bytes");
    check(report.file_size == file.size(), "the file size is reported");
    check(report.valid_mesh_count() == 2, "both meshes decode from the file");
    check(report.cost.region_bytes == region.size(), "cost names the region size");

    selector.size = 0;
    report = nfsu::load_track_region(path, selector, bytes, nfsu::TrackLimits{});
    check(!report.ok() && has_issue_with(report, "region size is zero"),
          "a zero-size region is refused");
    check(bytes.empty(), "no bytes are kept for a refused region");

    selector.offset = file.size() - 4;
    selector.size = 64;
    report = nfsu::load_track_region(path, selector, bytes, nfsu::TrackLimits{});
    check(!report.ok() && has_issue_with(report, "outside the"),
          "a region that runs past the end of the file is refused");
    check(bytes.empty(), "no bytes are read for an out-of-range region");

    selector.offset = file.size() + 1;
    selector.size = 16;
    report = nfsu::load_track_region(path, selector, bytes, nfsu::TrackLimits{});
    check(!report.ok() && has_issue_with(report, "outside the"),
          "a region that starts past the end of the file is refused");

    selector.offset = base;
    selector.size = region.size();
    nfsu::TrackLimits limits;
    limits.max_region_bytes = 8;
    report = nfsu::load_track_region(path, selector, bytes, limits);
    check(!report.ok() && has_issue_with(report, "limit is 8"),
          "the loader enforces the region byte budget before reading");
    check(bytes.empty(), "nothing is read when the budget refuses the region");

    report = nfsu::load_track_region(path / "missing", selector, bytes,
                                     nfsu::TrackLimits{});
    check(!report.ok() && has_issue_with(report, "failed to read file size"),
          "a missing file is refused");

    std::error_code error;
    fs::remove(path, error);
}

// ---------------------------------------------------------------------------
// Prepared region

nfsu::RenderTexture make_texture(std::uint32_t hash, std::uint32_t size) {
    nfsu::RenderTexture texture;
    texture.hash = hash;
    texture.claims[0] = hash;
    texture.claim_count = 1;
    texture.width = size;
    texture.height = size;
    texture.rgba.assign(static_cast<std::size_t>(size) * size * 4, 0x80U);
    texture.valid = true;
    return texture;
}

nfsu::TrackRegionReport two_mesh_region(Bytes& region) {
    RegionSpec spec;
    spec.meshes = {good_mesh("A", 0x10U), good_mesh("B", 0x20U)};
    spec.meshes[1].texture_hashes = {0xAAAA0002U, 0xAAAA0003U};
    region = build_region(spec, 0);
    return parse(region, 0);
}

void test_prepared_region() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);
    check(report.ok() && report.valid_mesh_count() == 2,
          "the prepared-region fixture parses cleanly");

    nfsu::RenderTextureSet textures;
    textures.textures.push_back(make_texture(0xAAAA0001U, 4));
    textures.textures.push_back(make_texture(0xAAAA0002U, 2));
    textures.textures.push_back(make_texture(0xAAAA0003U, 2));
    // A texture nothing asks for must not be copied into the region.
    textures.textures.push_back(make_texture(0x12345678U, 8));
    for (const nfsu::RenderTexture& texture : textures.textures) {
        textures.decoded_bytes += texture.rgba.size();
    }

    nfsu::PreparedTrackRegion prepared;
    std::string error;
    bool ok = nfsu::prepare_track_region(report, textures,
                                         nfsu::TrackSceneLimits{}, prepared,
                                         error);
    check(ok, "the region prepares: " + error);
    ok = nfsu::validate_prepared_track_region(prepared, error);
    check(ok, "the prepared region validates: " + error);
    check(prepared.meshes.size() == 2, "two prepared meshes");
    check(prepared.draws.size() == 4, "one draw per material record");
    check(prepared.cost.draws == 4 && prepared.cost.drawable_draws == 4 &&
              prepared.cost.resolved_draws == 4,
          "every draw resolves");
    check(prepared.textures.size() == 3,
          "only the three textures the region names are copied");
    check(prepared.cost.texture_bytes == 4 * 4 * 4 + 2 * 2 * 4 + 2 * 2 * 4,
          "texture bytes count only the copied textures");
    check(prepared.cost.vertices == 12 && prepared.cost.triangles == 6,
          "prepared geometry totals");
    check(prepared.cost.vertex_buffer_bytes == 12 * nfsu::kTrackVertexStride,
          "vertex buffer bytes use the source stride");
    check(prepared.cost.index_buffer_bytes == 6 * 6,
          "index buffer bytes use the source index stride");
    check(prepared.cost.drawn_triangles == 6, "drawn triangles");
    check(prepared.cost.largest_draw_triangles == 2, "largest draw");
    check(prepared.unresolved_hashes.empty(), "nothing is unresolved");
    check(prepared.skipped_invalid_meshes.empty() &&
              prepared.skipped_empty_meshes.empty(),
          "nothing is skipped");
    check(prepared.region.size == region.size(), "the region selector is copied");

    // Draw 1 and draw 2 share hash 0xAAAA0002, so a backend that binds on
    // change issues three binds for four draws.
    check(prepared.draws.size() == 4 && prepared.draws[1].texture_hash == 0xAAAA0002U &&
              prepared.draws[2].texture_hash == 0xAAAA0002U &&
              prepared.draws[1].texture == prepared.draws[2].texture,
          "the shared hash resolves to one texture handle");
    check(prepared.cost.texture_binds == 3,
          "texture binds count only changes in draw order, got " +
              std::to_string(prepared.cost.texture_binds));
    check(prepared.draws[0].texture == 0 && prepared.draws[1].texture == 1 &&
              prepared.draws[3].texture == 2,
          "texture handles are assigned in order of first use");
    for (const nfsu::TrackDraw& draw : prepared.draws) {
        check(draw.raw[12] == static_cast<unsigned char>(draw.index_count),
              "the raw material record travels with the draw");
    }
}

void test_prepared_region_bindings() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);

    {
        // No source has 0xAAAA0003: that hash is unresolved.
        nfsu::RenderTextureSet textures;
        textures.textures.push_back(make_texture(0xAAAA0001U, 2));
        textures.textures.push_back(make_texture(0xAAAA0002U, 2));
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        bool ok = nfsu::prepare_track_region(
            report, textures, nfsu::TrackSceneLimits{}, prepared, error);
        check(ok, "a region with an unresolved hash still prepares: " + error);
        check(prepared.cost.unresolved_draws == 1 &&
                  prepared.cost.resolved_draws == 3,
              "one draw is unresolved");
        check(prepared.unresolved_hashes.size() == 1 &&
                  prepared.unresolved_hashes[0] == 0xAAAA0003U,
              "the unresolved hash is listed");
        check(prepared.draws.back().texture == nfsu::kNoHandle,
              "an unresolved draw names no texture");
        check(prepared.draws.back().drawable,
              "an unresolved draw is still drawable with a fallback");
        ok = nfsu::validate_prepared_track_region(prepared, error);
        check(ok, "the unresolved region validates: " + error);
    }
    {
        // An unusable claim blocks the hash without providing pixels.
        nfsu::RenderTextureSet textures;
        textures.textures.push_back(make_texture(0xAAAA0001U, 2));
        textures.textures.push_back(make_texture(0xAAAA0002U, 2));
        textures.unusable_claims.push_back(
            nfsu::UnusableClaim{0xAAAA0003U, 1, 0, 0});
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        bool ok = nfsu::prepare_track_region(
            report, textures, nfsu::TrackSceneLimits{}, prepared, error);
        check(ok, "a region with an unusable claim prepares: " + error);
        check(prepared.cost.unusable_draws == 1 && prepared.unresolved_hashes.empty(),
              "an unusable claim is not an unresolved hash");
        ok = nfsu::validate_prepared_track_region(prepared, error);
        check(ok, "the unusable region validates: " + error);
    }
    {
        // Two usable textures claim the same hash: ambiguous, so unusable.
        nfsu::RenderTextureSet textures;
        textures.textures.push_back(make_texture(0xAAAA0001U, 2));
        textures.textures.push_back(make_texture(0xAAAA0002U, 2));
        textures.textures.push_back(make_texture(0xAAAA0002U, 4));
        textures.textures.push_back(make_texture(0xAAAA0003U, 2));
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        bool ok = nfsu::prepare_track_region(
            report, textures, nfsu::TrackSceneLimits{}, prepared, error);
        check(ok, "an ambiguous hash prepares: " + error);
        check(prepared.cost.unusable_draws == 2,
              "both draws that name the ambiguous hash are unusable");
        ok = nfsu::validate_prepared_track_region(prepared, error);
        check(ok, "the ambiguous region validates: " + error);
    }
}

void test_prepared_region_skips_empty_and_invalid() {
    RegionSpec spec;
    spec.meshes = {good_mesh("GOOD", 0x10U), empty_mesh_spec("EMPTY", 0x20U),
                   good_mesh("BROKEN", 0x30U)};
    spec.meshes[2].out_of_range_index = true;
    const Bytes region = build_region(spec, 0);
    const nfsu::TrackRegionReport report = parse(region, 0);
    check(!report.ok(), "the broken mesh is reported");
    check(report.valid_mesh_count() == 1, "only the good mesh is valid");

    nfsu::RenderTextureSet textures;
    textures.textures.push_back(make_texture(0xAAAA0001U, 2));
    textures.textures.push_back(make_texture(0xAAAA0002U, 2));
    nfsu::PreparedTrackRegion prepared;
    std::string error;
    bool ok = nfsu::prepare_track_region(report, textures,
                                         nfsu::TrackSceneLimits{}, prepared,
                                         error);
    check(ok, "a region with a skipped mesh prepares: " + error);
    check(prepared.meshes.size() == 1, "only the valid mesh is prepared");
    check(prepared.skipped_empty_meshes.size() == 1 &&
              prepared.skipped_empty_meshes[0] == 1,
          "the empty mesh is recorded as empty");
    check(prepared.skipped_invalid_meshes.size() == 1 &&
              prepared.skipped_invalid_meshes[0] == 2,
          "the broken mesh is recorded as invalid");
    check(prepared.source_mesh_count == 3, "the source mesh count is kept");
    ok = nfsu::validate_prepared_track_region(prepared, error);
    check(ok, "the prepared region validates: " + error);
}

// A backend binds only when the texture changes, and every group that is
// not resolved shares one fallback binding. The first drawable draw always
// counts as a bind, even when it is that fallback.
void test_texture_binds_and_unresolved_order() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);
    // Draw order is (A slot 0) 0xAAAA0001, (A slot 1) 0xAAAA0002,
    // (B slot 0) 0xAAAA0002, (B slot 1) 0xAAAA0003.
    {
        nfsu::RenderTextureSet textures;
        textures.textures.push_back(make_texture(0xAAAA0002U, 2));
        textures.textures.push_back(make_texture(0xAAAA0003U, 2));
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = nfsu::prepare_track_region(
            report, textures, nfsu::TrackSceneLimits{}, prepared, error);
        check(ok, "the leading-fallback region prepares: " + error);
        check(prepared.cost.unresolved_draws == 1 &&
                  prepared.cost.resolved_draws == 3,
              "one leading draw falls back");
        check(prepared.draws.size() == 4 &&
                  prepared.draws[0].binding == nfsu::BindingKind::unresolved &&
                  prepared.draws[0].drawable,
              "the first draw is an unresolved but drawable group");
        check(prepared.cost.texture_binds == 3,
              "the fallback, then two textures: 3 binds, got " +
                  std::to_string(prepared.cost.texture_binds));
        const bool valid = nfsu::validate_prepared_track_region(prepared, error);
        check(valid, "the leading-fallback region validates: " + error);
    }
    {
        // Nothing resolves: every group shares the one fallback binding,
        // and the three distinct hashes are listed once each, ascending.
        const nfsu::RenderTextureSet textures;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = nfsu::prepare_track_region(
            report, textures, nfsu::TrackSceneLimits{}, prepared, error);
        check(ok, "the all-fallback region prepares: " + error);
        check(prepared.cost.unresolved_draws == 4 &&
                  prepared.cost.resolved_draws == 0,
              "every draw falls back");
        check(prepared.textures.empty() && prepared.cost.texture_bytes == 0,
              "no texture is copied");
        check(prepared.cost.texture_binds == 1,
              "four fallback draws share one binding, got " +
                  std::to_string(prepared.cost.texture_binds));
        check(prepared.unresolved_hashes.size() == 3 &&
                  prepared.unresolved_hashes[0] == 0xAAAA0001U &&
                  prepared.unresolved_hashes[1] == 0xAAAA0002U &&
                  prepared.unresolved_hashes[2] == 0xAAAA0003U,
              "the unresolved hashes are deduplicated and ascending");
        const bool valid = nfsu::validate_prepared_track_region(prepared, error);
        check(valid, "the all-fallback region validates: " + error);
    }
}

// Writes the fields of a MeshMaterial into its raw record, so that a
// hand-built report satisfies the raw-record cross-check.
void fill_material_raw(nfsu::MeshMaterial& material) {
    Bytes record;
    for (std::size_t i = 0; i < 3; ++i) {
        append_f32(record, material.bounds_min[i]);
    }
    append_u32(record, material.index_count);
    for (std::size_t i = 0; i < 3; ++i) {
        append_f32(record, material.bounds_max[i]);
    }
    append_u32(record, material.texture_slot);
    append_u32(record, material.material_key_slot);
    record.resize(52, 0);
    append_u32(record, material.first_index);
    append_u32(record, material.flags);
    std::memcpy(material.raw, record.data(), nfsu::kMaterialRecordSize);
}

// parse_track_region never produces a material whose texture slot is
// outside its mesh's list, because parse_materials refuses that mesh. The
// prepared layer is a public API that takes a report, so it still has to
// handle one; this is the only way to reach BindingKind::invalid_slot.
void test_invalid_texture_slot_binding() {
    nfsu::TrackRegionReport report;
    report.region.offset = 0;
    report.region.size = 256;
    nfsu::TrackMesh mesh;
    mesh.index = 0;
    mesh.name = "HAND_BUILT";
    mesh.valid = true;
    mesh.declared_vertex_count = 3;
    mesh.declared_face_count = 1;
    mesh.declared_material_count = 1;
    mesh.vertices.resize(3);
    mesh.triangles.push_back(nfsu::MeshTriangle{0, 1, 2});
    // No texture hashes at all, so slot 0 is outside the list.
    nfsu::MeshMaterial material;
    material.index_count = 3;
    material.first_index = 0;
    material.texture_slot = 0;
    material.material_key_slot = 0xFFU;
    fill_material_raw(material);
    mesh.materials.push_back(material);
    report.meshes.push_back(mesh);

    const nfsu::RenderTextureSet textures;
    nfsu::PreparedTrackRegion prepared;
    std::string error;
    const bool ok = nfsu::prepare_track_region(
        report, textures, nfsu::TrackSceneLimits{}, prepared, error);
    check(ok, "a report with an invalid texture slot prepares: " + error);
    check(prepared.draws.size() == 1 &&
              prepared.draws[0].binding == nfsu::BindingKind::invalid_slot,
          "the group binds as an invalid slot");
    check(prepared.cost.invalid_slot_draws == 1 &&
              prepared.cost.drawable_draws == 0 &&
              prepared.cost.drawn_triangles == 0,
          "an invalid slot is never drawn");
    check(prepared.draws.size() == 1 && prepared.draws[0].texture_hash == 0 &&
              prepared.draws[0].texture == nfsu::kNoHandle,
          "an invalid slot names no hash and no texture");
    check(prepared.unresolved_hashes.empty(),
          "an invalid slot is not an unresolved hash");
    const bool valid = nfsu::validate_prepared_track_region(prepared, error);
    check(valid, "the invalid-slot region validates: " + error);

    nfsu::PreparedTrackRegion bad = prepared;
    bad.draws[0].texture_hash = 1;
    check(!nfsu::validate_prepared_track_region(bad, error),
          "an invalid slot with a hash is caught");
}

// shadowed records, blocked sources and not-wanted records are decode
// bookkeeping; they must not change what the region prepares.
void test_decode_bookkeeping_does_not_change_the_region() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);
    nfsu::RenderTextureSet plain;
    plain.textures.push_back(make_texture(0xAAAA0001U, 2));
    plain.textures.push_back(make_texture(0xAAAA0002U, 2));
    plain.textures.push_back(make_texture(0xAAAA0003U, 2));
    nfsu::RenderTextureSet noisy = plain;
    noisy.shadowed.push_back(nfsu::ShadowedTexture{0xAAAA0001U, 1, 0, 4, 0});
    noisy.blocked_sources.push_back(2);
    noisy.not_wanted = 17;

    nfsu::PreparedTrackRegion first;
    nfsu::PreparedTrackRegion second;
    std::string error;
    bool ok = nfsu::prepare_track_region(report, plain,
                                         nfsu::TrackSceneLimits{}, first, error);
    check(ok, "the plain set prepares: " + error);
    ok = nfsu::prepare_track_region(report, noisy, nfsu::TrackSceneLimits{},
                                    second, error);
    check(ok, "the noisy set prepares: " + error);
    check(first.draws.size() == second.draws.size() &&
              first.textures.size() == second.textures.size() &&
              first.cost.resolved_draws == second.cost.resolved_draws &&
              first.cost.texture_binds == second.cost.texture_binds &&
              first.cost.texture_bytes == second.cost.texture_bytes &&
              first.unresolved_hashes == second.unresolved_hashes,
          "decode bookkeeping does not change the prepared region");
}

void test_prepared_region_limits() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);
    nfsu::RenderTextureSet textures;
    textures.textures.push_back(make_texture(0xAAAA0001U, 4));
    textures.textures.push_back(make_texture(0xAAAA0002U, 4));
    textures.textures.push_back(make_texture(0xAAAA0003U, 4));

    struct Case {
        const char* what;
        nfsu::TrackSceneLimits limits;
        const char* text;
    };
    std::vector<Case> cases;
    {
        nfsu::TrackSceneLimits limits;
        limits.max_draws = 3;
        cases.push_back({"draw limit", limits, "material groups"});
    }
    {
        nfsu::TrackSceneLimits limits;
        limits.max_vertices = 6;
        cases.push_back({"vertex limit", limits, "vertices"});
    }
    {
        nfsu::TrackSceneLimits limits;
        limits.max_triangles = 3;
        cases.push_back({"triangle limit", limits, "triangles"});
    }
    {
        nfsu::TrackSceneLimits limits;
        limits.max_draw_triangles = 3;
        cases.push_back({"drawn triangle limit", limits, "draw more than"});
    }
    {
        nfsu::TrackSceneLimits limits;
        limits.max_texture_bytes = 64;
        cases.push_back({"texture byte limit", limits, "textures need more than"});
    }
    for (const Case& item : cases) {
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = nfsu::prepare_track_region(report, textures, item.limits,
                                                   prepared, error);
        const std::string tag = std::string(item.what) + ": ";
        check(!ok, tag + "the limit refuses the region");
        check(error.find(item.text) != std::string::npos,
              tag + "the error names the limit (got: " + error + ")");
        check(prepared.meshes.empty() && prepared.draws.empty() &&
                  prepared.textures.empty(),
              tag + "nothing is left behind on failure");
    }
}

void test_validate_prepared_region_rejects_tampering() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);
    nfsu::RenderTextureSet textures;
    textures.textures.push_back(make_texture(0xAAAA0001U, 2));
    textures.textures.push_back(make_texture(0xAAAA0002U, 2));
    textures.textures.push_back(make_texture(0xAAAA0003U, 2));

    nfsu::PreparedTrackRegion good;
    std::string error;
    const bool prepared_ok = nfsu::prepare_track_region(
        report, textures, nfsu::TrackSceneLimits{}, good, error);
    check(prepared_ok, "the tamper fixture prepares: " + error);

    {
        nfsu::PreparedTrackRegion bad = good;
        bad.meshes[0].triangles[0].a =
            static_cast<std::uint16_t>(bad.meshes[0].vertices.size());
        check(!nfsu::validate_prepared_track_region(bad, error),
              "an out-of-range triangle index is caught");
        check(error.find("references a vertex outside") != std::string::npos,
              "the index error names the range");
    }
    {
        // Draw 1 is (first_index 3, count 6) in a 3-triangle mesh, so 9
        // indices exist; +9 puts the end past the stream.
        nfsu::PreparedTrackRegion bad = good;
        bad.draws[1].index_count += 9;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "an index range past the end of the mesh is caught");
        check(error.find("disagrees with its own index range") != std::string::npos,
              "the index-range error says so, got: " + error);
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.draws[1].texture = 2;
        bad.draws[3].texture = 1;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a texture used before an earlier one is caught");
        check(error.find("was first used") != std::string::npos,
              "the first-use error says so, got: " + error);
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.draws[0].texture_source ^= 1U;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "stale texture provenance on a resolved draw is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.textures[0].valid = false;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "an unusable prepared texture is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.meshes[0].triangles.clear();
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a prepared mesh with no triangles is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.unresolved_hashes.push_back(0xFFFFFFFFU);
        bad.cost.unresolved_hashes = bad.unresolved_hashes.size();
        check(!nfsu::validate_prepared_track_region(bad, error),
              "an unresolved hash no draw names is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.draws[0].raw[12] ^= 0xFFU;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a raw material record that disagrees with the draw is caught");
        check(error.find("raw material record") != std::string::npos,
              "the raw-record error says so, got: " + error);
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.draws[0].texture_slot ^= 1U;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a texture slot the raw record does not carry is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.skipped_invalid_meshes.push_back(bad.meshes[0].source_index);
        ++bad.source_mesh_count;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a mesh that is both prepared and skipped is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.skipped_empty_meshes.push_back(1000000);
        ++bad.source_mesh_count;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a skipped position outside the source is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.draws[0].texture = bad.textures.size();
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a texture handle past the end is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.draws[0].texture_hash ^= 1U;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a draw bound to a texture that does not claim its hash is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.cost.drawn_triangles += 1;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "cost statistics that do not match the draw list are caught");
        check(error.find("cost statistics") != std::string::npos,
              "the cost error says so");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.source_mesh_count += 1;
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a source mesh count that does not add up is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        std::swap(bad.meshes[0], bad.meshes[1]);
        check(!nfsu::validate_prepared_track_region(bad, error),
              "meshes out of source order are caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.textures.push_back(make_texture(0xDEADBEEFU, 2));
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a texture no draw uses is caught");
    }
    {
        nfsu::PreparedTrackRegion bad = good;
        bad.draws.pop_back();
        check(!nfsu::validate_prepared_track_region(bad, error),
              "a missing draw is caught");
    }
    const bool still_valid = nfsu::validate_prepared_track_region(good, error);
    check(still_valid, "the untouched region still validates: " + error);
}

// ---------------------------------------------------------------------------
// Texture sources for a region.
//
// These fixtures build a texture pack as bytes and let the real reader parse
// it, so a malformation travels the whole path a retail file travels:
// parse_texture_pack_set, then decode_render_texture_sources, then
// prepare_track_region. A hand-built TexturePack would skip the reader and
// prove nothing about a malformed file.
//
// Only the raw storage layout is emitted, because every retail track texture
// pack lacks 0x33310003 (docs/TRACK.md, "Observed"). Every texture is
// A8R8G8B8 with one level and all four bytes of every texel equal, so one
// decoded byte names the pack the pixels came from.

unsigned char log2_of(std::uint32_t value) {
    unsigned char result = 0;
    while ((1U << result) < value) {
        ++result;
    }
    return result;
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

// Written into an override field to mean "write the value the record should
// hold". No correct field value in these fixtures is ever this large.
constexpr std::uint32_t kAuto = 0xFFFFFFFFU;

struct PackTexture {
    std::uint32_t hash = 0;
    // The hash table word. kAuto writes hash, which is what a sound record
    // holds. A raw record whose two disagree is refused (src/texture.cpp
    // check in parse_texture), but both fields are read before that check,
    // so the refused record still claims two hashes.
    std::uint32_t table_hash = kAuto;
    std::uint16_t width = 2;
    std::uint16_t height = 2;
    unsigned char fill = 0;
    // Overrides for the three info fields a malformed record gets wrong. A
    // record with an overridden virtual offset is never filled with pixels:
    // every use of that override puts the chain outside the payload or off
    // its alignment, so the record is refused before anything reads it.
    std::uint32_t virtual_offset = kAuto;  // info +48
    std::uint32_t data_size = kAuto;       // info +56
    std::uint32_t top_level_size = kAuto;  // info +64
};

PackTexture pack_texture(std::uint32_t hash, unsigned char fill) {
    PackTexture texture;
    texture.hash = hash;
    texture.fill = fill;
    return texture;
}

struct PackSpec {
    std::vector<PackTexture> textures;
    // Writes one hash-table entry fewer than there are info records, so the
    // tables disagree on the record count and parse_tables gives up before
    // it sets tables_parsed.
    bool short_hash_table = false;
    // Ends the 0x33320002 payload one byte before the last declared chain
    // ends, so that chain runs past the data the file holds.
    bool truncate_last_chain = false;
    // Writes an unrelated leaf instead of a pack root.
    bool no_root = false;
};

void write_raw_texture_pack(Writer& w, const PackSpec& spec) {
    w.begin(nfsu::kTexturePackRootId);
    w.begin(nfsu::kTextureInfoContainerId);

    const std::string path = "Location1\\Test.tpk";
    Bytes header(nfsu::kTexturePackHeaderSize, 0);
    put_u32(header, 0, 4);
    std::memcpy(&header[4], "TRACK", 5);
    std::memcpy(&header[32], path.data(), path.size());
    put_u32(header, 96, nfsu::bin_hash(path));
    w.leaf(nfsu::kTexturePackHeaderId, header);

    const std::size_t listed = spec.short_hash_table && !spec.textures.empty()
                                   ? spec.textures.size() - 1
                                   : spec.textures.size();
    Bytes hash_table;
    for (std::size_t i = 0; i < listed; ++i) {
        const PackTexture& texture = spec.textures[i];
        append_u32(hash_table, texture.table_hash == kAuto ? texture.hash
                                                           : texture.table_hash);
        append_u32(hash_table, 0);
    }
    w.leaf(nfsu::kTextureHashTableId, hash_table);

    // Chain offsets inside the data payload, 128-byte aligned as the format
    // requires. The payload is sized to hold every chain an un-overridden
    // record claims.
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> sizes;
    std::uint32_t next = 0;
    std::uint32_t last_end = 0;
    for (const PackTexture& texture : spec.textures) {
        const std::uint32_t bytes =
            static_cast<std::uint32_t>(texture.width) * texture.height * 4U;
        const std::uint32_t at =
            texture.virtual_offset == kAuto ? next : texture.virtual_offset;
        offsets.push_back(at);
        sizes.push_back(bytes);
        if (texture.virtual_offset == kAuto) {
            last_end = at + bytes;
        }
        next = (next + bytes + 127U) & ~127U;
    }

    Bytes info_table;
    for (std::size_t i = 0; i < spec.textures.size(); ++i) {
        const PackTexture& texture = spec.textures[i];
        Bytes record(nfsu::kTextureInfoRecordSize, 0);
        const std::string name = "T" + std::to_string(i);
        std::memcpy(&record[nfsu::kTextureNameOffset], name.data(), name.size());
        put_u32(record, 36, texture.hash);
        put_u32(record, 48, offsets[i]);
        put_u32(record, 56,
                texture.data_size == kAuto ? sizes[i] : texture.data_size);
        put_u32(record, 64,
                texture.top_level_size == kAuto ? sizes[i]
                                                : texture.top_level_size);
        put_u16(record, 68, texture.width);
        put_u16(record, 70, texture.height);
        record[72] = log2_of(texture.width);
        record[73] = log2_of(texture.height);
        record[74] = 0x20;  // A8R8G8B8
        record[78] = 1;     // one level
        info_table.insert(info_table.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureInfoTableId, info_table);

    Bytes format_table;
    for (std::size_t i = 0; i < spec.textures.size(); ++i) {
        Bytes record(nfsu::kTextureFormatRecordSize, 0);
        put_u32(record, 20, 21);  // D3DFMT_A8R8G8B8
        format_table.insert(format_table.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureFormatTableId, format_table);
    w.end();  // info container

    w.begin(nfsu::kTextureDataContainerId);
    Bytes data_header(24, 0);
    put_u32(data_header, 8, 1);
    put_u32(data_header, 12, nfsu::bin_hash(path));
    w.leaf(nfsu::kTextureDataHeaderId, data_header);
    w.begin(nfsu::kTextureDataId);
    while (w.at() % nfsu::kTextureDataAlignment != 0) {
        w.bytes().push_back(0x11);
    }
    const std::size_t start = w.size();
    std::uint32_t extent = next;
    if (spec.truncate_last_chain && last_end != 0) {
        extent = last_end - 1U;
    }
    w.append(Bytes(extent, 0));
    for (std::size_t i = 0; i < spec.textures.size(); ++i) {
        if (spec.textures[i].virtual_offset != kAuto ||
            offsets[i] + sizes[i] > extent) {
            continue;
        }
        std::fill_n(
            w.bytes().begin() + static_cast<std::ptrdiff_t>(start + offsets[i]),
            sizes[i], spec.textures[i].fill);
    }
    w.end();  // data chunk
    w.end();  // data container
    w.end();  // root
}

// One texture file and the pack set parsed from it. Build it once and leave
// it alone: decode_render_texture_sources pairs every pack with the exact
// buffer address and size it was parsed from, so copying or moving a built
// file, or touching its bytes, breaks that pairing.
struct TextureFile {
    Bytes bytes;
    nfsu::TexturePackSet set;

    void build(const std::vector<PackSpec>& packs) {
        Writer w(0);
        for (const PackSpec& spec : packs) {
            if (spec.no_root) {
                w.leaf(0x00135200U, Bytes(32, 0x5A));
                continue;
            }
            write_raw_texture_pack(w, spec);
        }
        bytes = w.bytes();
        set = nfsu::parse_texture_pack_set(bytes.data(), bytes.size());
    }

    nfsu::RenderTextureSource source(
        const std::vector<std::uint32_t>* wanted) const {
        nfsu::RenderTextureSource view;
        view.bytes = &bytes;
        for (const nfsu::TexturePack& pack : set.packs) {
            view.packs.push_back(&pack);
        }
        view.complete = set.complete;
        view.wanted = wanted;
        return view;
    }
};

bool decode_and_prepare(
    const nfsu::TrackRegionReport& report,
    const std::vector<nfsu::RenderTextureSource>& sources,
    nfsu::RenderTextureSet& set,
    nfsu::PreparedTrackRegion& prepared,
    std::string& error) {
    return nfsu::decode_render_texture_sources(
               sources, nfsu::RenderTextureLimits{}, set, error) &&
           nfsu::prepare_track_region(report, set, nfsu::TrackSceneLimits{},
                                      prepared, error) &&
           nfsu::validate_prepared_track_region(prepared, error);
}

// The first decoded byte of the texture a draw is bound to, or -1 when the
// draw has none. Every texel of a fixture texture holds the same byte, so
// this names the pack the pixels came from.
int drawn_fill(const nfsu::PreparedTrackRegion& prepared, std::size_t draw) {
    if (draw >= prepared.draws.size() ||
        prepared.draws[draw].texture == nfsu::kNoHandle ||
        prepared.draws[draw].texture >= prepared.textures.size()) {
        return -1;
    }
    const nfsu::RenderTexture& texture =
        prepared.textures[prepared.draws[draw].texture];
    return texture.rgba.empty() ? -1 : static_cast<int>(texture.rgba[0]);
}

// Two sources that both carry a hash the region names. Nothing in the track
// path compares content: the earliest source in the caller's order wins, the
// later record is dropped and listed as shadowed, and reversing the order
// changes the pixels the prepared region carries. Draw order for the fixture
// is (A slot 0) 0xAAAA0001, (A slot 1) 0xAAAA0002, (B slot 0) 0xAAAA0002,
// (B slot 1) 0xAAAA0003.
void test_track_texture_source_collision() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);
    const std::vector<std::uint32_t> wanted =
        nfsu::track_region_texture_hashes(report);
    check(wanted.size() == 3 && wanted[0] == 0xAAAA0001U &&
              wanted[1] == 0xAAAA0002U && wanted[2] == 0xAAAA0003U,
          "the fixture region names three hashes, ascending");

    PackSpec first;
    first.textures = {pack_texture(0xAAAA0001U, 0x11),
                      pack_texture(0xAAAA0002U, 0x22)};
    PackSpec second;
    second.textures = {pack_texture(0xAAAA0002U, 0x33),
                       pack_texture(0xAAAA0003U, 0x44)};

    TextureFile a;
    a.build({first});
    TextureFile b;
    b.build({second});
    check(a.set.ok() && a.set.complete && a.set.packs.size() == 1 &&
              a.set.packs[0].valid_texture_count() == 2 && b.set.ok() &&
              b.set.complete && b.set.packs[0].valid_texture_count() == 2,
          "both collision fixtures parse cleanly");

    {
        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report, {a.source(&wanted), b.source(&wanted)}, set, prepared, error);
        check(ok, "the two-source region prepares: " + error);
        // Both sources hold one pack, so pack 0 is the only value this
        // fixture can produce and this line alone distinguishes nothing.
        // It is the zero half of a pair; the one half is the second-pack
        // block below, which is what a decoder that stopped writing the
        // field would fail.
        check(set.shadowed.size() == 1 && set.shadowed[0].hash == 0xAAAA0002U &&
                  set.shadowed[0].source == 1 && set.shadowed[0].pack == 0 &&
                  set.shadowed[0].record == 0 && set.shadowed[0].by_source == 0,
              "the later record is shadowed, with its provenance");
        check(set.unusable_claims.empty() && set.blocked_sources.empty(),
              "a fully shadowed record adds no unusable claim");
        check(prepared.cost.resolved_draws == 4 &&
                  prepared.cost.unusable_draws == 0 &&
                  prepared.unresolved_hashes.empty(),
              "every draw resolves from one of the two sources");
        check(prepared.draws.size() == 4 &&
                  prepared.draws[1].texture_source == 0 &&
                  prepared.draws[2].texture_source == 0 &&
                  prepared.draws[3].texture_source == 1,
              "the contested hash binds to source 0, the other to source 1");
        check(drawn_fill(prepared, 1) == 0x22 && drawn_fill(prepared, 2) == 0x22,
              "the contested draws carry source 0's pixels, got " +
                  std::to_string(drawn_fill(prepared, 1)));
        check(drawn_fill(prepared, 0) == 0x11 && drawn_fill(prepared, 3) == 0x44,
              "the uncontested draws carry their own source's pixels");
        check(set.textures.size() == 3 && prepared.textures.size() == 3,
              "the shadowed record is not decoded and not copied");
        for (const nfsu::RenderTexture& texture : set.textures) {
            check(!(texture.source == 1 && texture.hash == 0xAAAA0002U),
                  "no decoded texture is the shadowed record");
        }
        // A8: texture_pack and texture_record are not always 0 here, so a
        // decoder that never wrote them would fail this.
        check(prepared.draws.size() == 4 && prepared.draws[3].texture_pack == 0 &&
                  prepared.draws[3].texture_record == 1,
              "a resolved draw carries the record position inside its pack");
    }
    {
        // The same two files in the other order. Only the order changed, so
        // any difference below is the precedence rule and nothing else.
        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report, {b.source(&wanted), a.source(&wanted)}, set, prepared, error);
        check(ok, "the reversed region prepares: " + error);
        // pack 0 here is the zero half of the same pair; record 1 is not,
        // because the block above pins the same field at 0.
        check(set.shadowed.size() == 1 && set.shadowed[0].hash == 0xAAAA0002U &&
                  set.shadowed[0].source == 1 && set.shadowed[0].pack == 0 &&
                  set.shadowed[0].record == 1 && set.shadowed[0].by_source == 0,
              "reversing the order shadows the other record");
        check(prepared.cost.resolved_draws == 4,
              "every draw still resolves after the reversal");
        check(drawn_fill(prepared, 1) == 0x33 && drawn_fill(prepared, 2) == 0x33,
              "caller order decides the contested pixels, got " +
                  std::to_string(drawn_fill(prepared, 1)));
        check(prepared.draws.size() == 4 &&
                  prepared.draws[1].texture_source == 0 &&
                  prepared.draws[0].texture_source == 1,
              "provenance follows the reversed order");
    }
    {
        // Two packs of one file claim the hash. Records inside one source
        // never shadow each other, so both are kept and the hash becomes
        // ambiguous: the draws stay drawable on the fallback, and an
        // ambiguous hash is not an unresolved one.
        TextureFile both;
        both.build({first, second});
        check(both.set.packs.size() == 2 && both.set.complete,
              "a file with two packs is still complete");
        check(!both.set.ok() && both.set.issues.size() == 1 &&
                  both.set.issues[0].message.find("repeats hash 0xAAAA0002") !=
                      std::string::npos,
              "the reader reports the ambiguity as a set issue");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(report, {both.source(&wanted)}, set,
                                           prepared, error);
        check(ok, "the ambiguous-source region prepares: " + error);
        check(set.shadowed.empty() && set.textures.size() == 4,
              "records inside one source do not shadow each other");
        check(prepared.cost.unusable_draws == 2 &&
                  prepared.cost.resolved_draws == 2,
              "both draws that name the ambiguous hash are unusable");
        check(prepared.draws.size() == 4 &&
                  prepared.draws[1].binding == nfsu::BindingKind::unusable &&
                  prepared.draws[1].texture == nfsu::kNoHandle &&
                  prepared.draws[1].drawable,
              "an unusable draw is drawable and names no texture");
        check(prepared.unresolved_hashes.empty(),
              "an ambiguous hash is not an unresolved hash");
        check(drawn_fill(prepared, 1) == -1 && drawn_fill(prepared, 0) == 0x11,
              "the ambiguity is contained to the hash it belongs to");
        check(prepared.draws.size() == 4 && prepared.draws[3].texture_pack == 1 &&
                  prepared.draws[3].texture_record == 1 &&
                  drawn_fill(prepared, 3) == 0x44,
              "a draw resolved from the second pack of a source says so");
    }
    {
        // A later source cannot repair an ambiguity an earlier source
        // created: its record is shadowed by the earlier claims.
        TextureFile both;
        both.build({first, second});
        TextureFile repair;
        repair.build({second});
        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report, {both.source(&wanted), repair.source(&wanted)}, set, prepared,
            error);
        check(ok, "the repair-attempt region prepares: " + error);
        check(set.blocked_sources.empty() && set.shadowed.size() == 2 &&
                  set.shadowed[0].source == 1 && set.shadowed[1].source == 1,
              "source 1 was read and both its records were shadowed");
        check(prepared.cost.unusable_draws == 2,
              "a later source does not resolve an earlier ambiguity");
    }
    {
        // The pack half of shadowed-record provenance. Every other shadowed
        // record in this file sits in the first pack of its source, so the
        // "pack 0" those blocks assert is the only value their fixtures can
        // produce: a decoder that never wrote the field would satisfy them
        // all. This block is the other half of the pair, the same shape the
        // resolved-draw assertions above already have with texture_pack 0
        // and texture_pack 1 at two sites. The shadowed record and the
        // unusable claim it leaves behind both come out of the second pack
        // of a two-pack source, at a position inside that pack that is not
        // 0 either.
        //
        // The fixture states the layout in bytes - build() writes the packs
        // in the order it is given - and the parse is read back below before
        // any decode assertion leans on it. Nothing hands the decoder the
        // numbers it is then held to; they are this test's own.
        PackSpec front;
        front.textures = {pack_texture(0xAAAA0001U, 0x11)};
        PackSpec back_first;
        back_first.textures = {pack_texture(0xAAAA0003U, 0x44)};
        PackSpec back_second;
        // Record 0 of the second pack is sound. Record 1 disagrees with its
        // own hash-table word, so it is refused and still claims two
        // hashes: 0x0BADF00D, which nothing else claims, and 0xAAAA0001,
        // which the earlier source already holds. Shadowed on the second,
        // it leaves the first as an unusable claim, so one record at a
        // known position pins the provenance of both records.
        back_second.textures = {pack_texture(0xAAAA0002U, 0x33),
                                pack_texture(0x0BADF00DU, 0x55)};
        back_second.textures[1].table_hash = 0xAAAA0001U;

        TextureFile front_file;
        front_file.build({front});
        TextureFile back_file;
        back_file.build({back_first, back_second});
        check(back_file.set.packs.size() == 2 &&
                  back_file.set.packs[0].textures.size() == 1 &&
                  back_file.set.packs[0].textures[0].hash == 0xAAAA0003U &&
                  back_file.set.packs[1].textures.size() == 2 &&
                  back_file.set.packs[1].textures[0].hash == 0xAAAA0002U &&
                  back_file.set.packs[1].textures[1].hash == 0x0BADF00DU &&
                  back_file.set.packs[1].textures[1].table_hash == 0xAAAA0001U,
              "the second file parses as two packs, the refused record last "
              "in the second");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report, {front_file.source(&wanted), back_file.source(&wanted)}, set,
            prepared, error);
        check(ok, "the second-pack shadow region prepares: " + error);
        check(set.shadowed.size() == 1 && set.shadowed[0].hash == 0xAAAA0001U &&
                  set.shadowed[0].source == 1 && set.shadowed[0].pack == 1 &&
                  set.shadowed[0].record == 1 && set.shadowed[0].by_source == 0,
              "a shadowed record names the pack it came from, got pack " +
                  std::to_string(set.shadowed.empty() ? 0
                                                      : set.shadowed[0].pack));
        check(set.unusable_claims.size() == 1 &&
                  set.unusable_claims[0].hash == 0x0BADF00DU &&
                  set.unusable_claims[0].source == 1 &&
                  set.unusable_claims[0].pack == 1 &&
                  set.unusable_claims[0].record == 1,
              "its unusable claim names the same pack and position, got pack " +
                  std::to_string(set.unusable_claims.empty()
                                     ? 0
                                     : set.unusable_claims[0].pack));
        // The first pack of that same source supplied a texture, so the
        // index above is a position inside the source and not the source
        // index, a constant, or a count of packs seen.
        bool saw_first_pack = false;
        for (const nfsu::RenderTexture& texture : set.textures) {
            if (texture.hash == 0xAAAA0003U) {
                saw_first_pack = true;
                check(texture.source == 1 && texture.pack == 0 &&
                          texture.record == 0,
                      "the first pack of the second source is still pack 0");
            }
        }
        check(saw_first_pack && set.textures.size() == 3,
              "the sound records of both packs are decoded");
        check(prepared.cost.resolved_draws == 4 &&
                  prepared.cost.unusable_draws == 0 &&
                  prepared.unresolved_hashes.empty(),
              "the refused record costs the region none of its draws");
        check(drawn_fill(prepared, 0) == 0x11 &&
                  drawn_fill(prepared, 1) == 0x33 &&
                  drawn_fill(prepared, 3) == 0x44,
              "each draw carries the pixels of the pack that kept its hash");
    }
    {
        // The wanted filter. A track source is filtered by the region's
        // hashes, the first one included, which is the difference from the
        // car viewer that docs/TRACK.md states first. A record claiming a
        // hash the region does not name is never decoded, and it still
        // claims that hash against later sources.
        PackSpec extra;
        extra.textures = {pack_texture(0xAAAA0001U, 0x11),
                          pack_texture(0xAAAA0002U, 0x22),
                          pack_texture(0xAAAA0003U, 0x44),
                          pack_texture(0x0BADF00DU, 0x55)};
        TextureFile file;
        file.build({extra});
        check(file.set.complete && file.set.packs[0].valid_texture_count() == 4,
              "the wanted-filter fixture parses with four valid records");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(report, {file.source(&wanted)}, set,
                                           prepared, error);
        check(ok, "the wanted-filter region prepares: " + error);
        check(set.not_wanted == 1 && set.textures.size() == 3,
              "the unwanted record is not decoded, got not_wanted " +
                  std::to_string(set.not_wanted) + " textures " +
                  std::to_string(set.textures.size()));
        check(set.decoded_bytes == 3U * 2U * 2U * 4U,
              "only the wanted records cost any bytes");
        for (const nfsu::RenderTexture& texture : set.textures) {
            check(texture.hash != 0x0BADF00DU,
                  "the unwanted hash is not in the set");
        }

        // With no wanted list the same file decodes all four, so the
        // difference above is the filter and not the fixture.
        nfsu::RenderTextureSource unfiltered = file.source(nullptr);
        nfsu::RenderTextureSet all;
        check(nfsu::decode_render_texture_sources(
                  {unfiltered}, nfsu::RenderTextureLimits{}, all, error) &&
                  all.textures.size() == 4 && all.not_wanted == 0,
              "without a wanted list every record is decoded");
    }
    {
        // An unwanted claim still shadows a later source. The record is
        // never decoded, and the hash it holds is still taken.
        PackSpec early;
        early.textures = {pack_texture(0x0BADF00DU, 0x55)};
        PackSpec late;
        late.textures = {pack_texture(0x0BADF00DU, 0x66),
                         pack_texture(0xAAAA0001U, 0x11)};
        TextureFile first_file;
        first_file.build({early});
        TextureFile second_file;
        second_file.build({late});
        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report, {first_file.source(&wanted), second_file.source(&wanted)}, set,
            prepared, error);
        check(ok, "the unwanted-claim region prepares: " + error);
        check(set.not_wanted == 1 && set.textures.size() == 1 &&
                  set.shadowed.size() == 1 && set.shadowed[0].source == 1 &&
                  set.shadowed[0].hash == 0x0BADF00DU,
              "a record the filter skipped still claims its hash");
    }
    {
        // A raw record whose info +36 and hash-table word disagree is
        // refused, but both fields are read first, so it claims two hashes.
        // Shadowed on one of them, its other claim becomes an unusable
        // claim: the third cause of an unusable binding that
        // docs/TRACK.md lists, and the only one a single pack cannot
        // otherwise produce.
        PackSpec owner;
        owner.textures = {pack_texture(0xAAAA0001U, 0x11)};
        PackSpec liar;
        liar.textures = {pack_texture(0xAAAA0002U, 0x22)};
        liar.textures[0].table_hash = 0xAAAA0001U;
        PackSpec rescuer;
        rescuer.textures = {pack_texture(0xAAAA0002U, 0x33),
                            pack_texture(0xAAAA0001U, 0x77)};

        TextureFile a_file;
        a_file.build({owner});
        TextureFile b_file;
        b_file.build({liar});
        TextureFile c_file;
        c_file.build({rescuer});
        check(!b_file.set.packs.empty() &&
                  b_file.set.packs[0].valid_texture_count() == 0 &&
                  b_file.set.packs[0].textures.size() == 1 &&
                  b_file.set.packs[0].textures[0].hash == 0xAAAA0002U &&
                  b_file.set.packs[0].textures[0].table_hash == 0xAAAA0001U,
              "the mismatched record is refused and still names both hashes");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report,
            {a_file.source(&wanted), b_file.source(&wanted), c_file.source(&wanted)},
            set, prepared, error);
        check(ok, "the unusable-claim region prepares: " + error);
        // Single-pack sources again, and the claiming record is the first
        // of its pack, so pack 0 and record 0 are the zero half of a pair.
        // The one half of both is the second-pack block above.
        check(set.unusable_claims.size() == 1 &&
                  set.unusable_claims[0].hash == 0xAAAA0002U &&
                  set.unusable_claims[0].source == 1 &&
                  set.unusable_claims[0].pack == 0 &&
                  set.unusable_claims[0].record == 0,
              "the second claim of a shadowed record is recorded, got " +
                  std::to_string(set.unusable_claims.size()) + " claims");
        check(prepared.cost.unusable_draws == 2 &&
                  prepared.cost.resolved_draws == 1,
              "the claim makes its hash unusable in the source behind it");
        check(prepared.draws.size() == 4 &&
                  prepared.draws[1].texture == nfsu::kNoHandle &&
                  drawn_fill(prepared, 1) == -1,
              "source 2's pixels are not substituted for the claimed hash");
        check(prepared.unresolved_hashes.size() == 1 &&
                  prepared.unresolved_hashes[0] == 0xAAAA0003U,
              "only the hash nothing claims is unresolved");
        // 0xAAAA0001 is claimed by source 0 and again by source 1, then
        // repeated by source 2. by_source must name the earliest claimant.
        check(set.shadowed.size() == 3 && set.shadowed[0].source == 1 &&
                  set.shadowed[0].hash == 0xAAAA0001U &&
                  set.shadowed[0].by_source == 0 &&
                  set.shadowed[1].source == 2 &&
                  set.shadowed[1].hash == 0xAAAA0002U &&
                  set.shadowed[1].by_source == 1 &&
                  set.shadowed[2].source == 2 &&
                  set.shadowed[2].hash == 0xAAAA0001U &&
                  set.shadowed[2].by_source == 0,
              "by_source names the earliest source that claimed the hash");
        check(set.textures.size() == 1 && set.textures[0].source == 0 &&
                  drawn_fill(prepared, 0) == 0x11,
              "only the earliest claimant supplies pixels");
    }
    {
        // A record that could not be decoded still competes. One pack with
        // a sound record and a malformed one on the same hash is ambiguous:
        // the sound copy is not taken. This is the "matches" half of the
        // resolve rule; the duplicate-hash case above only exercises the
        // "usable" half, because there both copies are sound.
        PackSpec mixed;
        mixed.textures = {pack_texture(0xAAAA0001U, 0x11),
                          pack_texture(0xAAAA0002U, 0x22),
                          pack_texture(0xAAAA0002U, 0x33),
                          pack_texture(0xAAAA0003U, 0x44)};
        mixed.textures[2].data_size = 20;  // 2x2 A8R8G8B8 needs 16
        TextureFile file;
        file.build({mixed});
        check(file.set.packs[0].valid_texture_count() == 3 &&
                  file.set.packs[0].issues.size() == 2,
              "one record is refused and the repeated hash is reported");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(report, {file.source(&wanted)}, set,
                                           prepared, error);
        check(ok, "the mixed-validity region prepares: " + error);
        check(set.textures.size() == 4,
              "the refused record is kept in the set and still claims");
        check(prepared.cost.unusable_draws == 2 &&
                  prepared.cost.resolved_draws == 2,
              "a sound copy is not taken when a refused copy claims the hash");
        check(drawn_fill(prepared, 1) == -1,
              "the surviving copy's pixels are not used");
    }
    {
        // An invalid record of an earlier source shadows a valid record of
        // a later one, which is the paragraph docs/TRACK.md gives its own
        // line. Source 0's only record is malformed; source 1 has a sound
        // record for the same hash and never gets to supply it.
        PackSpec broken_first;
        broken_first.textures = {pack_texture(0xAAAA0002U, 0x22)};
        broken_first.textures[0].virtual_offset = 0x40000U;
        TextureFile early_file;
        early_file.build({broken_first});
        check(early_file.set.complete &&
                  early_file.set.packs[0].valid_texture_count() == 0 &&
                  early_file.set.packs[0].textures.size() == 1,
              "a source of one refused record is still complete");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report, {early_file.source(&wanted), b.source(&wanted)}, set, prepared,
            error);
        check(ok, "the invalid-shadow region prepares: " + error);
        check(set.blocked_sources.empty() && set.shadowed.size() == 1 &&
                  set.shadowed[0].hash == 0xAAAA0002U &&
                  set.shadowed[0].source == 1 && set.shadowed[0].by_source == 0,
              "the sound later record is shadowed by the refused earlier one");
        check(prepared.cost.unusable_draws == 2 &&
                  drawn_fill(prepared, 1) == -1,
              "no silent fallback to the lower-priority source");
        check(prepared.draws.size() == 4 && prepared.draws[3].binding ==
                                                nfsu::BindingKind::resolved &&
                  drawn_fill(prepared, 3) == 0x44,
              "the later source still supplies the hash nothing contested");
    }
    {
        // A source whose record tables could not be read is incomplete, so
        // no later source is used at all and every hash is unresolved. The
        // decoder reaches that on its own: the caller's complete flag is set
        // here and the pack's tables_parsed still overrides it.
        PackSpec broken;
        broken.textures = {pack_texture(0xAAAA0001U, 0x11),
                           pack_texture(0xAAAA0002U, 0x22)};
        broken.short_hash_table = true;
        TextureFile bad;
        bad.build({broken});
        check(!bad.set.packs.empty() && !bad.set.packs[0].tables_parsed &&
                  bad.set.packs[0].textures.empty() && !bad.set.complete,
              "a table count mismatch stops the reader before the records");

        nfsu::RenderTextureSource lying = bad.source(&wanted);
        lying.complete = true;
        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(report, {lying, b.source(&wanted)}, set,
                                           prepared, error);
        check(ok, "the blocked-source region prepares: " + error);
        check(set.blocked_sources.size() == 1 && set.blocked_sources[0] == 1,
              "an incomplete source blocks every later source");
        check(set.textures.empty() && prepared.cost.unresolved_draws == 4 &&
                  prepared.unresolved_hashes.size() == 3,
              "nothing resolves behind an incomplete source");
    }
}

// Which malformations the reader refuses and which it accepts. Each pack
// carries all three hashes the region names with one record malformed, so a
// containment failure shows as a changed count for the other two. Every
// malformation below leaves the source complete, which is what lets a later
// source still be read; the one that does not is the table count mismatch in
// test_track_texture_source_collision.
void test_track_malformed_texture_pack() {
    Bytes region;
    const nfsu::TrackRegionReport report = two_mesh_region(region);
    const std::vector<std::uint32_t> wanted =
        nfsu::track_region_texture_hashes(report);

    struct Case {
        std::string what;
        PackSpec spec;
        std::string message;
        // The draw the malformed record's hash belongs to, and how many
        // draws lose their texture because of it.
        std::size_t bad_draw;
        std::uint64_t unusable;
    };

    std::vector<Case> cases;
    for (int variant = 0; variant < 6; ++variant) {
        PackSpec spec;
        spec.textures = {pack_texture(0xAAAA0001U, 0x11),
                         pack_texture(0xAAAA0002U, 0x22),
                         pack_texture(0xAAAA0003U, 0x44)};
        Case item;
        item.bad_draw = 1;
        item.unusable = 2;
        switch (variant) {
        case 0:
            item.what = "a declared size that does not match the dimensions";
            spec.textures[1].data_size = 20;  // 2x2 A8R8G8B8 needs 16
            item.message = "info +64 is 16 and +56 is 20";
            break;
        case 1:
            item.what = "a declared top level that does not match the dimensions";
            spec.textures[1].top_level_size = 12;  // 2x2 A8R8G8B8 needs 16
            item.message = "info +64 is 12 and +56 is 16";
            break;
        case 2:
            item.what = "a zero dimension";
            spec.textures[1].height = 0;
            item.message = "dimensions 2x0 are outside";
            break;
        case 3:
            item.what = "a chain that starts past the payload";
            spec.textures[1].virtual_offset = 0x40000U;
            item.message = "is outside the texture data range";
            break;
        case 4:
            item.what = "a chain that is not 128-byte aligned";
            spec.textures[1].virtual_offset = 4U;
            item.message = "is not a multiple of 128";
            break;
        default:
            item.what = "a payload truncated below the last declared chain";
            spec.truncate_last_chain = true;
            item.message = "is outside the texture data range";
            item.bad_draw = 3;
            item.unusable = 1;
            break;
        }
        item.spec = spec;
        cases.push_back(item);
    }

    for (const Case& item : cases) {
        TextureFile file;
        file.build({item.spec});
        check(!file.set.packs.empty() && file.set.packs[0].tables_parsed &&
                  file.set.complete,
              item.what + ": the reader still reads the record tables");
        check(file.set.packs[0].issues.size() == 1 &&
                  file.set.packs[0].issues[0].message.find(item.message) !=
                      std::string::npos,
              item.what + ": the reader names the record, got " +
                  (file.set.packs[0].issues.empty()
                       ? std::string("no issue")
                       : file.set.packs[0].issues[0].message));
        check(file.set.packs[0].valid_texture_count() == 2,
              item.what + ": exactly the malformed record is refused");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(report, {file.source(&wanted)}, set,
                                           prepared, error);
        check(ok, item.what + ": the region still prepares: " + error);
        // The refused record still claims its hash, so the hash is unusable
        // rather than unresolved: nothing is silently substituted for it.
        check(prepared.draws.size() == 4 &&
                  prepared.draws[item.bad_draw].binding ==
                      nfsu::BindingKind::unusable &&
                  prepared.draws[item.bad_draw].texture == nfsu::kNoHandle &&
                  prepared.draws[item.bad_draw].drawable,
              item.what + ": the draw falls back rather than resolving");
        check(prepared.unresolved_hashes.empty(),
              item.what + ": a refused record still claims its hash");
        check(prepared.cost.unusable_draws == item.unusable &&
                  prepared.cost.resolved_draws == 4 - item.unusable,
              item.what + ": the other hashes are unaffected");
    }

    {
        // A duplicate hash inside one pack is accepted at the record level:
        // both records stay valid and both are kept. The ambiguity surfaces
        // only as an unusable binding.
        PackSpec spec;
        spec.textures = {pack_texture(0xAAAA0001U, 0x11),
                         pack_texture(0xAAAA0002U, 0x22),
                         pack_texture(0xAAAA0002U, 0x33),
                         pack_texture(0xAAAA0003U, 0x44)};
        TextureFile file;
        file.build({spec});
        check(file.set.complete && file.set.packs[0].tables_parsed &&
                  file.set.packs[0].valid_texture_count() == 4,
              "a repeated hash leaves every record valid");
        check(file.set.packs[0].issues.size() == 1 &&
                  file.set.packs[0].issues[0].message.find(
                      "repeats hash 0xAAAA0002") != std::string::npos,
              "the repeated hash is reported as a pack issue");

        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(report, {file.source(&wanted)}, set,
                                           prepared, error);
        check(ok, "the repeated-hash region prepares: " + error);
        check(set.textures.size() == 4 && set.shadowed.empty(),
              "both records of the repeated hash are kept");
        check(prepared.cost.unusable_draws == 2 &&
                  prepared.unresolved_hashes.empty(),
              "a repeated hash is unusable, not unresolved");
    }
    {
        // A readable file with no pack root claims nothing and stays
        // complete, so a later source is read and resolves everything.
        PackSpec empty;
        empty.no_root = true;
        TextureFile none;
        none.build({empty});
        check(none.set.complete && none.set.packs.empty() &&
                  none.set.issues.size() == 1,
              "a file without a pack root is complete but reported");

        PackSpec all;
        all.textures = {pack_texture(0xAAAA0001U, 0x11),
                        pack_texture(0xAAAA0002U, 0x22),
                        pack_texture(0xAAAA0003U, 0x44)};
        TextureFile good;
        good.build({all});
        nfsu::RenderTextureSet set;
        nfsu::PreparedTrackRegion prepared;
        std::string error;
        const bool ok = decode_and_prepare(
            report, {none.source(&wanted), good.source(&wanted)}, set, prepared,
            error);
        check(ok, "the rootless-source region prepares: " + error);
        check(set.blocked_sources.empty() && prepared.cost.resolved_draws == 4,
              "a rootless source does not block the source behind it");
        check(drawn_fill(prepared, 1) == 0x22,
              "the later source supplies the pixels");
    }
}

}  // namespace

int main() {
    test_vertex_format_table();
    test_well_formed_region();
    test_multiple_geometry_roots();
    test_hash_table_needs_every_mesh_hash();
    test_root_without_meshes();
    test_header_without_a_hash_table();
    test_root_without_a_leading_padding_chunk();
    test_hash_records_match_by_hash_not_position();
    test_parse_is_deterministic();
    test_empty_mesh_is_not_an_error();
    test_mixed_counts_are_rejected();
    test_other_strides_are_refused();
    test_wrong_vertex_payload_size();
    test_vertex_payload_not_a_whole_stride();
    test_count_word_high_half();
    test_per_mesh_count_limits();
    test_region_vertex_and_triangle_limits();
    test_mesh_and_chunk_limits();
    test_alignment_prefix_bytes();
    test_index_and_material_validation();
    test_hash_table_rules();
    test_region_boundaries();
    test_load_track_region_reads_only_the_region();
    test_prepared_region();
    test_prepared_region_bindings();
    test_prepared_region_skips_empty_and_invalid();
    test_texture_binds_and_unresolved_order();
    test_invalid_texture_slot_binding();
    test_decode_bookkeeping_does_not_change_the_region();
    test_prepared_region_limits();
    test_validate_prepared_region_rejects_tampering();
    test_track_texture_source_collision();
    test_track_malformed_texture_pack();

    if (g_failures != 0) {
        std::cerr << g_failures << " track test failures of " << g_checks
                  << " checks\n";
        return 1;
    }
    std::cout << "track tests passed (" << g_checks << " checks)\n";
    return 0;
}
