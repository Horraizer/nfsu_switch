#include "nfsu/geometry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
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

void put_i32(Bytes& bytes, std::size_t offset, std::int32_t value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u32(bytes, offset, bits);
}

// Writes a chunk tree into one buffer. Buffer positions are absolute file
// offsets, so alignment prefixes are computed exactly as the format does.
class Writer {
public:
    std::size_t size() const { return bytes_.size(); }
    Bytes& bytes() { return bytes_; }

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

    // Appends 0x11 bytes up to the next multiple of alignment; returns the
    // number of bytes written.
    std::size_t pad_to(std::size_t alignment) {
        std::size_t count = 0;
        while (bytes_.size() % alignment != 0) {
            bytes_.push_back(0x11U);
            ++count;
        }
        return count;
    }

private:
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

struct TestVertex {
    float px, py, pz, nx, ny, nz, u, v;
};

enum class Corrupt {
    none,
    metadata_prefix,
    vertex_prefix,
    face_prefix,
    material_prefix
};

struct MaterialSpec {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    std::uint32_t texture_slot = 0;
    std::uint32_t material_key_slot = 0;
};

// One synthetic 0x0013401A record.
struct TestMarker {
    std::uint32_t name_hash = 0xBCF8A18BU;  // bin_hash("LEFT_EXHAUST")
    std::array<std::uint32_t, 3> reserved{};
    std::array<float, 16> matrix{{1.0F, 0.0F, 0.0F, 0.0F,
                                  0.0F, 1.0F, 0.0F, 0.0F,
                                  0.0F, 0.0F, 1.0F, 0.0F,
                                  0.0F, 0.0F, 0.0F, 1.0F}};
};

struct MeshSpec {
    std::string name = "BODY";
    std::vector<TestVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::optional<std::int32_t> declared_vertices;
    std::optional<std::int32_t> declared_faces;
    // 0x00134012 texture list and the count byte at 0x00134011 +25.
    std::vector<std::uint32_t> texture_hashes = {0xCAFEF00DU};
    std::optional<unsigned char> texture_count_byte;
    std::uint32_t texture_second_word = 0;
    bool include_texture_list = true;
    // 0x00134013 material-key list and the count byte at 0x00134011 +26.
    std::vector<std::uint32_t> material_key_hashes = {0x0BADF00DU};
    std::optional<unsigned char> material_key_count_byte;
    std::uint32_t material_key_second_word = 0;
    std::size_t material_key_extra = 0;
    bool include_material_key_list = true;
    // 0x0013401A attachment markers. The chunk is omitted unless
    // include_marker_list is set; most retail meshes have none.
    bool include_marker_list = false;
    std::vector<TestMarker> markers;
    // Extra bytes appended after the marker records, which breaks the stride.
    std::size_t marker_extra = 0;
    // Emit the marker chunk twice.
    bool duplicate_marker_list = false;
    // Overwrite the first alignment filler byte with something other than 0x11.
    bool corrupt_marker_prefix = false;
    // An ignored sibling chunk used to shift later records in alignment tests.
    std::size_t alignment_shift_size = 0;
    // Default: one material covering every index with texture slot 0.
    std::optional<std::vector<MaterialSpec>> materials;
    std::optional<std::int32_t> declared_materials;
    std::size_t material_extra = 0;
    std::size_t metadata_size = 64;
    unsigned char metadata_first_byte = 0x00U;
    std::optional<Bytes> raw_metadata;
    std::size_t vertex_extra = 0;
    std::optional<std::size_t> face_tail;
    unsigned char face_tail_byte = 0x00U;
    Corrupt corrupt = Corrupt::none;
    bool include_faces = true;
    bool name_terminated = true;
};

struct MeshLayout {
    std::size_t container_offset = 0;
    std::size_t container_size = 0;
    std::size_t metadata_payload_offset = 0;
    std::size_t metadata_record_offset = 0;
    std::size_t metadata_prefix = 0;
    std::size_t vertex_payload_offset = 0;
    std::size_t vertex_data_offset = 0;
    std::size_t vertex_prefix = 0;
    std::size_t face_payload_offset = 0;
    std::size_t face_data_offset = 0;
    std::size_t face_prefix = 0;
    std::size_t face_tail = 0;
    std::size_t texture_list_offset = 0;
    std::size_t material_key_list_offset = 0;
    std::size_t material_payload_offset = 0;
    std::size_t material_data_offset = 0;
    std::size_t material_prefix = 0;
    std::size_t marker_payload_offset = 0;
    std::size_t marker_data_offset = 0;
    std::size_t marker_prefix = 0;
};

std::vector<MaterialSpec> materials_for(const MeshSpec& spec) {
    if (spec.materials) {
        return *spec.materials;
    }
    return {MaterialSpec{0, static_cast<std::uint32_t>(spec.indices.size()), 0}};
}

struct Fixture {
    Bytes bytes;
    std::vector<MeshLayout> meshes;
    std::size_t hash_table_header_offset = 0;
    std::size_t hash_table_payload_offset = 0;
    std::size_t directory_header_offset = 0;
    std::size_t directory_payload_offset = 0;
    std::vector<std::size_t> root_directory_payload_offsets;
};

void write_mesh(Writer& w, const MeshSpec& spec, MeshLayout& layout) {
    layout.container_offset = w.size();
    w.begin(nfsu::kMeshContainerId);

    // Mesh metadata: 176 bytes, name in the final 28 bytes.
    Bytes mesh_metadata(176, 0);
    for (std::size_t i = 0; i < spec.name.size() && i < 28; ++i) {
        mesh_metadata[176 - 28 + i] = static_cast<unsigned char>(spec.name[i]);
    }
    if (!spec.name_terminated) {
        for (std::size_t i = spec.name.size(); i < 28; ++i) {
            mesh_metadata[176 - 28 + i] = 'X';
        }
    }
    mesh_metadata[25] = spec.texture_count_byte.value_or(
        static_cast<unsigned char>(spec.texture_hashes.size()));
    mesh_metadata[26] = spec.material_key_count_byte.value_or(
        static_cast<unsigned char>(spec.material_key_hashes.size()));
    w.leaf(nfsu::kMeshMetadataId, mesh_metadata);
    if (spec.include_texture_list) {
        Bytes list;
        for (std::uint32_t hash : spec.texture_hashes) {
            append_u32(list, hash);
            append_u32(list, spec.texture_second_word);
        }
        layout.texture_list_offset = w.size() + 8;
        w.leaf(nfsu::kTextureListId, list);
    }
    if (spec.include_material_key_list) {
        Bytes list;
        for (std::uint32_t hash : spec.material_key_hashes) {
            append_u32(list, hash);
            append_u32(list, spec.material_key_second_word);
        }
        list.insert(list.end(), spec.material_key_extra, 0);
        layout.material_key_list_offset = w.size() + 8;
        w.leaf(nfsu::kMaterialKeyListId, list);
    }
    for (int copy = 0; copy < (spec.duplicate_marker_list ? 2 : 1); ++copy) {
        if (!spec.include_marker_list) {
            break;
        }
        w.begin(nfsu::kMarkerListId);
        layout.marker_payload_offset = w.size();
        layout.marker_prefix = w.pad_to(nfsu::kMarkerDataAlignment);
        layout.marker_data_offset = w.size();
        for (const TestMarker& marker : spec.markers) {
            Bytes record;
            append_u32(record, marker.name_hash);
            for (std::uint32_t word : marker.reserved) {
                append_u32(record, word);
            }
            for (float element : marker.matrix) {
                append_f32(record, element);
            }
            w.append(record);
        }
        w.append(Bytes(spec.marker_extra, 0));
        if (spec.corrupt_marker_prefix && layout.marker_prefix != 0) {
            w.bytes()[layout.marker_payload_offset] = 0x00U;
        }
        w.end();
    }
    if (spec.alignment_shift_size != 0) {
        w.leaf(0x00134017U, Bytes(spec.alignment_shift_size, 0));
    }

    w.begin(nfsu::kGeometryContainerId);

    const std::vector<MaterialSpec> materials = materials_for(spec);
    const std::int32_t vertex_count = spec.declared_vertices.value_or(
        static_cast<std::int32_t>(spec.vertices.size()));
    const std::int32_t face_count = spec.declared_faces.value_or(
        static_cast<std::int32_t>(spec.indices.size() / 3));
    const std::int32_t material_count = spec.declared_materials.value_or(
        static_cast<std::int32_t>(materials.size()));

    w.begin(nfsu::kGeometryMetadataId);
    layout.metadata_payload_offset = w.size();
    if (spec.raw_metadata) {
        w.append(*spec.raw_metadata);
    } else {
        layout.metadata_prefix = w.pad_to(16);
        layout.metadata_record_offset = w.size();
        Bytes record(spec.metadata_size, 0);
        if (!record.empty()) {
            record[0] = spec.metadata_first_byte;
        }
        if (record.size() >= 56) {
            put_i32(record, 16, material_count);
            put_i32(record, 36, face_count);
            put_i32(record, 52, vertex_count);
        }
        w.append(record);
        if (spec.corrupt == Corrupt::metadata_prefix &&
            layout.metadata_prefix != 0) {
            w.bytes()[layout.metadata_payload_offset] = 0x00U;
        }
    }
    w.end();

    w.begin(nfsu::kVertexBufferId);
    layout.vertex_payload_offset = w.size();
    layout.vertex_prefix = w.pad_to(128);
    layout.vertex_data_offset = w.size();
    Bytes vertices;
    for (const TestVertex& v : spec.vertices) {
        append_f32(vertices, v.px);
        append_f32(vertices, v.pz);
        append_f32(vertices, v.py);
        append_f32(vertices, v.nx);
        append_f32(vertices, v.nz);
        append_f32(vertices, v.ny);
        vertices.insert(vertices.end(), {0x10U, 0x20U, 0x30U, 0x40U});
        append_f32(vertices, v.u);
        append_f32(vertices, v.v);
    }
    vertices.insert(vertices.end(), spec.vertex_extra, 0x00U);
    w.append(vertices);
    if (spec.corrupt == Corrupt::vertex_prefix && layout.vertex_prefix != 0) {
        w.bytes()[layout.vertex_payload_offset] = 0x00U;
    }
    w.end();

    w.begin(nfsu::kMaterialBufferId);
    layout.material_payload_offset = w.size();
    layout.material_prefix = w.pad_to(16);
    layout.material_data_offset = w.size();
    for (const MaterialSpec& material : materials) {
        Bytes record(60, 0);
        put_u32(record, 12, material.index_count);
        put_u32(record, 28, material.texture_slot);
        put_u32(record, 32, material.material_key_slot);
        put_u32(record, 52, material.first_index);
        put_u32(record, 56, 0x4180U);
        w.append(record);
    }
    w.append(Bytes(spec.material_extra, 0));
    if (spec.corrupt == Corrupt::material_prefix && layout.material_prefix != 0) {
        w.bytes()[layout.material_payload_offset] = 0x00U;
    }
    w.end();

    if (spec.include_faces) {
        w.begin(nfsu::kFaceBufferId);
        layout.face_payload_offset = w.size();
        layout.face_prefix = w.pad_to(16);
        layout.face_data_offset = w.size();
        Bytes faces;
        for (std::uint16_t index : spec.indices) {
            append_u16(faces, index);
        }
        layout.face_tail =
            spec.face_tail.value_or(faces.size() % 4 == 0 ? 0 : 2);
        faces.insert(faces.end(), layout.face_tail, spec.face_tail_byte);
        w.append(faces);
        if (spec.corrupt == Corrupt::face_prefix && layout.face_prefix != 0) {
            w.bytes()[layout.face_payload_offset] = 0x00U;
        }
        w.end();
    }

    w.end(); // geometry container
    w.end(); // mesh container
    layout.container_size = w.size() - layout.container_offset;
}

Fixture build(std::initializer_list<MeshSpec> specs, bool with_directory = false) {
    Fixture fixture;
    Writer w;
    w.begin(nfsu::kGeometryRootId);
    w.leaf(0x00000000U, Bytes(4, 0));
    for (const MeshSpec& spec : specs) {
        MeshLayout layout;
        write_mesh(w, spec, layout);
        fixture.meshes.push_back(layout);
        w.leaf(0x00000000U, Bytes());
    }
    if (with_directory) {
        w.begin(nfsu::kGeometryHeaderId);
        Bytes hashes;
        Bytes directory;
        for (std::size_t i = 0; i < fixture.meshes.size(); ++i) {
            const std::uint32_t hash =
                0xA0000000U + static_cast<std::uint32_t>(i);
            append_u32(hashes, hash);
            append_u32(hashes, 0);
            append_u32(directory, hash);
            append_u32(directory, static_cast<std::uint32_t>(
                                      fixture.meshes[i].container_offset));
            append_u32(directory, static_cast<std::uint32_t>(
                                      fixture.meshes[i].container_size));
            append_u32(directory, 0);
            append_u32(directory, 0);
        }
        fixture.hash_table_header_offset = w.size();
        fixture.hash_table_payload_offset = w.size() + 8;
        w.leaf(nfsu::kMeshHashTableId, hashes);
        fixture.directory_header_offset = w.size();
        fixture.directory_payload_offset = w.size() + 8;
        fixture.root_directory_payload_offsets.push_back(w.size() + 8);
        w.leaf(nfsu::kMeshDirectoryId, directory);
        w.end();
    }
    w.end();
    w.begin(0x80034020U);
    w.leaf(0x00039200U, Bytes(4, 0));
    w.end();
    fixture.bytes = w.bytes();
    return fixture;
}

MeshSpec quad_spec();
MeshSpec triangle_spec();

Fixture build_two_directory_roots() {
    Fixture fixture;
    Writer w;
    const MeshSpec specs[] = {quad_spec(), triangle_spec()};
    for (std::size_t root = 0; root < 2; ++root) {
        w.begin(nfsu::kGeometryRootId);
        MeshLayout layout;
        write_mesh(w, specs[root], layout);
        fixture.meshes.push_back(layout);

        const std::uint32_t hash =
            0xC0000000U + static_cast<std::uint32_t>(root);
        Bytes hashes;
        append_u32(hashes, hash);
        append_u32(hashes, 0);
        Bytes directory;
        append_u32(directory, hash);
        append_u32(directory, static_cast<std::uint32_t>(layout.container_offset));
        append_u32(directory, static_cast<std::uint32_t>(layout.container_size));
        append_u32(directory, 0);
        append_u32(directory, 0);

        w.begin(nfsu::kGeometryHeaderId);
        w.leaf(nfsu::kMeshHashTableId, hashes);
        fixture.root_directory_payload_offsets.push_back(w.size() + 8);
        w.leaf(nfsu::kMeshDirectoryId, directory);
        w.end();
        w.end();
    }
    fixture.bytes = w.bytes();
    return fixture;
}

MeshSpec quad_spec() {
    MeshSpec spec;
    spec.name = "BODY_A";
    spec.vertices = {
        {0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F},
        {1.0F, 0.0F, 2.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F},
        {0.0F, 0.0F, 2.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F},
    };
    spec.indices = {0, 1, 2, 0, 2, 3};
    return spec;
}

// One triangle: 6 bytes of face data, so a 2-byte zero tail follows.
MeshSpec triangle_spec() {
    MeshSpec spec;
    spec.name = "WHEEL #1";
    spec.vertices = {
        {0.5F, -1.5F, 0.25F, 1.0F, 0.0F, 0.0F, 0.5F, 0.25F},
        {0.5F, 1.5F, 0.25F, 1.0F, 0.0F, 0.0F, 0.75F, 0.5F},
        {0.5F, 0.0F, 3.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F},
    };
    spec.indices = {0, 1, 2};
    return spec;
}

nfsu::GeometryReport parse(const Bytes& bytes) {
    return nfsu::parse_geometry(bytes.data(), bytes.size());
}

bool has_issue(const nfsu::GeometryReport& report, const std::string& text) {
    for (const nfsu::GeometryIssue& issue : report.issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void dump_issues(const nfsu::GeometryReport& report) {
    for (const nfsu::GeometryIssue& issue : report.issues) {
        std::cerr << "  issue at " << issue.offset << ": " << issue.message
                  << '\n';
    }
}

// Checks that parsed offsets match the fixture layout and the alignment
// rules of the format.
bool layout_matches(const nfsu::Mesh& mesh, const MeshLayout& layout) {
    return mesh.metadata_record_offset == layout.metadata_record_offset &&
           mesh.metadata_prefix_bytes == layout.metadata_prefix &&
           mesh.vertex_data_offset == layout.vertex_data_offset &&
           mesh.vertex_prefix_bytes == layout.vertex_prefix &&
           mesh.face_data_offset == layout.face_data_offset &&
           mesh.face_prefix_bytes == layout.face_prefix &&
           mesh.face_tail_bytes == layout.face_tail &&
           mesh.texture_list_offset == layout.texture_list_offset &&
           mesh.material_key_list_offset == layout.material_key_list_offset &&
           mesh.material_data_offset == layout.material_data_offset &&
           mesh.material_prefix_bytes == layout.material_prefix &&
           mesh.material_data_offset % 16U == 0U &&
           mesh.metadata_record_offset % 16U == 0U &&
           mesh.vertex_data_offset % 128U == 0U &&
           mesh.face_data_offset % 16U == 0U;
}

void test_valid_file() {
    const Fixture fixture = build({quad_spec(), triangle_spec()});
    const nfsu::GeometryReport report = parse(fixture.bytes);
    if (!report.ok()) {
        dump_issues(report);
    }
    check(report.ok(), "valid file reported issues");
    check(report.meshes.size() == 2U, "valid file mesh count");
    if (report.meshes.size() != 2U) {
        return;
    }

    const nfsu::Mesh& quad = report.meshes[0];
    check(quad.valid, "quad is valid");
    check(quad.name == "BODY_A", "quad name");
    check(quad.declared_vertex_count == 4, "quad vertex count");
    check(quad.declared_face_count == 2, "quad face count");
    check(layout_matches(quad, fixture.meshes[0]), "quad layout");
    check(fixture.meshes[0].metadata_prefix != 0U &&
              fixture.meshes[0].vertex_prefix != 0U &&
              fixture.meshes[0].face_prefix != 0U,
          "default fixture exercises non-empty prefixes");
    check(quad.face_tail_bytes == 0U, "even face bytes have no tail");
    check(quad.vertices.size() == 4U && quad.triangles.size() == 2U,
          "quad buffers");
    if (quad.vertices.size() == 4U) {
        const nfsu::MeshVertex& v = quad.vertices[2];
        check(v.px == 1.0F && v.py == 0.0F && v.pz == 2.0F,
              "position source order px,pz,py");
        check(v.nx == 0.0F && v.ny == 1.0F && v.nz == 0.0F,
              "normal source order nx,nz,ny");
        check(v.u == 1.0F && v.v == 1.0F, "uv");
        check(v.color[0] == 0x10U && v.color[3] == 0x40U, "color bytes");
    }

    const nfsu::Mesh& tri = report.meshes[1];
    check(tri.valid, "triangle is valid");
    check(tri.name == "WHEEL #1", "triangle name");
    check(layout_matches(tri, fixture.meshes[1]), "triangle layout");
    check(tri.face_tail_bytes == 2U, "odd face bytes have a 2-byte tail");
    check(nfsu::obj_object_name(tri) == "m0001_WHEEL__1", "object name");
}

void test_mesh_directory() {
    const Fixture fixture = build({quad_spec(), triangle_spec()}, true);
    const nfsu::GeometryReport report = parse(fixture.bytes);
    if (!report.ok()) {
        dump_issues(report);
    }
    check(report.ok() && report.mesh_directory_valid,
          "valid mesh directory accepted");
    check(report.meshes.size() == 2U, "directory mesh count");
    if (report.meshes.size() == 2U) {
        for (std::size_t i = 0; i < report.meshes.size(); ++i) {
            const nfsu::Mesh& mesh = report.meshes[i];
            check(mesh.has_directory_entry &&
                      mesh.directory_hash ==
                          0xA0000000U + static_cast<std::uint32_t>(i) &&
                      mesh.directory_offset == fixture.meshes[i].container_offset &&
                      mesh.directory_size == fixture.meshes[i].container_size,
                  "verified directory entry " + std::to_string(i));
        }
    }

    const auto rejects = [&](Bytes bytes, const std::string& needle,
                             const std::string& label) {
        const nfsu::GeometryReport bad = parse(bytes);
        check(!bad.mesh_directory_valid && has_issue(bad, needle), label);
        bool attached = false;
        for (const nfsu::Mesh& mesh : bad.meshes) {
            attached = attached || mesh.has_directory_entry;
        }
        check(!attached, label + " is transactional");
    };

    Bytes bytes = fixture.bytes;
    put_u32(bytes, fixture.hash_table_header_offset, 0x00134005U);
    rejects(bytes, "mesh hash table, found 0", "missing hash table rejected");

    bytes = fixture.bytes;
    put_u32(bytes, fixture.directory_header_offset, 0x00134005U);
    rejects(bytes, "mesh directory, found 0", "missing directory rejected");

    bytes = fixture.bytes;
    put_u32(bytes, fixture.directory_payload_offset + nfsu::kMeshDirectoryRecordSize,
            0xB0000001U);
    rejects(bytes, "hash does not match", "mismatched table hashes rejected");

    bytes = fixture.bytes;
    put_u32(bytes, fixture.hash_table_payload_offset + 4, 1U);
    rejects(bytes, "nonzero reserved", "hash reserved word rejected");

    bytes = fixture.bytes;
    put_u32(bytes, fixture.directory_payload_offset + 4, 0xFFFFFFFFU);
    rejects(bytes, "range is outside", "out of bounds directory range rejected");

    bytes = fixture.bytes;
    put_u32(bytes, fixture.directory_payload_offset + 8,
            static_cast<std::uint32_t>(fixture.meshes[0].container_size - 1));
    rejects(bytes, "size does not match", "wrong container size rejected");

    bytes = fixture.bytes;
    const std::size_t second =
        fixture.directory_payload_offset + nfsu::kMeshDirectoryRecordSize;
    put_u32(bytes, second + 4,
            static_cast<std::uint32_t>(fixture.meshes[0].container_offset));
    put_u32(bytes, second + 8,
            static_cast<std::uint32_t>(fixture.meshes[0].container_size));
    rejects(bytes, "more than once", "duplicate directory range rejected");

    const Fixture two_roots = build_two_directory_roots();
    const nfsu::GeometryReport two_report = parse(two_roots.bytes);
    check(two_report.ok() && two_report.mesh_directory_valid &&
              two_report.meshes.size() == 2U &&
              two_report.meshes[0].directory_hash == 0xC0000000U &&
              two_report.meshes[1].directory_hash == 0xC0000001U,
          "two geometry roots keep independent directories");

    bytes = two_roots.bytes;
    put_u32(bytes, two_roots.root_directory_payload_offsets[0] + 4,
            static_cast<std::uint32_t>(two_roots.meshes[1].container_offset));
    put_u32(bytes, two_roots.root_directory_payload_offsets[0] + 8,
            static_cast<std::uint32_t>(two_roots.meshes[1].container_size));
    const nfsu::GeometryReport cross_root = parse(bytes);
    check(!cross_root.mesh_directory_valid &&
              has_issue(cross_root, "outside its geometry root") &&
              cross_root.meshes.size() == 2U &&
              !cross_root.meshes[0].has_directory_entry &&
              cross_root.meshes[1].has_directory_entry,
          "cross-root directory range rejected without harming the other root");

    Bytes valid_plus_bad_empty = build({quad_spec()}, true).bytes;
    Writer empty_root;
    empty_root.begin(nfsu::kGeometryRootId);
    empty_root.begin(nfsu::kGeometryHeaderId);
    empty_root.leaf(nfsu::kMeshHashTableId, Bytes());
    empty_root.end();
    empty_root.end();
    valid_plus_bad_empty.insert(valid_plus_bad_empty.end(),
                                empty_root.bytes().begin(),
                                empty_root.bytes().end());
    const nfsu::GeometryReport bad_empty = parse(valid_plus_bad_empty);
    check(!bad_empty.mesh_directory_valid &&
              has_issue(bad_empty, "mesh directory, found 0") &&
              bad_empty.meshes.size() == 1U &&
              bad_empty.meshes[0].has_directory_entry,
          "malformed empty root disables file-wide hash selection");

    Bytes valid_plus_bad_root = build({quad_spec()}, true).bytes;
    Writer bad_root;
    bad_root.begin(nfsu::kGeometryRootId);
    bad_root.append(Bytes(4, 0));
    bad_root.end();
    valid_plus_bad_root.insert(valid_plus_bad_root.end(),
                               bad_root.bytes().begin(), bad_root.bytes().end());
    const nfsu::GeometryReport malformed_root = parse(valid_plus_bad_root);
    check(!malformed_root.mesh_directory_valid &&
              has_issue(malformed_root, "trailing bytes are smaller") &&
              malformed_root.meshes.size() == 1U &&
              malformed_root.meshes[0].has_directory_entry,
          "malformed no-directory root disables file-wide hash selection");

    Bytes top_tail = build({quad_spec()}, true).bytes;
    top_tail.insert(top_tail.end(), 4, 0);
    const nfsu::GeometryReport malformed_top = parse(top_tail);
    check(!malformed_top.mesh_directory_valid &&
              has_issue(malformed_top, "top level: 4 trailing bytes") &&
              malformed_top.meshes.size() == 1U &&
              malformed_top.meshes[0].has_directory_entry,
          "malformed top level disables file-wide hash selection");
}

void test_alignment_sweep() {
    // An ignored sibling chunk moves every later record across all residues,
    // including records that need no prefix at all.
    bool saw_zero_metadata_prefix = false;
    bool saw_zero_vertex_prefix = false;
    bool saw_zero_face_prefix = false;
    for (std::size_t shift = 0; shift <= 256; shift += 4) {
        MeshSpec quad = quad_spec();
        quad.alignment_shift_size = shift;
        MeshSpec tri = triangle_spec();
        tri.alignment_shift_size = shift + 4;
        // The face prefix depends only on the vertex and material counts
        // because vertex data is 128-byte aligned and material data 16-byte
        // aligned; two 60-byte materials put the faces on a 16-byte boundary
        // with no prefix.
        MeshSpec five = quad_spec();
        five.vertices.push_back(five.vertices[0]);
        five.materials = std::vector<MaterialSpec>{{0, 3, 0}, {3, 3, 0}};
        five.alignment_shift_size = shift;
        const Fixture fixture = build({quad, tri, five});
        const nfsu::GeometryReport report = parse(fixture.bytes);
        bool ok = report.ok() && report.valid_mesh_count() == 3U;
        for (std::size_t i = 0; ok && i < 3U; ++i) {
            ok = layout_matches(report.meshes[i], fixture.meshes[i]);
        }
        if (!ok) {
            dump_issues(report);
        }
        check(ok, "alignment sweep shift " + std::to_string(shift));
        for (const MeshLayout& layout : fixture.meshes) {
            saw_zero_metadata_prefix |= layout.metadata_prefix == 0U;
            saw_zero_vertex_prefix |= layout.vertex_prefix == 0U;
            saw_zero_face_prefix |= layout.face_prefix == 0U;
        }
    }
    // The vertex payload always starts 8 bytes after the 16-byte aligned end
    // of the metadata record, so its prefix is never empty (the retail
    // sample agrees: 8 to 120 bytes).
    check(saw_zero_metadata_prefix && saw_zero_face_prefix &&
              !saw_zero_vertex_prefix,
          "sweep covers already-aligned records");
}

void test_leading_0x11_data_does_not_shift() {
    // The first metadata record byte and the first vertex byte are 0x11. A
    // parser that scanned leading 0x11 bytes would start one byte late.
    MeshSpec spec = quad_spec();
    spec.metadata_first_byte = 0x11U;
    std::uint32_t bits = 0x3F800011U;
    std::memcpy(&spec.vertices[0].px, &bits, sizeof(bits));
    const Fixture fixture = build({spec});
    const MeshLayout& layout = fixture.meshes[0];
    check(fixture.bytes[layout.metadata_record_offset] == 0x11U &&
              fixture.bytes[layout.vertex_data_offset] == 0x11U &&
              layout.metadata_prefix != 0U && layout.vertex_prefix != 0U,
          "regression fixture has 0x11 right after both prefixes");

    const nfsu::GeometryReport report = parse(fixture.bytes);
    if (!report.ok()) {
        dump_issues(report);
    }
    check(report.ok() && report.valid_mesh_count() == 1U,
          "0x11 first data bytes parse");
    if (report.meshes.size() == 1U && report.meshes[0].valid) {
        const nfsu::Mesh& mesh = report.meshes[0];
        check(layout_matches(mesh, layout), "0x11 first data bytes layout");
        check(mesh.declared_vertex_count == 4 && mesh.declared_face_count == 2,
              "counts read from the aligned record");
        std::uint32_t parsed = 0;
        std::memcpy(&parsed, &mesh.vertices[0].px, sizeof(parsed));
        check(parsed == bits && mesh.vertices[3].pz == 2.0F,
              "vertices read from the aligned offset");
    }
}

void test_obj_output() {
    const nfsu::GeometryReport report =
        parse(build({quad_spec(), triangle_spec()}).bytes);
    std::ostringstream output;
    nfsu::ObjExportStats stats;
    std::string error;
    check(nfsu::write_obj(output, report, stats, error), "write_obj succeeded");
    check(stats.meshes == 2U && stats.vertices == 7U && stats.triangles == 3U,
          "obj stats");

    const std::string expected =
        "# nfsu_probe diagnostic OBJ export\n"
        "# axes: x = source px, y = source py, z = -source pz"
        " (source float order px, pz, py)\n"
        "# winding: source triangle order (a, b, c) is kept\n"
        "# uv: vt u v = raw source u v (origin not confirmed)\n"
        "o m0000_BODY_A\ng m0000_BODY_A\n"
        "v 0 0 0\nv 1 0 0\nv 1 0 -2\nv 0 0 -2\n"
        "vn 0 1 0\nvn 0 1 0\nvn 0 1 0\nvn 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        // Counter-clockwise around +y in the right-handed output, matching
        // the vertex normals, as observed for real 240SX data.
        "f 1/1/1 2/2/2 3/3/3\n"
        "f 1/1/1 3/3/3 4/4/4\n"
        "o m0001_WHEEL__1\ng m0001_WHEEL__1\n"
        "v 0.5 -1.5 -0.25\nv 0.5 1.5 -0.25\nv 0.5 0 -3\n"
        "vn 1 0 0\nvn 1 0 0\nvn 1 0 0\n"
        "vt 0.5 0.25\nvt 0.75 0.5\nvt 1 1\n"
        "f 5/5/5 6/6/6 7/7/7\n";
    if (output.str() != expected) {
        std::cerr << "actual OBJ:\n" << output.str();
    }
    check(output.str() == expected, "OBJ text is exact and deterministic");

    std::ostringstream again;
    check(nfsu::write_obj(again, report, stats, error) &&
              again.str() == output.str(),
          "OBJ output repeats identically");
}

void test_invalid_mesh_is_skipped_and_offsets_stay_correct() {
    MeshSpec bad = quad_spec();
    bad.indices = {0, 1, 9, 0, 2, 3};
    const nfsu::GeometryReport report =
        parse(build({bad, triangle_spec()}).bytes);
    check(!report.ok(), "out of range index reported");
    check(has_issue(report,
                    "out of range for 4 vertices; first is triangle 0 index 9"),
          "out of range diagnostic is precise");
    check(report.meshes.size() == 2U && !report.meshes[0].valid &&
              report.meshes[1].valid,
          "parsing continued after invalid mesh");
    check(report.meshes.empty() || report.meshes[0].vertices.empty(),
          "invalid mesh holds no data");

    std::ostringstream output;
    nfsu::ObjExportStats stats;
    std::string error;
    check(nfsu::write_obj(output, report, stats, error), "partial export");
    check(stats.meshes == 1U && stats.vertices == 3U, "partial export stats");
    check(output.str().find("f 1/1/1 2/2/2 3/3/3\n") != std::string::npos,
          "second mesh indices restart at one when first is skipped");
    check(output.str().find("m0000") == std::string::npos,
          "invalid mesh not exported");
}

void expect_mesh_rejected(const MeshSpec& spec, const std::string& needle,
                          const std::string& label) {
    const nfsu::GeometryReport report =
        parse(build({spec, triangle_spec()}).bytes);
    const bool data_cleared =
        report.meshes.empty() ||
        (report.meshes[0].vertices.empty() &&
         report.meshes[0].triangles.empty() &&
         report.meshes[0].texture_hashes.empty() &&
         report.meshes[0].material_key_hashes.empty() &&
         report.meshes[0].materials.empty() &&
         report.meshes[0].markers.empty() &&
         !report.meshes[0].has_marker_chunk &&
         report.meshes[0].marker_data_offset == 0 &&
         report.meshes[0].marker_prefix_bytes == 0);
    const bool ok = !report.ok() && report.meshes.size() == 2U &&
                    !report.meshes[0].valid && report.meshes[1].valid &&
                    has_issue(report, needle) && data_cleared;
    if (!ok) {
        std::cerr << "case '" << label << "' expected issue containing '"
                  << needle << "'\n";
        dump_issues(report);
    }
    check(ok, label);
}

void test_bad_counts() {
    {
        MeshSpec spec = quad_spec();
        spec.declared_vertices = 0;
        expect_mesh_rejected(spec, "vertex count 0", "zero vertex count");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_vertices = -5;
        expect_mesh_rejected(spec, "vertex count -5 in", "negative vertex count");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_faces = std::numeric_limits<std::int32_t>::min();
        expect_mesh_rejected(spec, "face count -2147483648",
                             "negative face count");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_vertices = std::numeric_limits<std::int32_t>::max();
        expect_mesh_rejected(spec, "exceeds the limit of 65536",
                             "huge vertex count rejected before allocation");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_faces = 0x7FFFFFF0;
        expect_mesh_rejected(spec, "exceeds the limit", "huge face count");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_vertices = 60000;
        expect_mesh_rejected(spec, "60000 vertices of 36 bytes (2160000 bytes)",
                             "vertex count larger than buffer");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_vertices = 3;
        expect_mesh_rejected(spec, "3 vertices of 36 bytes (108 bytes)",
                             "vertex count smaller than buffer");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_faces = 3;
        expect_mesh_rejected(spec, "3 triangles of 6 bytes (18 bytes) and 2 zero",
                             "face count larger than buffer");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_faces = 1;
        expect_mesh_rejected(spec, "1 triangles of 6 bytes (6 bytes) and 2 zero",
                             "face count smaller than buffer");
    }
}

void test_bad_layout() {
    {
        MeshSpec spec = quad_spec();
        spec.metadata_size = 60;
        expect_mesh_rejected(spec, "one 64-byte geometry metadata record",
                             "short geometry metadata record");
    }
    {
        MeshSpec spec = quad_spec();
        spec.metadata_size = 72;
        expect_mesh_rejected(spec, "one 64-byte geometry metadata record",
                             "long geometry metadata record");
    }
    {
        // Payload ends before the aligned record would even start.
        MeshSpec spec = quad_spec();
        spec.raw_metadata = Bytes(4, 0x11U);
        expect_mesh_rejected(spec, "payload is 4 bytes but",
                             "metadata shorter than its prefix");
    }
    {
        MeshSpec spec = quad_spec();
        spec.vertex_extra = 36;
        expect_mesh_rejected(spec, "4 vertices of 36 bytes (144 bytes)",
                             "extra bytes after vertices");
    }
    {
        MeshSpec spec = triangle_spec();
        spec.face_tail = 0;
        expect_mesh_rejected(spec, "and 2 zero tail bytes",
                             "missing tail after odd face bytes");
    }
    {
        MeshSpec spec = quad_spec();
        spec.face_tail = 2;
        expect_mesh_rejected(spec, "2 triangles of 6 bytes (12 bytes);",
                             "unexpected tail after even face bytes");
    }
    {
        MeshSpec spec = triangle_spec();
        spec.face_tail_byte = 0xEEU;
        expect_mesh_rejected(spec, "tail padding byte 0", "non-zero tail");
    }

    const struct {
        Corrupt corrupt;
        const char* label;
    } prefixes[] = {
        {Corrupt::metadata_prefix, "bad metadata prefix byte"},
        {Corrupt::vertex_prefix, "bad vertex prefix byte"},
        {Corrupt::face_prefix, "bad face prefix byte"},
    };
    for (const auto& item : prefixes) {
        MeshSpec spec = quad_spec();
        spec.corrupt = item.corrupt;
        const MeshLayout layout = build({spec}).meshes[0];
        std::size_t offset = layout.metadata_payload_offset;
        std::size_t prefix = layout.metadata_prefix;
        if (item.corrupt == Corrupt::vertex_prefix) {
            offset = layout.vertex_payload_offset;
            prefix = layout.vertex_prefix;
        } else if (item.corrupt == Corrupt::face_prefix) {
            offset = layout.face_payload_offset;
            prefix = layout.face_prefix;
        }
        check(prefix != 0U, std::string(item.label) + " precondition");
        expect_mesh_rejected(
            spec,
            "alignment prefix byte 0 (file offset " + std::to_string(offset) +
                ") is 0x00, expected 0x11",
            item.label);
    }
}

void test_bad_contents() {
    {
        MeshSpec spec = quad_spec();
        spec.include_faces = false;
        expect_mesh_rejected(spec, "expected exactly one 0x00134B03 chunk, found 0",
                             "missing face buffer");
    }
    {
        MeshSpec spec = quad_spec();
        spec.name_terminated = false;
        expect_mesh_rejected(spec, "not NUL-terminated", "unterminated name");
    }
    {
        MeshSpec spec = quad_spec();
        spec.name = "BAD\x01";
        expect_mesh_rejected(spec, "byte 3 is 0x01", "non-printable name");
    }
    {
        MeshSpec spec = quad_spec();
        spec.vertices[1].py = std::numeric_limits<float>::quiet_NaN();
        expect_mesh_rejected(spec, "vertex 1", "non-finite vertex");
    }
}

void test_materials() {
    MeshSpec spec = quad_spec();
    spec.texture_hashes = {0x11111111U, 0x4B6F3195U};
    spec.material_key_hashes = {0x22222222U, 0xAABBCCDDU};
    spec.materials =
        std::vector<MaterialSpec>{{0, 3, 1, 1}, {3, 3, 0, 0xFFU}};
    const Fixture fixture = build({spec, triangle_spec()});
    const nfsu::GeometryReport report = parse(fixture.bytes);
    if (!report.ok()) {
        dump_issues(report);
    }
    check(report.ok() && report.valid_mesh_count() == 2U, "material fixture");
    if (report.meshes.size() != 2U || !report.meshes[0].valid) {
        return;
    }
    const nfsu::Mesh& mesh = report.meshes[0];
    check(layout_matches(mesh, fixture.meshes[0]), "material layout");
    check(mesh.declared_material_count == 2 && mesh.materials.size() == 2U,
          "material count");
    check(mesh.texture_hashes.size() == 2U &&
              mesh.texture_hashes[1] == 0x4B6F3195U,
          "texture list");
    check(mesh.material_key_hashes.size() == 2U &&
              mesh.material_key_hashes[1] == 0xAABBCCDDU,
          "material-key list");
    if (mesh.materials.size() == 2U) {
        const nfsu::MeshMaterial& first = mesh.materials[0];
        check(first.texture_slot == 1U && first.index_count == 3U &&
                  first.first_index == 0U && first.flags == 0x4180U &&
                  first.material_key_slot == 1U,
              "first material fields");
        check(first.offset == fixture.meshes[0].material_data_offset &&
                  first.raw[28] == 1U,
              "first material offset and raw bytes");
        check(mesh.materials[1].first_index == 3U &&
                  mesh.materials[1].material_key_slot == 0xFFU &&
                  mesh.materials[1].offset == first.offset + 60U,
              "second material and no-entry sentinel");
        check(mesh.texture_hashes[first.texture_slot] == 0x4B6F3195U,
              "material resolves to a texture hash");
    }
}

void test_bad_materials() {
    {
        MeshSpec spec = quad_spec();
        spec.texture_count_byte = 2;
        expect_mesh_rejected(spec, "texture count byte at file offset",
                             "texture count byte mismatch");
    }
    {
        MeshSpec spec = quad_spec();
        spec.texture_second_word = 1;
        expect_mesh_rejected(spec, "non-zero second word",
                             "texture list second word");
    }
    {
        MeshSpec spec = quad_spec();
        spec.include_texture_list = false;
        expect_mesh_rejected(spec, "expected exactly one 0x00134012 chunk, found 0",
                             "missing texture list");
    }
    {
        MeshSpec spec = quad_spec();
        spec.include_material_key_list = false;
        expect_mesh_rejected(spec, "expected exactly one 0x00134013 chunk, found 0",
                             "missing material-key list");
    }
    {
        MeshSpec spec = quad_spec();
        spec.material_key_extra = 4;
        expect_mesh_rejected(spec, "not a multiple of 8",
                             "material-key list partial entry");
    }
    {
        MeshSpec spec = quad_spec();
        spec.material_key_count_byte = 2;
        expect_mesh_rejected(spec, "material-key count byte at file offset",
                             "material-key count byte mismatch");
    }
    {
        MeshSpec spec = quad_spec();
        spec.material_key_second_word = 1;
        expect_mesh_rejected(spec, "non-zero second word",
                             "material-key list second word");
    }
    {
        MeshSpec spec = quad_spec();
        spec.materials = std::vector<MaterialSpec>{{0, 6, 0, 1}};
        expect_mesh_rejected(spec, "material-key slot 1 is neither 0xFF",
                             "material-key slot out of range");
    }
    {
        MeshSpec spec = quad_spec();
        spec.materials = std::vector<MaterialSpec>{{0, 6, 1}};
        expect_mesh_rejected(spec, "texture slot 1 is outside the 1-entry",
                             "texture slot out of range");
    }
    {
        MeshSpec spec = quad_spec();
        spec.materials = std::vector<MaterialSpec>{{0, 3, 0}, {4, 3, 0}};
        expect_mesh_rejected(spec, "first index 4 is not the running total 3",
                             "material index gap");
    }
    {
        MeshSpec spec = quad_spec();
        spec.materials = std::vector<MaterialSpec>{{0, 4, 0}, {4, 2, 0}};
        expect_mesh_rejected(spec, "index count 4 is zero, not a multiple of 3",
                             "material count not a multiple of 3");
    }
    {
        MeshSpec spec = quad_spec();
        spec.materials = std::vector<MaterialSpec>{{0, 9, 0}};
        expect_mesh_rejected(spec, "exceeds the 6 remaining indices",
                             "material overruns faces");
    }
    {
        // The same rejection with a marker chunk present, so the marker
        // conjuncts in expect_mesh_rejected are exercised here too and not
        // only by test_markers_withdrawn_when_a_later_check_fails.
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        spec.markers = {TestMarker{}};
        spec.materials = std::vector<MaterialSpec>{{0, 9, 0}};
        expect_mesh_rejected(spec, "exceeds the 6 remaining indices",
                             "material overruns faces with a marker chunk");
    }
    {
        MeshSpec spec = quad_spec();
        spec.materials = std::vector<MaterialSpec>{{0, 3, 0}};
        expect_mesh_rejected(spec, "cover 3 indices; the face count needs 6",
                             "materials do not cover all faces");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_materials = 2;
        expect_mesh_rejected(spec, "2 material records of 60 bytes (120 bytes)",
                             "material count larger than buffer");
    }
    {
        MeshSpec spec = quad_spec();
        spec.declared_materials = 0;
        expect_mesh_rejected(spec, "material count 0", "zero material count");
    }
    {
        MeshSpec spec = quad_spec();
        spec.material_extra = 4;
        expect_mesh_rejected(spec, "1 material records of 60 bytes (60 bytes)",
                             "extra material bytes");
    }
    {
        MeshSpec spec = quad_spec();
        spec.corrupt = Corrupt::material_prefix;
        const MeshLayout layout = build({spec}).meshes[0];
        check(layout.material_prefix != 0U, "material prefix precondition");
        expect_mesh_rejected(
            spec,
            "alignment prefix byte 0 (file offset " +
                std::to_string(layout.material_payload_offset) +
                ") is 0x00, expected 0x11",
            "bad material prefix byte");
    }
}

void test_structural_errors() {
    {
        const nfsu::GeometryReport report = parse(Bytes());
        check(!report.ok() && has_issue(report, "smaller than a chunk header"),
              "empty input");
    }
    {
        const nfsu::GeometryReport report = nfsu::parse_geometry(nullptr, 16);
        check(!report.ok() && has_issue(report, "null input"), "null input");
    }
    {
        Writer w;
        w.leaf(0x80034020U, Bytes(8, 0));
        const nfsu::GeometryReport report = parse(w.bytes());
        check(!report.ok() && has_issue(report, "no geometry root chunk"),
              "missing root");
    }
    {
        // Mesh container claims more bytes than the root holds.
        Fixture fixture = build({quad_spec()});
        put_u32(fixture.bytes, fixture.meshes[0].container_offset + 4,
                0x00100000U);
        const nfsu::GeometryReport report = parse(fixture.bytes);
        check(!report.ok() && has_issue(report, "remain in its parent"),
              "oversized mesh container rejected");
        check(report.valid_mesh_count() == 0U, "no mesh from broken container");
    }
    {
        // Vertex buffer chunk claims more bytes than its geometry container.
        Fixture fixture = build({quad_spec(), triangle_spec()});
        put_u32(fixture.bytes, fixture.meshes[0].vertex_payload_offset - 4,
                0x7FFFFFFFU);
        const nfsu::GeometryReport report = parse(fixture.bytes);
        check(!report.ok() && has_issue(report, "geometry container"),
              "oversized vertex chunk rejected");
        check(report.meshes.size() == 2U && !report.meshes[0].valid &&
                  report.meshes[1].valid,
              "sibling mesh survives broken geometry container");
    }
    {
        // Truncate in the middle of the second mesh.
        Bytes bytes = build({quad_spec(), triangle_spec()}).bytes;
        bytes.resize(bytes.size() - 40);
        const nfsu::GeometryReport report = parse(bytes);
        check(!report.ok() && has_issue(report, "remain in its parent"),
              "truncated file reported");
        check(report.valid_mesh_count() == 0U,
              "no meshes trusted from a truncated root");
    }
    {
        nfsu::GeometryLimits limits;
        limits.max_chunks = 5;
        const Bytes bytes = build({quad_spec()}).bytes;
        const nfsu::GeometryReport report =
            nfsu::parse_geometry(bytes.data(), bytes.size(), limits);
        check(has_issue(report, "chunk limit 5 exceeded"), "chunk limit");
    }
    {
        nfsu::GeometryLimits limits;
        limits.max_file_size = 16;
        const Bytes bytes = build({quad_spec()}).bytes;
        const nfsu::GeometryReport report =
            nfsu::parse_geometry(bytes.data(), bytes.size(), limits);
        check(has_issue(report, "limit is 16"), "memory buffer size limit");
    }
}

void test_exporter_revalidates_indices() {
    nfsu::GeometryReport report = parse(build({quad_spec()}).bytes);
    if (report.meshes.empty() || report.meshes[0].triangles.size() < 2U) {
        check(false, "fixture for exporter validation");
        return;
    }
    report.meshes[0].triangles[1].c = 4;
    std::ostringstream output;
    nfsu::ObjExportStats stats;
    std::string error;
    check(!nfsu::write_obj(output, report, stats, error),
          "exporter rejects out of range index");
    check(error.find("triangle 1") != std::string::npos, "exporter diagnostic");
    check(output.str().empty(), "exporter writes nothing on invalid input");
}

void test_markers() {
    // A mesh with no marker chunk: the common retail case.
    {
        const nfsu::GeometryReport report = parse(build({quad_spec()}).bytes);
        check(report.ok(), "mesh without markers parses");
        if (!report.meshes.empty()) {
            check(!report.meshes[0].has_marker_chunk, "no marker chunk reported");
            check(report.meshes[0].markers.empty(), "no markers decoded");
            check(report.meshes[0].marker_data_offset == 0, "no marker offset");
        }
    }

    // Two markers, the retail shape of 240SX_KIT15_REAR_BUMPER_A.
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        TestMarker exhaust;
        exhaust.name_hash = 0xBCF8A18BU;  // LEFT_EXHAUST
        exhaust.matrix = {{0.0F, 0.0F, 1.0F, 0.0F,
                           0.0F, 1.0F, 0.0F, 0.0F,
                           -1.0F, 0.0F, 0.0F, 0.0F,
                           -1.98311F, 0.48212F, 0.03986F, 1.0F}};
        TestMarker plate;
        plate.name_hash = 0xC805F7E0U;  // REAR_LICENSE_PLATE
        plate.matrix[12] = -2.02408F;
        plate.matrix[13] = 0.00062F;
        plate.matrix[14] = 0.31001F;
        spec.markers = {exhaust, plate};
        const Fixture fixture = build({spec});
        const nfsu::GeometryReport report = parse(fixture.bytes);
        check(report.ok(), "mesh with two markers parses");
        if (report.meshes.empty() || report.meshes[0].markers.size() != 2U) {
            check(false, "two markers decoded");
            return;
        }
        const nfsu::Mesh& mesh = report.meshes[0];
        check(mesh.has_marker_chunk, "marker chunk reported");
        check(mesh.marker_data_offset == fixture.meshes[0].marker_data_offset,
              "marker data offset");
        check(mesh.marker_prefix_bytes == fixture.meshes[0].marker_prefix,
              "marker prefix");
        check(mesh.marker_data_offset % nfsu::kMarkerDataAlignment == 0,
              "marker records are 16-byte aligned");
        check(mesh.markers[0].name_hash == 0xBCF8A18BU, "marker 0 hash");
        check(mesh.markers[1].name_hash == 0xC805F7E0U, "marker 1 hash");
        check(mesh.markers[0].offset == mesh.marker_data_offset, "marker 0 offset");
        check(mesh.markers[1].offset ==
                  mesh.marker_data_offset + nfsu::kMarkerRecordSize,
              "marker 1 offset");
        check(std::fabs(mesh.markers[0].translation(0) + 1.98311F) < 1e-5F,
              "marker 0 translation 0");
        check(std::fabs(mesh.markers[0].translation(1) - 0.48212F) < 1e-5F,
              "marker 0 translation 1");
        check(std::fabs(mesh.markers[0].translation(2) - 0.03986F) < 1e-5F,
              "marker 0 translation 2");
        check(std::fabs(mesh.markers[1].translation(2) - 0.31001F) < 1e-5F,
              "marker 1 translation 2");
    }

    // An empty marker chunk. 647 retail chunks hold no record.
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        const nfsu::GeometryReport report = parse(build({spec}).bytes);
        check(report.ok(), "empty marker chunk parses");
        if (!report.meshes.empty()) {
            check(report.meshes[0].has_marker_chunk,
                  "empty marker chunk is still reported");
            check(report.meshes[0].markers.empty(),
                  "empty marker chunk has no records");
        }
    }

    // A payload that is not a whole number of records.
    for (std::size_t extra : {std::size_t{1}, std::size_t{16},
                              nfsu::kMarkerRecordSize - 1}) {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        spec.markers = {TestMarker{}};
        spec.marker_extra = extra;
        const nfsu::GeometryReport report = parse(build({spec}).bytes);
        check(has_issue(report, "not a multiple of the 80-byte marker record"),
              "marker stride with " + std::to_string(extra) + " extra bytes");
        check(report.valid_mesh_count() == 0U,
              "a bad marker stride skips the mesh");
    }

    // Filler that is not 0x11.
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        spec.markers = {TestMarker{}};
        spec.corrupt_marker_prefix = true;
        const Fixture fixture = build({spec});
        if (fixture.meshes[0].marker_prefix != 0) {
            const nfsu::GeometryReport report = parse(fixture.bytes);
            check(has_issue(report, "alignment prefix byte"),
                  "marker filler byte is checked");
        }
    }

    // Two marker chunks in one mesh.
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        spec.markers = {TestMarker{}};
        spec.duplicate_marker_list = true;
        const nfsu::GeometryReport report = parse(build({spec}).bytes);
        check(has_issue(report, "expected at most one"),
              "a duplicate marker chunk is refused");
    }

    // A matrix element that is not a number.
    for (std::size_t element : {std::size_t{0}, std::size_t{12}, std::size_t{15}}) {
        for (int kind = 0; kind < 2; ++kind) {
            MeshSpec spec = quad_spec();
            spec.include_marker_list = true;
            TestMarker marker;
            marker.matrix[element] = kind == 0
                                         ? std::numeric_limits<float>::quiet_NaN()
                                         : std::numeric_limits<float>::infinity();
            spec.markers = {marker};
            const nfsu::GeometryReport report = parse(build({spec}).bytes);
            check(has_issue(report, "is not finite"),
                  "marker matrix element " + std::to_string(element) +
                      " must be finite");
            check(report.valid_mesh_count() == 0U,
                  "a non-finite marker matrix skips the mesh");
        }
    }

    // The reserved words are kept raw, not required to be zero. Every
    // marker in the 37 retail-original car files has zero here, but all 245
    // markers of the two toolkit-rewritten files do not, and the game loads
    // those files, so the parser must not refuse them.
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        TestMarker marker;
        marker.reserved = {{0x11223344U, 0x55667788U, 0x99AABBCCU}};
        spec.markers = {marker};
        const nfsu::GeometryReport report = parse(build({spec}).bytes);
        check(report.ok(), "non-zero reserved marker words are accepted");
        if (!report.meshes.empty() && report.meshes[0].markers.size() == 1U) {
            check(report.meshes[0].markers[0].reserved[0] == 0x11223344U,
                  "reserved word 0 is kept");
            check(report.meshes[0].markers[0].reserved[2] == 0x99AABBCCU,
                  "reserved word 2 is kept");
        }
    }

    // The count boundary. The game stores the count with `mov byte
    // [ebx+0x1b], dl` and reads it back with `movsx`, so 127 is the largest
    // value it can use; 128 comes back negative.
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        spec.markers.assign(nfsu::kMaxMarkersPerMesh, TestMarker{});
        const nfsu::GeometryReport report = parse(build({spec}).bytes);
        check(report.ok(), "the largest usable marker count parses");
        if (!report.meshes.empty()) {
            check(report.meshes[0].markers.size() == nfsu::kMaxMarkersPerMesh,
                  "all 127 markers are decoded");
        }
    }
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        spec.markers.assign(nfsu::kMaxMarkersPerMesh + 1, TestMarker{});
        const nfsu::GeometryReport report = parse(build({spec}).bytes);
        check(has_issue(report, "are reachable"),
              "one marker past the usable count is refused");
        check(report.valid_mesh_count() == 0U,
              "an unreachable marker count skips the mesh");
    }

    // A marker chunk does not disturb the records that follow it.
    {
        MeshSpec spec = quad_spec();
        spec.include_marker_list = true;
        spec.markers = {TestMarker{}, TestMarker{}, TestMarker{}};
        const Fixture fixture = build({spec});
        const nfsu::GeometryReport report = parse(fixture.bytes);
        check(report.ok(), "three markers plus geometry parse");
        if (!report.meshes.empty()) {
            const nfsu::Mesh& mesh = report.meshes[0];
            check(mesh.vertex_data_offset == fixture.meshes[0].vertex_data_offset,
                  "vertex offset survives a marker chunk");
            check(mesh.face_data_offset == fixture.meshes[0].face_data_offset,
                  "face offset survives a marker chunk");
            check(mesh.vertices.size() == 4U, "vertices still decoded");
        }
    }
}

// A mesh whose marker chunk parses before a later check fails must report no
// markers at all. parse_mesh_contents reads 0x0013401A before the counts,
// the buffers and the materials, so this ordering is reachable; retail
// CIVIC mesh 286 'CIVIC_KIT13_REAR_BUMPER_A' is the case in the shipped
// data, where material validation fails after two markers have parsed and
// the mesh nevertheless reported them.
void test_markers_withdrawn_when_a_later_check_fails() {
    // First establish that this spec's markers really do parse when nothing
    // else is wrong, so the assertions below cannot pass vacuously.
    TestMarker exhaust;
    exhaust.name_hash = 0xBD7CF15EU;  // bin_hash("RIGHT_EXHAUST")
    exhaust.matrix[12] = -2.18176F;
    TestMarker plate;
    plate.name_hash = 0xC805F7E0U;  // bin_hash("REAR_LICENSE_PLATE")
    plate.matrix[12] = -2.09928F;

    MeshSpec good = quad_spec();
    good.include_marker_list = true;
    good.markers = {exhaust, plate};
    const nfsu::GeometryReport baseline = parse(build({good}).bytes);
    check(baseline.ok(), "the marker fixture parses when the mesh is sound");
    check(baseline.meshes.size() == 1U && baseline.meshes[0].valid &&
              baseline.meshes[0].has_marker_chunk &&
              baseline.meshes[0].markers.size() == 2U &&
              baseline.meshes[0].marker_data_offset != 0,
          "the marker fixture decodes two markers");

    // Now break each stage that runs after the markers in turn: the vertex
    // buffer, the face indices and the material records. The first case is
    // the shape retail CIVIC mesh 286 has.
    struct Case {
        MeshSpec spec;
        std::string needle;
        std::string label;
    };
    std::vector<Case> cases;
    {
        MeshSpec spec = good;
        spec.materials = std::vector<MaterialSpec>{{0, 0, 0}, {0, 6, 0}};
        cases.push_back({spec, "index count 0 is zero, not a multiple of 3",
                         "material index count zero after markers"});
    }
    {
        // A different post-marker stage: the vertex buffer.
        MeshSpec spec = good;
        spec.declared_vertices = 8;
        cases.push_back({spec, "vertices of 36 bytes",
                         "short vertex buffer after markers"});
    }
    {
        // And the face indices, later still.
        MeshSpec spec = good;
        spec.indices = {0, 1, 9, 0, 2, 3};
        cases.push_back({spec, "out of range for 4 vertices",
                         "bad face index after markers"});
    }

    for (const Case& item : cases) {
        const nfsu::GeometryReport report =
            parse(build({item.spec, triangle_spec()}).bytes);
        if (!has_issue(report, item.needle) || report.meshes.size() != 2U) {
            std::cerr << "case '" << item.label << "' expected issue containing '"
                      << item.needle << "'\n";
            dump_issues(report);
            check(false, item.label);
            continue;
        }
        const nfsu::Mesh& mesh = report.meshes[0];
        check(!mesh.valid, item.label + ": the mesh is invalid");
        check(mesh.markers.empty(), item.label + ": no markers are reported");
        check(!mesh.has_marker_chunk,
              item.label + ": the marker chunk flag is cleared");
        check(mesh.marker_data_offset == 0,
              item.label + ": the marker data offset is cleared");
        check(mesh.marker_prefix_bytes == 0,
              item.label + ": the marker prefix is cleared");
        // The sound mesh beside it keeps parsing, so this is a per-mesh
        // withdrawal and not a whole-file one.
        check(report.meshes[1].valid, item.label + ": the next mesh still parses");
    }

    // The same rule when the failure is inside the marker chunk itself: the
    // partially decoded records must not survive either.
    {
        MeshSpec spec = good;
        TestMarker broken;
        broken.matrix[3] = std::numeric_limits<float>::quiet_NaN();
        spec.markers = {exhaust, broken};
        const nfsu::GeometryReport report = parse(build({spec}).bytes);
        check(has_issue(report, "is not finite"),
              "a non-finite element in the second marker is reported");
        check(report.meshes.size() == 1U && !report.meshes[0].valid &&
                  report.meshes[0].markers.empty() &&
                  !report.meshes[0].has_marker_chunk &&
                  report.meshes[0].marker_data_offset == 0 &&
                  report.meshes[0].marker_prefix_bytes == 0,
              "a failure inside the marker chunk withdraws the first record too");
    }
}

void test_file_loading() {

    const fs::path path = fs::temp_directory_path() / "nfsu_geometry_test.bin";
    const Bytes bytes = build({quad_spec()}).bytes;
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        check(static_cast<bool>(stream), "write fixture file");
    }
    const nfsu::GeometryReport report = nfsu::load_geometry_file(path);
    check(report.ok() && report.valid_mesh_count() == 1U, "load from file");

    nfsu::GeometryLimits limits;
    limits.max_file_size = 16;
    const nfsu::GeometryReport limited = nfsu::load_geometry_file(path, limits);
    check(has_issue(limited, "limit is 16"), "file size limit");

    std::error_code error;
    fs::remove(path, error);
    const nfsu::GeometryReport missing = nfsu::load_geometry_file(path);
    check(!missing.ok(), "missing file reported");
}

} // namespace

int main() {
    test_valid_file();
    test_mesh_directory();
    test_alignment_sweep();
    test_leading_0x11_data_does_not_shift();
    test_obj_output();
    test_invalid_mesh_is_skipped_and_offsets_stay_correct();
    test_bad_counts();
    test_bad_layout();
    test_bad_contents();
    test_materials();
    test_bad_materials();
    test_structural_errors();
    test_exporter_revalidates_indices();
    test_markers();
    test_markers_withdrawn_when_a_later_check_fails();
    test_file_loading();

    if (g_failures != 0) {
        std::cerr << g_failures << " geometry test failures\n";
        return 1;
    }
    std::cout << "geometry tests passed\n";
    return 0;
}
