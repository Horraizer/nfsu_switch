#include "nfsu/geometry.h"
#include "nfsu/track_probe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

// The track inspection layer: the free camera and its two moves, the
// residency reason, the bounding-tree provenance and the three gate
// verdicts.
//
// The fixtures go through the real parsers over byte images written at
// absolute offsets, exactly as tests/track_render_tests.cpp does, so every
// alignment rule of docs/TRACK.md and docs/TRACK_OBJECTS.md applies and the
// instance matrix, the stored int16 boxes and the material tiling rule are
// exercised rather than assumed.
//
// EVERY VISIBILITY ASSERTION HERE IS DOUBLE-ENTRY. The probe reports its own
// verdict and the one cull_track_region reached for the same frustum, and
// probe_track_scene raises an issue where they differ; the cases below
// assert the verdict AND that the two agreed, so a probe that reproduced the
// traversal wrongly fails whichever of the two it got wrong.

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void check_eq_u64(std::uint64_t got, std::uint64_t want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

void check_eq_text(const std::string& got, const std::string& want,
                   const std::string& what) {
    check(got == want, what + ": got '" + got + "', want '" + want + "'");
}

void check_close(double got, double want, double tolerance,
                 const std::string& what) {
    const double delta = got - want;
    check(delta <= tolerance && -delta <= tolerance,
          what + ": got " + std::to_string(got) + ", want " +
              std::to_string(want) + " +- " + std::to_string(tolerance));
}

void check_exact(double got, double want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

// The same as check, for a call that reports through an out-parameter.
// check(f(..., error), what + ": " + error) reads correctly and reports the
// WRONG string: both arguments are evaluated before check runs, so the
// message carries whatever `error` held BEFORE f set it.
void check_ok(bool ok, const std::string& what, const std::string& error) {
    check(ok, ok ? what : what + ": " + error);
}

void check_fails(bool ok, const std::string& error, const std::string& needle,
                 const std::string& what) {
    if (ok) {
        check(false, what + ": the call succeeded and had to fail");
        return;
    }
    check(error.find(needle) != std::string::npos,
          what + ": the diagnostic is '" + error + "' and must name '" +
              needle + "'");
}

using Bytes = std::vector<unsigned char>;

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_i16(Bytes& bytes, std::int16_t value) {
    append_u16(bytes, static_cast<std::uint16_t>(value));
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
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<unsigned char>((value >> (8 * i)) & 0xFFU);
    }
}

class Writer {
public:
    explicit Writer(std::uint64_t base) : base_(base) {}

    std::uint64_t at() const { return base_ + bytes_.size(); }
    const Bytes& bytes() const { return bytes_; }

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

    void append(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append(payload);
        end();
    }

    void pad_to(std::uint64_t alignment) {
        while (at() % alignment != 0) {
            bytes_.push_back(nfsu::kAlignmentByte);
        }
    }

    void zero_to(std::uint64_t alignment) {
        while (at() % alignment != 0) {
            bytes_.push_back(0U);
        }
    }

private:
    std::uint64_t base_;
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

// ---------------------------------------------------------------------------
// Geometry fixture
// ---------------------------------------------------------------------------

// One vertex in the FILE slot order: src/track.cpp reads the three position
// floats as px, pz, py in that order, so writing them in the struct's
// declaration order would place every vertex in the wrong plane.
struct VertexSpec {
    float px = 0.0F;
    float pz = 0.0F;
    float py = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    unsigned char color[4] = {255, 255, 255, 255};
};

struct MaterialSpec {
    std::uint32_t index_count = 3;
    std::uint32_t texture_slot = 0;
};

struct MeshSpec {
    std::string name = "MESH";
    std::uint32_t hash = 0x3001U;
    std::vector<VertexSpec> vertices;
    std::vector<std::array<std::uint16_t, 3>> triangles;
    std::vector<MaterialSpec> materials{MaterialSpec{}};
    std::vector<std::uint32_t> texture_hashes{0xBBBB0001U};
};

void write_mesh(Writer& writer, const MeshSpec& spec) {
    writer.begin(nfsu::kMeshContainerId);

    writer.begin(nfsu::kMeshMetadataId);
    writer.pad_to(nfsu::kMeshMetadataAlignment);
    Bytes record(nfsu::kMeshMetadataRecordSize, 0);
    put_u32(record, nfsu::kMeshMetadataHashOffset, spec.hash);
    record[nfsu::kMeshMetadataTextureCountOffset] =
        static_cast<unsigned char>(spec.texture_hashes.size());
    record[nfsu::kMeshMetadataKeyCountOffset] = 0;
    for (std::size_t i = 0; i < spec.name.size() && i < nfsu::kMeshNameSize; ++i) {
        record[nfsu::kMeshMetadataNameOffset + i] =
            static_cast<unsigned char>(spec.name[i]);
    }
    writer.append(record);
    writer.end();

    Bytes textures;
    for (const std::uint32_t hash : spec.texture_hashes) {
        append_u32(textures, hash);
        append_u32(textures, 0U);
    }
    writer.leaf(nfsu::kTextureListId, textures);

    writer.begin(nfsu::kGeometryContainerId);

    writer.begin(nfsu::kGeometryMetadataId);
    writer.pad_to(nfsu::kGeometryMetadataAlignment);
    Bytes metadata(nfsu::kGeometryMetadataSize, 0);
    put_u32(metadata, nfsu::kVertexFormatOffset, 0U);
    put_u32(metadata, nfsu::kMaterialCountOffset,
            static_cast<std::uint32_t>(spec.materials.size()));
    put_u32(metadata, nfsu::kFaceCountOffset,
            static_cast<std::uint32_t>(spec.triangles.size()));
    put_u32(metadata, nfsu::kVertexCountOffset,
            static_cast<std::uint32_t>(spec.vertices.size()));
    writer.append(metadata);
    writer.end();

    writer.begin(nfsu::kVertexBufferId);
    writer.pad_to(nfsu::kVertexDataAlignment);
    Bytes vertices;
    for (const VertexSpec& vertex : spec.vertices) {
        append_f32(vertices, vertex.px);
        append_f32(vertices, vertex.pz);
        append_f32(vertices, vertex.py);
        for (int k = 0; k < 4; ++k) {
            vertices.push_back(vertex.color[k]);
        }
        append_f32(vertices, vertex.u);
        append_f32(vertices, vertex.v);
    }
    writer.append(vertices);
    writer.end();

    writer.begin(nfsu::kMaterialBufferId);
    writer.pad_to(nfsu::kMaterialDataAlignment);
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
        append_u32(material_record, 0xFFU);
        material_record.resize(52, 0);
        append_u32(material_record, first_index);
        append_u32(material_record, 0U);
        writer.append(material_record);
        first_index += material.index_count;
    }
    writer.end();

    writer.begin(nfsu::kFaceBufferId);
    writer.pad_to(nfsu::kFaceDataAlignment);
    for (const std::array<std::uint16_t, 3>& triangle : spec.triangles) {
        Bytes indices;
        for (const std::uint16_t index : triangle) {
            append_u16(indices, index);
        }
        writer.append(indices);
    }
    writer.zero_to(nfsu::kFaceTailAlignment);
    writer.end();

    writer.end();
    writer.end();
}

Bytes build_geometry(const std::vector<MeshSpec>& meshes, std::uint64_t base) {
    Writer writer(base);
    writer.begin(nfsu::kGeometryRootId);
    writer.leaf(0x00000000U, Bytes{});
    writer.begin(nfsu::kGeometryHeaderId);
    writer.leaf(0x00134002U, Bytes(128, 0));
    std::vector<std::uint32_t> hashes;
    for (const MeshSpec& mesh : meshes) {
        hashes.push_back(mesh.hash);
    }
    std::sort(hashes.begin(), hashes.end());
    Bytes table;
    for (const std::uint32_t hash : hashes) {
        append_u32(table, hash);
        append_u32(table, 0U);
    }
    writer.leaf(nfsu::kMeshHashTableId, table);
    writer.leaf(0x80134008U, Bytes{});
    writer.end();
    for (const MeshSpec& mesh : meshes) {
        write_mesh(writer, mesh);
    }
    writer.end();
    return writer.bytes();
}

// ---------------------------------------------------------------------------
// Scenery fixture
// ---------------------------------------------------------------------------

struct ObjectSpec {
    std::uint32_t mesh_hash[nfsu::kSceneryObjectMeshSlots] = {0, 0, 0, 0, 0, 0};
    float radius = 5.0F;
    std::uint32_t flags = 0x11223344U;
    std::uint32_t flags2 = 0x55667788U;
};

constexpr std::int16_t kOne = static_cast<std::int16_t>(nfsu::kSceneryMatrixOne);

struct InstanceSpec {
    std::int16_t bounds_min[3] = {-2, -2, -2};
    std::int16_t bounds_max[3] = {2, 2, 2};
    std::uint16_t object_index = 0;
    std::uint16_t flags = 0;
    float position[3] = {0.0F, 0.0F, 0.0F};
    std::int16_t linear[9] = {kOne, 0, 0, 0, kOne, 0, 0, 0, kOne};
};

struct NodeSpec {
    std::int16_t bounds_min[3] = {-2, -2, -2};
    std::int16_t bounds_max[3] = {2, 2, 2};
    std::uint16_t entry_count = 0;
    std::int16_t entries[nfsu::kSceneryBoundNodeEntries] = {0, 0, 0, 0, 0};
};

struct SectionSpec {
    std::uint32_t number = 303;
    std::vector<ObjectSpec> objects;
    std::vector<InstanceSpec> instances;
    std::vector<NodeSpec> nodes;
};

void write_section(Writer& writer, const SectionSpec& spec) {
    writer.begin(nfsu::kScenerySectionId);

    writer.begin(nfsu::kSceneryHeaderId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes header(nfsu::kSceneryHeaderRecordSize, 0U);
    put_u32(header, nfsu::kScenerySectionNumberOffset, spec.number);
    writer.append(header);
    writer.end();

    writer.begin(nfsu::kSceneryInstanceListId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes instances;
    for (const InstanceSpec& instance : spec.instances) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(instances, instance.bounds_min[axis]);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(instances, instance.bounds_max[axis]);
        }
        append_u16(instances, instance.object_index);
        append_u16(instances, instance.flags);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_f32(instances, instance.position[axis]);
        }
        for (std::size_t word = 0; word < 9; ++word) {
            append_i16(instances, instance.linear[word]);
        }
        append_i16(instances, 0);
    }
    writer.append(instances);
    writer.end();

    Bytes objects;
    for (const ObjectSpec& object : spec.objects) {
        const std::size_t start = objects.size();
        for (std::size_t slot = 0; slot < nfsu::kSceneryObjectMeshSlots; ++slot) {
            append_u32(objects, object.mesh_hash[slot]);
        }
        append_u32(objects, object.flags);
        append_u32(objects, object.flags2);
        objects.resize(start + nfsu::kSceneryObjectRadiusOffset, 0U);
        append_f32(objects, object.radius);
        objects.resize(start + nfsu::kSceneryObjectRecordSize, 0U);
    }
    writer.leaf(nfsu::kSceneryObjectListId, objects);

    Bytes tree;
    for (const NodeSpec& node : spec.nodes) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(tree, node.bounds_min[axis]);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(tree, node.bounds_max[axis]);
        }
        append_u16(tree, node.entry_count);
        for (std::size_t slot = 0; slot < nfsu::kSceneryBoundNodeEntries; ++slot) {
            append_i16(tree, node.entries[slot]);
        }
    }
    writer.leaf(nfsu::kSceneryBoundTreeId, tree);

    writer.end();
}

Bytes build_sections(const std::vector<SectionSpec>& sections,
                     std::uint64_t base) {
    Writer writer(base);
    for (const SectionSpec& section : sections) {
        write_section(writer, section);
    }
    return writer.bytes();
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTexA = 0xBBBB0001U;
constexpr std::uint32_t kTexB = 0xBBBB0002U;
// A hash no texture claims, so one material group of POST is unresolved and
// the per-instance binding counts have something to count apart.
constexpr std::uint32_t kTexMissing = 0xBBBB0009U;

nfsu::RenderTexture flat_texture(std::uint32_t hash) {
    nfsu::RenderTexture texture;
    texture.hash = hash;
    texture.claims[0] = hash;
    texture.claim_count = 1;
    texture.width = 2;
    texture.height = 2;
    for (int i = 0; i < 4; ++i) {
        texture.rgba.push_back(200);
        texture.rgba.push_back(100);
        texture.rgba.push_back(50);
        texture.rgba.push_back(255);
    }
    texture.valid = true;
    return texture;
}

nfsu::RenderTextureSet two_textures() {
    nfsu::RenderTextureSet set;
    set.textures.push_back(flat_texture(kTexA));
    set.textures.push_back(flat_texture(kTexB));
    for (const nfsu::RenderTexture& texture : set.textures) {
        set.decoded_bytes += texture.rgba.size();
    }
    return set;
}

// ---------------------------------------------------------------------------
// The fixture
//
//   mesh 0  PANEL  hash 0x3001  one group of 2 triangles, kTexA
//   mesh 1  POST   hash 0x3002  two groups of 1 triangle, kTexA then a hash
//                               no source claims
//
// One section, number 303, three objects and seven instances. The instances
// sit along world x, and the bounding tree has a root with two children so
// that a camera can prune a whole subtree rather than only reject a box:
//
//   node 0  root, entries -1 and -2
//   node 1  instances 0..3, at world x = -20, -9, -6 and -3
//   node 2  instances 4..6, at world x = 20, 25 and 30
//
// Instance 4 names a mesh hash no mesh of the region carries, so it places
// nothing; instance 2 carries flags the mask gate can single out.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kPanel = 0x3001U;
constexpr std::uint32_t kPost = 0x3002U;
constexpr std::uint32_t kMissingMesh = 0x00007777U;
constexpr std::uint64_t kGeometryBase = 0;
constexpr std::uint64_t kSceneryBase = 0x2000;
// (flags ^ kTrackInstanceMaskBias) is zero for this value, so no mask byte
// can reject an instance carrying it.
constexpr std::uint16_t kQuietFlags = 0x0060U;
// The one instance a mask byte can see, at bit 0x01.
constexpr std::uint16_t kMaskedFlags = 0x0061U;
// An instance the BIAS decides: 0x0040 ^ 0x0060 is 0x0020, so a mask of 0x20
// rejects it and would pass it if the bias were dropped - and would reject
// every kQuietFlags instance instead, which no other value here can show.
constexpr std::uint16_t kBiasedFlags = 0x0040U;

VertexSpec plane_vertex(float x, float y, float u, float v) {
    VertexSpec vertex;
    vertex.px = x;
    vertex.pz = 0.0F;
    vertex.py = y;
    vertex.u = u;
    vertex.v = v;
    return vertex;
}

std::vector<MeshSpec> fixture_meshes() {
    MeshSpec panel;
    panel.name = "PANEL";
    panel.hash = kPanel;
    panel.vertices = {plane_vertex(-1.0F, -1.0F, 0.0F, 0.0F),
                      plane_vertex(1.0F, -1.0F, 1.0F, 0.0F),
                      plane_vertex(1.0F, 1.0F, 1.0F, 1.0F),
                      plane_vertex(-1.0F, 1.0F, 0.0F, 1.0F)};
    panel.triangles = {{0, 1, 2}, {0, 2, 3}};
    panel.materials = {MaterialSpec{6, 0}};
    panel.texture_hashes = {kTexA};

    MeshSpec post;
    post.name = "POST";
    post.hash = kPost;
    post.vertices = {plane_vertex(-0.5F, -1.0F, 0.0F, 0.0F),
                     plane_vertex(0.5F, -1.0F, 1.0F, 0.0F),
                     plane_vertex(0.5F, 1.0F, 1.0F, 1.0F),
                     plane_vertex(-0.5F, 1.0F, 0.0F, 1.0F)};
    post.triangles = {{0, 1, 2}, {0, 2, 3}};
    post.materials = {MaterialSpec{3, 0}, MaterialSpec{3, 1}};
    post.texture_hashes = {kTexA, kTexMissing};

    return {panel, post};
}

InstanceSpec placed(std::uint16_t object_index, float x, std::uint16_t flags) {
    InstanceSpec instance;
    instance.object_index = object_index;
    // The raw slot order is (px, pz, py), so this puts the instance at world
    // (x, 0, 0): world x = px, y = py, z = -pz.
    instance.position[0] = x;
    instance.position[1] = 0.0F;
    instance.position[2] = 0.0F;
    instance.bounds_min[0] = static_cast<std::int16_t>(x - 2.0F);
    instance.bounds_max[0] = static_cast<std::int16_t>(x + 2.0F);
    // ASYMMETRIC on slot 1, deliberately. The world map negates that slot and
    // exchanges the ends, so a box of -2 .. 2 there is its own image and a
    // stored-box conversion that forgot the negation would look right. This
    // one is -1 .. 3, whose world z interval is -3 .. 1.
    instance.bounds_min[1] = -1;
    instance.bounds_max[1] = 3;
    instance.bounds_min[2] = -2;
    instance.bounds_max[2] = 2;
    instance.flags = flags;
    return instance;
}

NodeSpec node_over(std::int16_t low, std::int16_t high) {
    NodeSpec node;
    node.bounds_min[0] = low;
    node.bounds_max[0] = high;
    node.bounds_min[1] = -1;
    node.bounds_max[1] = 3;
    node.bounds_min[2] = -2;
    node.bounds_max[2] = 2;
    return node;
}

std::vector<SectionSpec> fixture_sections(std::uint32_t number) {
    SectionSpec section;
    section.number = number;
    section.objects.resize(3);
    section.objects[0].mesh_hash[0] = kPanel;
    section.objects[0].radius = 5.0F;
    section.objects[1].mesh_hash[0] = kPost;
    section.objects[1].radius = 7.0F;
    section.objects[2].mesh_hash[0] = kMissingMesh;

    section.instances.push_back(placed(0, -20.0F, kQuietFlags));
    section.instances.push_back(placed(0, -9.0F, kQuietFlags));
    section.instances.push_back(placed(1, -6.0F, kMaskedFlags));
    section.instances.push_back(placed(0, -3.0F, kBiasedFlags));
    section.instances.push_back(placed(2, 20.0F, kQuietFlags));
    section.instances.push_back(placed(0, 25.0F, kQuietFlags));
    section.instances.push_back(placed(1, 30.0F, kQuietFlags));

    NodeSpec root = node_over(-22, 32);
    root.entry_count = 2;
    root.entries[0] = -1;
    root.entries[1] = -2;
    NodeSpec left = node_over(-22, -1);
    left.entry_count = 4;
    for (int k = 0; k < 4; ++k) {
        left.entries[k] = static_cast<std::int16_t>(k);
    }
    NodeSpec right = node_over(18, 32);
    right.entry_count = 3;
    for (int k = 0; k < 3; ++k) {
        right.entries[k] = static_cast<std::int16_t>(4 + k);
    }
    section.nodes.push_back(root);
    section.nodes.push_back(left);
    section.nodes.push_back(right);
    return {section};
}

// A second section, so that a region can hold more than one and the
// per-section instance table is exercised with a non-zero base. With one
// section every base is zero and a table that always answered zero would be
// right by accident.
//
// Its tree is THREE levels rather than two, and every instance of it sits
// out on the right where the street camera's frustum cannot reach:
//
//   node 0  root, one entry: child node 1
//   node 1  two entries: child node 2, and instance 0
//   node 2  five entries: instances 1 to 5
//
// Three things come out of that shape and out of nothing else here. Node 1
// tests outside, so the prune root has to be carried DOWN a level to node
// 2's five instances rather than stopping where it was found. The root has
// exactly one entry and node 2 exactly five, which are the two ends of the
// range a node's entry count may take. And the second section's six
// instances sit at non-zero positions in the per-section table.
std::vector<SectionSpec> two_sections(std::uint32_t first,
                                      std::uint32_t second) {
    std::vector<SectionSpec> sections = fixture_sections(first);
    SectionSpec other;
    other.number = second;
    other.objects.resize(2);
    other.objects[0].mesh_hash[0] = kPost;
    other.objects[0].radius = 3.0F;
    other.objects[1].mesh_hash[0] = kMissingMesh;
    other.instances.push_back(placed(0, 20.0F, kQuietFlags));
    for (int k = 0; k < 4; ++k) {
        other.instances.push_back(
            placed(0, 25.0F + static_cast<float>(k), kQuietFlags));
    }
    other.instances.push_back(placed(1, 30.0F, kQuietFlags));

    // The root reaches back across the whole region, so the FRUSTUM crosses
    // it and the traversal has to descend before it can prune. A root that
    // was itself outside would prune at depth 0 and never exercise a carry
    // from a node the walk actually reached. A stored box larger than what
    // is below it is what the parser accepts; only a smaller one is refused.
    NodeSpec root = node_over(-22, 34);
    root.entry_count = 1;
    root.entries[0] = -1;
    NodeSpec middle = node_over(18, 34);
    middle.entry_count = 2;
    middle.entries[0] = -2;
    middle.entries[1] = 0;
    NodeSpec leaf = node_over(23, 34);
    leaf.entry_count = 5;
    for (int k = 0; k < 5; ++k) {
        leaf.entries[k] = static_cast<std::int16_t>(1 + k);
    }
    other.nodes.push_back(root);
    other.nodes.push_back(middle);
    other.nodes.push_back(leaf);
    sections.push_back(other);
    return sections;
}

// The same two, with the second one's root box shrunk so that it no longer
// contains what is below it. src/track_objects.cpp refuses that at parse
// time, so the section is kept with valid == false and carries no decoded
// record at all.
std::vector<SectionSpec> one_broken_section() {
    std::vector<SectionSpec> sections = two_sections(303, 310);
    sections[1].nodes[0].bounds_min[0] = 0;
    sections[1].nodes[0].bounds_max[0] = 1;
    return sections;
}

struct Fixture {
    Bytes geometry_bytes;
    Bytes scenery_bytes;
    nfsu::TrackRegionReport geometry;
    nfsu::TrackObjectReport objects;
    nfsu::RenderTextureSet textures;
    nfsu::PreparedTrackScene scene;
};

Fixture make_fixture(const std::vector<MeshSpec>& meshes,
                     const std::vector<SectionSpec>& sections) {
    Fixture fixture;
    fixture.geometry_bytes = build_geometry(meshes, kGeometryBase);
    fixture.scenery_bytes = build_sections(sections, kSceneryBase);
    fixture.geometry = nfsu::parse_track_region(fixture.geometry_bytes.data(),
                                                fixture.geometry_bytes.size(),
                                                kGeometryBase);
    fixture.objects = nfsu::parse_track_objects(fixture.scenery_bytes.data(),
                                                fixture.scenery_bytes.size(),
                                                kSceneryBase);
    fixture.textures = two_textures();
    std::string error;
    if (!nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                 fixture.textures, nfsu::TrackSceneOptions{},
                                 fixture.scene, error)) {
        check(false, "the fixture scene builds: " + error);
    }
    return fixture;
}

Fixture town(std::uint32_t number = 303) {
    return make_fixture(fixture_meshes(), fixture_sections(number));
}

// ---------------------------------------------------------------------------
// Running a probe
// ---------------------------------------------------------------------------

nfsu::TrackFreeCamera street_camera() {
    nfsu::TrackFreeCamera camera;
    camera.eye = nfsu::Vec3{-6.0, 0.0, 30.0};
    camera.target = nfsu::Vec3{-6.0, 0.0, 0.0};
    camera.up = nfsu::Vec3{0.0, 1.0, 0.0};
    // Narrow, and square, so that the right-hand subtree is entirely outside
    // and the leftmost instance's own box is outside while its node is not.
    camera.fov_y_deg = 10.0;
    camera.width = 100;
    camera.height = 100;
    camera.near_plane = 1.0;
    camera.far_plane = 100.0;
    return camera;
}

struct Run {
    nfsu::TrackCamera cull_camera;
    nfsu::TrackFrustum frustum;
    nfsu::TrackCullReport cull;
    nfsu::TrackProbeReport probe;
    bool ok = false;
    std::string error;
};

Run probe_run(const Fixture& fixture,
              const nfsu::TrackFreeCamera& camera,
              const nfsu::TrackCullOptions& cull_options,
              const nfsu::TrackProbeSelection& select =
                  nfsu::TrackProbeSelection{}) {
    Run run;
    if (!nfsu::track_cull_camera_of(camera, run.cull_camera, run.error)) {
        return run;
    }
    if (!nfsu::track_frustum_look_at(run.cull_camera, run.frustum)) {
        run.error = "the fixture camera builds no frustum";
        return run;
    }
    run.cull = nfsu::cull_track_region(fixture.objects, run.frustum,
                                       cull_options);
    nfsu::TrackProbeOptions options;
    options.cull = cull_options;
    options.select = select;
    run.ok = nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                     fixture.scene, run.cull, run.frustum,
                                     options, run.probe, run.error);
    return run;
}

const nfsu::TrackInstanceProbe* find_instance(
    const nfsu::TrackProbeReport& report, std::size_t instance_index) {
    for (const nfsu::TrackInstanceProbe& probe : report.instances) {
        if (probe.instance_index == instance_index) {
            return &probe;
        }
    }
    return nullptr;
}

// Every visible instance the culler named, as a sorted list, so a probe's
// verdicts can be compared against the whole set rather than one at a time.
std::vector<std::size_t> culler_visible_set(const nfsu::TrackCullReport& cull) {
    std::vector<std::size_t> out;
    for (const nfsu::TrackVisibleInstance& item : cull.visible) {
        out.push_back(item.instance_index);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::size_t> probe_visible_set(const nfsu::TrackProbeReport& report) {
    std::vector<std::size_t> out;
    for (const nfsu::TrackInstanceProbe& probe : report.instances) {
        if (probe.visible) {
            out.push_back(probe.instance_index);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string join(const std::vector<std::size_t>& values) {
    std::string out;
    for (const std::size_t value : values) {
        if (!out.empty()) {
            out += ',';
        }
        out += std::to_string(value);
    }
    return out.empty() ? std::string("(none)") : out;
}

// ---------------------------------------------------------------------------
// The free camera
// ---------------------------------------------------------------------------

void test_camera_validation() {
    std::string error;
    const nfsu::TrackFreeCamera base = street_camera();
    check_ok(nfsu::validate_track_free_camera(base, error),
             "the street camera validates", error);

    // Each refusal names its own field, so a case that only required a
    // refusal could not tell one bound from another.
    struct Case {
        const char* what;
        const char* needle;
        nfsu::TrackFreeCamera camera;
    };
    std::vector<Case> cases;
    const double nan_value = std::nan("");
    const double inf_value = std::numeric_limits<double>::infinity();

    nfsu::TrackFreeCamera bad = base;
    bad.eye.y = nan_value;
    cases.push_back({"a NaN eye", "eye is not finite", bad});
    bad = base;
    bad.eye.z = inf_value;
    cases.push_back({"an infinite eye", "eye is not finite", bad});
    bad = base;
    bad.target.x = nan_value;
    cases.push_back({"a NaN target", "target is not finite", bad});
    bad = base;
    bad.up.z = nan_value;
    cases.push_back({"a NaN up", "up vector is not finite", bad});
    bad = base;
    bad.eye.x = nfsu::kMaxTargetMagnitude + 1.0;
    bad.target.x = nfsu::kMaxTargetMagnitude + 1.0;
    cases.push_back({"an eye past the range", "eye is at", bad});
    bad = base;
    bad.target.y = -nfsu::kMaxTargetMagnitude - 1.0;
    cases.push_back({"a target past the range", "target is at", bad});
    bad = base;
    bad.fov_y_deg = nfsu::kMinFov - 0.001;
    cases.push_back({"a field of view below the range", "field of view", bad});
    bad = base;
    bad.fov_y_deg = nfsu::kMaxFov + 0.001;
    cases.push_back({"a field of view above the range", "field of view", bad});
    bad = base;
    bad.near_plane = nfsu::kMaxNear + 0.001;
    cases.push_back({"a near plane above the range", "near plane", bad});
    bad = base;
    bad.near_plane = 0.0;
    cases.push_back({"a zero near plane", "near plane", bad});
    bad = base;
    bad.far_plane = bad.near_plane;
    cases.push_back({"a far plane at the near plane", "far plane", bad});
    bad = base;
    bad.far_plane = nfsu::kMaxFar + 1.0;
    cases.push_back({"a far plane past the limit", "far plane", bad});
    bad = base;
    bad.width = 0;
    cases.push_back({"a zero width", "frame size", bad});
    bad = base;
    bad.height = nfsu::kMaxTrackProbeFrameSize + 1;
    cases.push_back({"a height past the limit", "frame size", bad});
    bad = base;
    bad.target = bad.eye;
    cases.push_back({"an eye at its target", "eye-to-target distance", bad});
    bad = base;
    bad.target = nfsu::Vec3{bad.eye.x, bad.eye.y, bad.eye.z + 0.001};
    cases.push_back({"a distance below the minimum", "eye-to-target distance",
                     bad});

    for (const Case& item : cases) {
        std::string message;
        const bool ok = nfsu::validate_track_free_camera(item.camera, message);
        check_fails(ok, message, item.needle,
                    std::string("validate refuses ") + item.what);
    }

    // The boundaries themselves are accepted, so a `<` that became `<=` is
    // caught rather than only its far side.
    nfsu::TrackFreeCamera edge = base;
    edge.fov_y_deg = nfsu::kMinFov;
    check_ok(nfsu::validate_track_free_camera(edge, error),
             "the minimum field of view is accepted", error);
    edge.fov_y_deg = nfsu::kMaxFov;
    check_ok(nfsu::validate_track_free_camera(edge, error),
             "the maximum field of view is accepted", error);
    edge = base;
    edge.near_plane = nfsu::kMinNear;
    check_ok(nfsu::validate_track_free_camera(edge, error),
             "the minimum near plane is accepted", error);
    edge.near_plane = nfsu::kMaxNear;
    check_ok(nfsu::validate_track_free_camera(edge, error),
             "the maximum near plane is accepted", error);
    edge = base;
    edge.width = nfsu::kMaxTrackProbeFrameSize;
    edge.height = 1;
    check_ok(nfsu::validate_track_free_camera(edge, error),
             "the extreme frame sizes are accepted", error);
}

void test_camera_basis() {
    std::string error;
    nfsu::TrackCameraBasis basis;
    const nfsu::TrackFreeCamera camera = street_camera();
    check_ok(nfsu::track_camera_basis(camera, basis, error),
             "the street camera has a basis", error);

    // Looking down -z from +z, with +y up: forward is -z and right is +x,
    // because right is forward x up and (0,0,-1) x (0,1,0) is (1,0,0).
    check_exact(basis.forward.x, 0.0, "forward x");
    check_exact(basis.forward.y, 0.0, "forward y");
    check_exact(basis.forward.z, -1.0, "forward z");
    check_exact(basis.right.x, 1.0, "right x");
    check_exact(basis.right.z, 0.0, "right z");
    check_exact(basis.up_axis.y, 1.0, "up axis y");
    check_exact(basis.distance, 30.0, "eye-to-target distance");

    // Orthonormal, for a camera whose axes are not the world's.
    nfsu::TrackFreeCamera tilted = camera;
    tilted.eye = nfsu::Vec3{10.0, 7.0, -3.0};
    tilted.target = nfsu::Vec3{-2.0, 1.0, 4.0};
    tilted.up = nfsu::Vec3{0.1, 1.0, -0.2};
    check_ok(nfsu::track_camera_basis(tilted, basis, error),
             "a tilted camera has a basis", error);
    const double lengths[3] = {
        basis.forward.x * basis.forward.x + basis.forward.y * basis.forward.y +
            basis.forward.z * basis.forward.z,
        basis.right.x * basis.right.x + basis.right.y * basis.right.y +
            basis.right.z * basis.right.z,
        basis.up_axis.x * basis.up_axis.x + basis.up_axis.y * basis.up_axis.y +
            basis.up_axis.z * basis.up_axis.z};
    for (int k = 0; k < 3; ++k) {
        check_close(lengths[k], 1.0, 1e-12, "basis axis is unit length");
    }
    check_close(basis.forward.x * basis.right.x +
                    basis.forward.y * basis.right.y +
                    basis.forward.z * basis.right.z,
                0.0, 1e-12, "forward and right are perpendicular");
    check_close(basis.forward.x * basis.up_axis.x +
                    basis.forward.y * basis.up_axis.y +
                    basis.forward.z * basis.up_axis.z,
                0.0, 1e-12, "forward and up axis are perpendicular");
    check_close(basis.right.x * basis.up_axis.x +
                    basis.right.y * basis.up_axis.y +
                    basis.right.z * basis.up_axis.z,
                0.0, 1e-12, "right and up axis are perpendicular");

    // The two degenerate cases a basis has to refuse rather than invent.
    nfsu::TrackFreeCamera parallel = camera;
    parallel.up = nfsu::Vec3{0.0, 0.0, -1.0};
    std::string message;
    check_fails(nfsu::track_camera_basis(parallel, basis, message), message,
                "parallel to the view direction",
                "an up vector along the view is refused");
    nfsu::TrackFreeCamera zero_up = camera;
    zero_up.up = nfsu::Vec3{0.0, 0.0, 0.0};
    check_fails(nfsu::track_camera_basis(zero_up, basis, message), message,
                "parallel to the view direction", "a zero up vector is refused");
}

// The default camera is the framing docs/TRACK_RENDER.md fits, read out as an
// eye and a target. That is asserted against frame_track_bounds itself rather
// than restated, because restating it is exactly what this function must not
// do.
void test_default_camera_is_the_framing() {
    nfsu::TrackWorldBounds bounds;
    bounds.valid = true;
    bounds.min[0] = 436.0;
    bounds.max[0] = 865.0;
    bounds.min[1] = -24.0;
    bounds.max[1] = 137.0;
    bounds.min[2] = -209.0;
    bounds.max[2] = -69.0;
    bounds.points = 1000;

    nfsu::TrackFramingOptions framing;
    std::string error;
    nfsu::OrbitCamera orbit;
    check_ok(nfsu::frame_track_bounds(bounds, 1280, 720, framing, orbit, error),
             "the reference framing succeeds", error);
    const nfsu::Vec3 orbit_position = nfsu::orbit_eye(orbit);

    nfsu::TrackFreeCamera camera;
    check_ok(nfsu::track_frame_free_camera(bounds, framing, 1280, 720, camera,
                                           error),
             "the free camera frames the same bounds", error);
    check_exact(camera.eye.x, orbit_position.x, "framed eye x");
    check_exact(camera.eye.y, orbit_position.y, "framed eye y");
    check_exact(camera.eye.z, orbit_position.z, "framed eye z");
    check_exact(camera.target.x, orbit.target.x, "framed target x");
    check_exact(camera.target.y, orbit.target.y, "framed target y");
    check_exact(camera.target.z, orbit.target.z, "framed target z");
    check_exact(camera.fov_y_deg, orbit.fov_y_deg, "framed field of view");
    check_exact(camera.near_plane, orbit.near_plane, "framed near plane");
    check_exact(camera.far_plane, orbit.far_plane, "framed far plane");
    check_exact(camera.up.x, 0.0, "framed up x");
    check_exact(camera.up.y, 1.0, "framed up y");
    check_exact(camera.up.z, 0.0, "framed up z");
    check_eq_u64(camera.width, 1280, "framed width");
    check_eq_u64(camera.height, 720, "framed height");

    // The property the sphere fit was chosen for: the distance does not
    // depend on the view direction, so two runs at different angles are
    // comparable.
    //
    // It is asserted twice, and the difference between the two matters. The
    // FITTED distance, which is the rule, is bit-identical at every angle.
    // The distance READ BACK out of the eye and the target is not quite: an
    // eye is target + distance * (cos p cos y, sin p, cos p sin y), and the
    // length of that unit triple is 1 only in exact arithmetic, so recovering
    // it through det_sin_deg and det_cos_deg costs a few ulp. Requiring bit
    // identity there would be requiring a property the representation does
    // not have.
    nfsu::TrackCameraBasis basis;
    check_ok(nfsu::track_camera_basis(camera, basis, error),
             "the framed camera has a basis", error);
    const double reference_distance = basis.distance;
    const double fitted_distance = orbit.distance;
    const double yaws[6] = {-175.0, -40.0, 0.0, 35.0, 91.0, 180.0};
    const double pitches[5] = {-80.0, -30.0, 0.0, 30.0, 85.0};
    for (const double yaw : yaws) {
        for (const double pitch : pitches) {
            nfsu::TrackFramingOptions angled = framing;
            angled.yaw_deg = yaw;
            angled.pitch_deg = pitch;
            nfsu::TrackFreeCamera turned;
            const bool ok = nfsu::track_frame_free_camera(bounds, angled, 1280,
                                                          720, turned, error);
            check_ok(ok, "framing at yaw " + std::to_string(yaw) + " pitch " +
                             std::to_string(pitch),
                     error);
            if (!ok) {
                continue;
            }
            check_exact(turned.target.x, camera.target.x,
                        "the framed target does not move with the angle");
            nfsu::OrbitCamera angled_orbit;
            if (nfsu::frame_track_bounds(bounds, 1280, 720, angled,
                                         angled_orbit, error)) {
                check_exact(angled_orbit.distance, fitted_distance,
                            "the FITTED distance is the same at every angle");
            } else {
                check(false, "the angled framing succeeds: " + error);
            }
            nfsu::TrackCameraBasis angled_basis;
            if (nfsu::track_camera_basis(turned, angled_basis, error)) {
                check_close(angled_basis.distance, reference_distance, 1e-9,
                            "and the distance read back out of the eye is the "
                            "same to within the read-back's own rounding");
            } else {
                check(false, "the angled camera has a basis: " + error);
            }

            // And the box it was fitted to is inside the frustum it builds, on
            // every side, which is what "contains the region" means.
            nfsu::TrackCamera cull_camera;
            nfsu::TrackFrustum frustum;
            if (!nfsu::track_cull_camera_of(turned, cull_camera, error) ||
                !nfsu::track_frustum_look_at(cull_camera, frustum)) {
                check(false, "the angled camera builds a frustum: " + error);
                continue;
            }
            // The bounds are in the world frame and the frustum is in the raw
            // slot order, so the box has to be mapped back before it is
            // classified: world (x, y, z) is raw (x, -z, y).
            const float world_min[3] = {static_cast<float>(bounds.min[0]),
                                        static_cast<float>(bounds.min[1]),
                                        static_cast<float>(bounds.min[2])};
            const float world_max[3] = {static_cast<float>(bounds.max[0]),
                                        static_cast<float>(bounds.max[1]),
                                        static_cast<float>(bounds.max[2])};
            float raw_a[3];
            float raw_b[3];
            nfsu::track_raw_from_world(world_min, raw_a);
            nfsu::track_raw_from_world(world_max, raw_b);
            float raw_min[3];
            float raw_max[3];
            for (int k = 0; k < 3; ++k) {
                raw_min[k] = std::min(raw_a[k], raw_b[k]);
                raw_max[k] = std::max(raw_a[k], raw_b[k]);
            }
            const nfsu::TrackBoxClass code =
                nfsu::track_classify_box(frustum, raw_min, raw_max);
            check(code == nfsu::TrackBoxClass::inside,
                  std::string("the framed box is inside the frustum at yaw ") +
                      std::to_string(yaw) + " pitch " + std::to_string(pitch) +
                      ": got " + nfsu::track_box_class_name(code));
        }
    }

    // The refusals the framing forwards, and the one it adds.
    nfsu::TrackWorldBounds empty;
    std::string message;
    check_fails(nfsu::track_frame_free_camera(empty, framing, 1280, 720, camera,
                                              message),
                message, "no point", "framing empty bounds is refused");
    check_fails(nfsu::track_frame_free_camera(bounds, framing, 0, 720, camera,
                                              message),
                message, "frame size", "a zero width is refused");
    check_fails(nfsu::track_frame_free_camera(
                    bounds, framing, nfsu::kMaxTrackProbeFrameSize + 1, 720,
                    camera, message),
                message, "frame size", "a width past the limit is refused");
}

void test_camera_moves() {
    std::string error;
    const nfsu::TrackFreeCamera base = street_camera();
    nfsu::TrackFreeCamera moved;

    // No move at all leaves the camera exactly as it was.
    check_ok(nfsu::track_move_free_camera(base, nfsu::TrackCameraMoves{}, moved,
                                          error),
             "an empty move succeeds", error);
    check_exact(moved.eye.z, base.eye.z, "an empty move keeps the eye");
    check_exact(moved.target.z, base.target.z, "an empty move keeps the target");

    // Positive yaw turns the view towards the camera's own right. Looking
    // down -z with +y up, right is -x, so a quarter turn puts forward on -x.
    nfsu::TrackCameraMoves turn;
    turn.has_turn = true;
    turn.turn_yaw_deg = 90.0;
    check_ok(nfsu::track_move_free_camera(base, turn, moved, error),
             "a quarter turn succeeds", error);
    check_exact(moved.eye.x, base.eye.x, "a turn keeps the eye x");
    check_exact(moved.eye.y, base.eye.y, "a turn keeps the eye y");
    check_exact(moved.eye.z, base.eye.z, "a turn keeps the eye z");
    nfsu::TrackCameraBasis basis;
    check_ok(nfsu::track_camera_basis(moved, basis, error),
             "the turned camera has a basis", error);
    check_close(basis.forward.x, 1.0, 1e-12, "a right turn looks along +x");
    check_close(basis.forward.z, 0.0, 1e-12, "a right turn leaves z");
    check_close(basis.distance, 30.0, 1e-9, "a turn keeps the distance");

    // Positive pitch raises the view.
    turn.turn_yaw_deg = 0.0;
    turn.turn_pitch_deg = 30.0;
    check_ok(nfsu::track_move_free_camera(base, turn, moved, error),
             "a pitch up succeeds", error);
    check_ok(nfsu::track_camera_basis(moved, basis, error),
             "the pitched camera has a basis", error);
    check_close(basis.forward.y, 0.5, 1e-12, "a 30 degree pitch raises by sin");
    check_close(basis.forward.z, -std::sqrt(3.0) / 2.0, 1e-12,
                "a 30 degree pitch keeps cos along the view");

    // A whole revolution comes back to where it started.
    turn.turn_yaw_deg = 360.0;
    turn.turn_pitch_deg = 0.0;
    check_ok(nfsu::track_move_free_camera(base, turn, moved, error),
             "a full revolution succeeds", error);
    check_close(moved.target.x, base.target.x, 1e-9,
                "a full revolution returns the target x");
    check_close(moved.target.z, base.target.z, 1e-9,
                "a full revolution returns the target z");

    // A fly moves the eye and the target by the same vector and leaves the
    // direction alone.
    nfsu::TrackCameraMoves fly;
    fly.has_fly = true;
    fly.fly_forward = 5.0;
    fly.fly_right = 2.0;
    fly.fly_up = 1.0;
    check_ok(nfsu::track_move_free_camera(base, fly, moved, error),
             "a fly succeeds", error);
    check_close(moved.eye.z, 25.0, 1e-12, "flying forward moves along -z");
    check_close(moved.eye.x, -4.0, 1e-12, "flying right moves along +x");
    check_close(moved.eye.y, 1.0, 1e-12, "flying up moves along +y");
    check_close(moved.target.z, -5.0, 1e-12, "the target flies with the eye");
    check_close(moved.target.x, -4.0, 1e-12, "the target flies with the eye x");

    // TURN BEFORE FLY, and the order is observable. Turning a quarter and
    // then flying forward moves along +x; flying first and then turning
    // leaves the eye where the fly put it, on -z.
    nfsu::TrackCameraMoves both;
    both.has_turn = true;
    both.turn_yaw_deg = 90.0;
    both.has_fly = true;
    both.fly_forward = 5.0;
    check_ok(nfsu::track_move_free_camera(base, both, moved, error),
             "a turn and a fly succeed", error);
    check_close(moved.eye.x, -1.0, 1e-9, "the fly follows the turn on x");
    check_close(moved.eye.z, 30.0, 1e-9, "the fly follows the turn on z");
    nfsu::TrackCameraMoves only_fly;
    only_fly.has_fly = true;
    only_fly.fly_forward = 5.0;
    nfsu::TrackCameraMoves only_turn;
    only_turn.has_turn = true;
    only_turn.turn_yaw_deg = 90.0;
    nfsu::TrackFreeCamera flown;
    nfsu::TrackFreeCamera then_turned;
    check_ok(nfsu::track_move_free_camera(base, only_fly, flown, error),
             "the fly of the other order succeeds", error);
    check_ok(nfsu::track_move_free_camera(flown, only_turn, then_turned, error),
             "the turn of the other order succeeds", error);
    check(then_turned.eye.z != moved.eye.z,
          "flying before turning reaches a different eye, so the stated order "
          "is load bearing");

    // The refusals.
    std::string message;
    nfsu::TrackCameraMoves bad;
    bad.has_turn = true;
    bad.turn_yaw_deg = std::nan("");
    check_fails(nfsu::track_move_free_camera(base, bad, moved, message), message,
                "turn needs a yaw", "a NaN turn is refused");
    bad.turn_yaw_deg = nfsu::kMaxTrackTurnDegrees + 1.0;
    check_fails(nfsu::track_move_free_camera(base, bad, moved, message), message,
                "turn needs a yaw", "a turn past the limit is refused");
    // Both signs, because the bound is two comparisons and one of them can go
    // while the other stands.
    bad.turn_yaw_deg = -nfsu::kMaxTrackTurnDegrees - 1.0;
    check_fails(nfsu::track_move_free_camera(base, bad, moved, message), message,
                "turn needs a yaw", "a turn past the negative limit is refused");
    bad.turn_yaw_deg = nfsu::kMaxTrackTurnDegrees;
    check_ok(nfsu::track_move_free_camera(base, bad, moved, message),
             "and the limit itself is accepted", message);
    bad.turn_yaw_deg = 0.0;
    bad.turn_pitch_deg = -nfsu::kMaxTrackTurnDegrees - 1.0;
    check_fails(nfsu::track_move_free_camera(base, bad, moved, message), message,
                "turn needs a yaw", "a pitch past the limit is refused");
    bad = nfsu::TrackCameraMoves{};
    bad.has_fly = true;
    bad.fly_right = std::numeric_limits<double>::infinity();
    check_fails(nfsu::track_move_free_camera(base, bad, moved, message), message,
                "fly needs three distances", "an infinite fly is refused");
    bad.fly_right = nfsu::kMaxTrackFlyDistance + 1.0;
    check_fails(nfsu::track_move_free_camera(base, bad, moved, message), message,
                "fly needs three distances", "a fly past the limit is refused");

    // Pitching onto the up axis leaves a camera with no basis, and that is
    // refused rather than answered with an invented right vector.
    nfsu::TrackCameraMoves pole;
    pole.has_turn = true;
    pole.turn_pitch_deg = 90.0;
    check_fails(nfsu::track_move_free_camera(base, pole, moved, message), message,
                "no basis", "pitching onto the up axis is refused");

    // A fly far enough to leave the camera range is refused by the same
    // bounds validate_track_free_camera enforces, not silently clamped.
    nfsu::TrackCameraMoves far_away;
    far_away.has_fly = true;
    far_away.fly_up = nfsu::kMaxTargetMagnitude + 1.0;
    check_fails(nfsu::track_move_free_camera(base, far_away, moved, message),
                message, "flown camera is invalid",
                "a fly out of range is refused");

    // An invalid camera is refused before any move is read, INCLUDING when
    // there is no move at all. With a move the result is revalidated and the
    // refusal arrives from there under another name, so an empty move is the
    // only case where the entry check is the one thing standing.
    nfsu::TrackFreeCamera broken = base;
    broken.fov_y_deg = 0.0;
    check_fails(nfsu::track_move_free_camera(broken, fly, moved, message),
                message, "field of view",
                "a move on an invalid camera is refused");
    check_fails(nfsu::track_move_free_camera(broken, nfsu::TrackCameraMoves{},
                                             moved, message),
                message, "field of view",
                "an empty move on an invalid camera is refused too");

    // A TURN WHOSE AXIS IS NOT PERPENDICULAR TO THE VIEW. Every case above
    // looks along a direction square to the reference up, where the axis
    // term of the rotation is zero and dropping it changes nothing. Here the
    // camera looks upward, so that term carries the answer.
    nfsu::TrackFreeCamera slanted;
    slanted.eye = nfsu::Vec3{0.0, 0.0, 10.0};
    slanted.target = nfsu::Vec3{0.0, 10.0, 0.0};
    slanted.up = nfsu::Vec3{0.0, 1.0, 0.0};
    slanted.near_plane = 1.0;
    slanted.far_plane = 100.0;
    nfsu::TrackCameraMoves quarter;
    quarter.has_turn = true;
    quarter.turn_yaw_deg = 90.0;
    check_ok(nfsu::track_move_free_camera(slanted, quarter, moved, error),
             "a turn from a slanted camera succeeds", error);
    check_ok(nfsu::track_camera_basis(moved, basis, error),
             "and the result has a basis", error);
    // Rodrigues about (0,1,0) by -90 degrees sends (0, s, -s) to
    // (-s, s, 0) with s = 1/sqrt(2): the y component is the axis term and
    // survives the turn, and only the axis term puts it there.
    const double s = 1.0 / std::sqrt(2.0);
    check_close(basis.forward.y, s, 1e-12,
                "the component along the axis survives a turn about it");
    check_close(basis.forward.x, s, 1e-12, "and the rest turns");
    check_close(basis.forward.z, 0.0, 1e-12, "onto the plane of the axis");
}

void test_cull_camera_conversion() {
    std::string error;
    nfsu::TrackFreeCamera camera = street_camera();
    camera.fov_y_deg = 60.0;
    camera.width = 1600;
    camera.height = 900;
    nfsu::TrackCamera cull;
    check_ok(nfsu::track_cull_camera_of(camera, cull, error),
             "the cull camera converts", error);
    check_close(static_cast<double>(cull.vertical_fov),
                60.0 * 3.14159265358979323846 / 180.0, 1e-6,
                "the field of view converts to radians");
    check_close(static_cast<double>(cull.aspect), 1600.0 / 900.0, 1e-6,
                "the aspect is the frame ratio");
    check_close(static_cast<double>(cull.eye[0]), camera.eye.x, 1e-6,
                "the eye narrows");
    check_close(static_cast<double>(cull.target[2]), camera.target.z, 1e-6,
                "the target narrows");
    check_close(static_cast<double>(cull.near_plane), camera.near_plane, 1e-6,
                "the near plane narrows");

    std::string message;
    nfsu::TrackFreeCamera broken = camera;
    broken.near_plane = 0.0;
    check_fails(nfsu::track_cull_camera_of(broken, cull, message), message,
                "near plane", "an invalid camera does not convert");
}

// ---------------------------------------------------------------------------
// The probe
// ---------------------------------------------------------------------------

void test_source_provenance() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the probe runs over the fixture", run.error);
    if (!run.ok) {
        return;
    }
    check_eq_u64(run.probe.instances.size(), 7, "every instance is probed");
    check_eq_u64(run.probe.cost.disagreements, 0,
                 "the probe and the culler agree");
    check_eq_u64(run.probe.issues.size(), 0, "the probe raises no issue");

    const nfsu::TrackSection& section = fixture.objects.sections[0];
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        const std::size_t k = probe.instance_index;
        const nfsu::TrackInstance& source = section.instances[k];
        check_eq_u64(probe.section_offset, section.offset,
                     "the section chunk offset");
        check_eq_u64(probe.instance_offset, source.offset,
                     "the instance record offset");
        check_eq_u64(probe.object_index, source.object_index,
                     "the object index");
        check_eq_u64(probe.object_offset,
                     section.objects[source.object_index].offset,
                     "the object record offset");
        check_eq_u64(probe.section_number, 303, "the section number");
        check(probe.has_section_letter && probe.section_letter == 'C',
              "the section letter is the one speed.exe derives");
        check_eq_u64(probe.instance_flags, source.flags, "the instance flags");
        check_eq_u64(probe.object_flags,
                     section.objects[source.object_index].flags,
                     "the object flags word");
        check_eq_u64(probe.object_flags2,
                     section.objects[source.object_index].flags2,
                     "the second object flags word");
        check_exact(probe.object_radius,
                    section.objects[source.object_index].radius,
                    "the object radius, without the gate's bias");
        for (std::size_t slot = 0; slot < nfsu::kSceneryObjectMeshSlots; ++slot) {
            check_eq_u64(probe.object_mesh_hash[slot],
                         section.objects[source.object_index].mesh_hash[slot],
                         "an object mesh slot hash");
        }
        const nfsu::TrackTransform expected =
            nfsu::track_instance_transform(source);
        for (std::size_t w = 0; w < nfsu::kSceneryInstanceMatrixWords; ++w) {
            check_exact(probe.transform.linear[w], expected.linear[w],
                        "a transform matrix word");
        }
        for (int axis = 0; axis < 3; ++axis) {
            check_exact(probe.transform.translation[axis],
                        expected.translation[axis], "a transform translation");
            check_exact(static_cast<double>(probe.stored_min[axis]),
                        static_cast<double>(source.bounds_min[axis]),
                        "the stored box minimum");
            check_exact(static_cast<double>(probe.stored_max[axis]),
                        static_cast<double>(source.bounds_max[axis]),
                        "the stored box maximum");
        }
    }

    // The mesh provenance of a placed instance points into the geometry
    // report, not into the prepared copy.
    const nfsu::TrackInstanceProbe* first = find_instance(run.probe, 0);
    check(first != nullptr, "instance 0 is in the report");
    if (first != nullptr && first->placed) {
        check_eq_text(first->mesh_name, "PANEL", "the placed mesh name");
        check_eq_u64(first->mesh_hash, kPanel, "the placed mesh hash");
        check(first->source_mesh_index < fixture.geometry.meshes.size(),
              "the source mesh index is inside the geometry report");
        const nfsu::TrackMesh& source =
            fixture.geometry.meshes[first->source_mesh_index];
        check_eq_u64(first->mesh_container_offset, source.offset,
                     "the mesh container offset");
        check_eq_u64(first->mesh_record_offset,
                     source.mesh_metadata_record_offset,
                     "the 0x00134011 record offset");
        check(first->has_mesh_hash_record,
              "the mesh was matched to a hash-table record");
        check_eq_u64(first->mesh_hash_record_offset, source.hash_record_offset,
                     "the 0x00134003 record offset");
        check_eq_u64(first->slot, 0, "the slot that placed it");
        check_eq_u64(first->requested_slot, 0, "the slot the caller asked for");
        check(!first->slot_fallback_used, "no slots 4 and 5 fallback was used");
    } else {
        check(false, "instance 0 places a mesh");
    }
}

// The two boxes a probe reports are the two track_draw_list_bounds reports
// for a draw list of that one instance. Asserting it against that function
// rather than against recorded numbers is what keeps the probe's own walk
// from becoming a second rule for what a frame draws.
void test_bounds_agree_with_the_draw_list() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the probe runs", run.error);
    if (!run.ok) {
        return;
    }
    std::uint64_t compared = 0;
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        if (!probe.placed) {
            check(!probe.drawn_world.valid,
                  "an unplaced instance draws no vertex");
            continue;
        }
        nfsu::TrackDrawList list;
        for (std::size_t d = 0; d < probe.draw_count; ++d) {
            const std::size_t index = probe.first_draw + d;
            if (!fixture.scene.region.draws[index].drawable) {
                continue;
            }
            nfsu::TrackDrawListEntry entry;
            entry.instance = static_cast<std::uint32_t>(probe.scene_instance);
            entry.draw = static_cast<std::uint32_t>(index);
            list.entries.push_back(entry);
        }
        nfsu::TrackDrawListBounds bounds;
        std::string error;
        check_ok(nfsu::track_draw_list_bounds(fixture.scene, list, bounds, error),
                 "the one-instance draw list measures", error);
        check(probe.drawn_world.valid == bounds.drawn.valid,
              "the drawn box is valid on both sides");
        for (int k = 0; k < 3; ++k) {
            check_exact(probe.drawn_world.min[k], bounds.drawn.min[k],
                        "the drawn box minimum matches the draw list's");
            check_exact(probe.drawn_world.max[k], bounds.drawn.max[k],
                        "the drawn box maximum matches the draw list's");
            check_exact(probe.stored_world.min[k], bounds.stored.min[k],
                        "the stored box minimum matches the draw list's");
            check_exact(probe.stored_world.max[k], bounds.stored.max[k],
                        "the stored box maximum matches the draw list's");
        }
        check_eq_u64(probe.drawn_world.points, bounds.drawn.points,
                     "the drawn box visits the same vertices");
        ++compared;
    }
    check(compared >= 5, "at least five placed instances were compared, so the "
                         "comparison above is not vacuous");
}

void test_materials() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the probe runs", run.error);
    if (!run.ok) {
        return;
    }
    // PANEL has one resolved group of two triangles; POST has two groups, one
    // resolved and one whose hash no source claims.
    const nfsu::TrackInstanceProbe* panel = find_instance(run.probe, 0);
    const nfsu::TrackInstanceProbe* post = find_instance(run.probe, 2);
    check(panel != nullptr && post != nullptr, "both instances are reported");
    if (panel == nullptr || post == nullptr) {
        return;
    }
    check_eq_u64(panel->draw_count, 1, "PANEL has one material group");
    check_eq_u64(panel->groups_drawable, 1, "PANEL's group is drawable");
    check_eq_u64(panel->triangles, 2, "PANEL draws two triangles");
    check_eq_u64(panel->groups_resolved, 1, "PANEL's group resolves");
    check_eq_u64(panel->groups_unresolved, 0, "PANEL has no unresolved group");

    check_eq_text(post->mesh_name, "POST", "instance 2 places POST");
    check_eq_u64(post->draw_count, 2, "POST has two material groups");
    check_eq_u64(post->triangles, 2, "POST draws two triangles");
    check_eq_u64(post->groups_resolved, 1, "one POST group resolves");
    check_eq_u64(post->groups_unresolved, 1, "the other POST group does not");
    check_eq_u64(post->groups_unusable, 0, "no POST group is unusable");
    check_eq_u64(post->groups_invalid_slot, 0, "no POST group has a bad slot");

    // The group range names this mesh's groups and no other's.
    for (std::size_t d = 0; d < post->draw_count; ++d) {
        const nfsu::TrackDraw& draw =
            fixture.scene.region.draws[post->first_draw + d];
        check_eq_u64(draw.mesh, post->mesh,
                     "a listed group belongs to the placed mesh");
        check_eq_u64(draw.material_index, d,
                     "the groups are in material record order");
    }

    // And the totals are the sum over the probed, placed instances.
    std::uint64_t groups = 0;
    std::uint64_t triangles = 0;
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        groups += probe.groups_drawable;
        triangles += probe.triangles;
    }
    check_eq_u64(run.probe.cost.groups, groups, "the group total");
    check_eq_u64(run.probe.cost.triangles, triangles, "the triangle total");

    // AN UNDRAWABLE GROUP IS COUNTED AS A GROUP AND DRAWN BY NOTHING.
    // src/track.cpp refuses a material record with no indices and requires
    // every group's range to tile the mesh index stream, so a region parsed
    // from a file never holds one; this clears the flag on a prepared copy,
    // which is what tests/track_draw_list_tests.cpp does for the same reason.
    {
        nfsu::PreparedTrackScene blunted = fixture.scene;
        check(post->draw_count == 2 && post->first_draw + 1 <
                                           blunted.region.draws.size(),
              "POST's two groups are where the probe said they were");
        if (post->draw_count == 2) {
            blunted.region.draws[post->first_draw + 1].drawable = false;
            nfsu::TrackProbeOptions blunt_options;
            blunt_options.cull = options;
            nfsu::TrackProbeReport report;
            std::string error;
            check_ok(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                             blunted, run.cull, run.frustum,
                                             blunt_options, report, error),
                     "the blunted scene probes", error);
            const nfsu::TrackInstanceProbe* dull = find_instance(report, 2);
            check(dull != nullptr, "the POST instance is reported");
            if (dull != nullptr) {
                check_eq_u64(dull->draw_count, 2,
                             "the group is still one of the mesh's two");
                check_eq_u64(dull->groups_drawable, 1,
                             "but only one of them is drawable");
                check_eq_u64(dull->triangles, 1,
                             "and it contributes one triangle rather than "
                             "two");
                check_eq_u64(dull->groups_unresolved, 0,
                             "an undrawable group is not counted by binding "
                             "either");
            }
        }
    }

    // The unplaced instance names the reason rather than leaving it blank.
    const nfsu::TrackInstanceProbe* missing = find_instance(run.probe, 4);
    check(missing != nullptr, "the unplaced instance is reported");
    if (missing != nullptr) {
        check(!missing->placed, "instance 4 places nothing");
        check(missing->has_unplaced_reason, "and says why");
        check(missing->unplaced_reason ==
                  nfsu::TrackUnplacedReason::unresolved_hash,
              "the reason is the unresolved hash");
        check_eq_u64(missing->draw_count, 0, "an unplaced instance has no group");
        check(missing->mesh == nfsu::kNoHandle, "and no mesh handle");
    }
}

void test_residency_reasons() {
    struct Case {
        std::uint32_t number;
        nfsu::TrackResidencyRule rule;
        std::vector<std::uint32_t> zone_sections;
        bool resident;
        const char* reason;
        std::size_t zone_position;
    };
    const std::vector<Case> cases = {
        {303, nfsu::TrackResidencyRule::every_section, {}, true,
         "every-section", nfsu::kTrackProbeNone},
        {310, nfsu::TrackResidencyRule::every_section, {}, true,
         "every-section", nfsu::kTrackProbeNone},
        {303, nfsu::TrackResidencyRule::no_zone, {}, true,
         "sub-index-in-range", nfsu::kTrackProbeNone},
        {310, nfsu::TrackResidencyRule::no_zone, {}, false,
         "sub-index-out-of-range", nfsu::kTrackProbeNone},
        {309, nfsu::TrackResidencyRule::no_zone, {}, true,
         "sub-index-in-range", nfsu::kTrackProbeNone},
        {2650, nfsu::TrackResidencyRule::zone_list, {}, true,
         "always-resident-band", nfsu::kTrackProbeNone},
        {2600, nfsu::TrackResidencyRule::zone_list, {}, true,
         "always-resident-band", nfsu::kTrackProbeNone},
        {2699, nfsu::TrackResidencyRule::zone_list, {}, true,
         "always-resident-band", nfsu::kTrackProbeNone},
        {2700, nfsu::TrackResidencyRule::zone_list, {}, false,
         "not-in-zone-list", nfsu::kTrackProbeNone},
        {303, nfsu::TrackResidencyRule::zone_list, {101, 202, 303}, true,
         "zone-list-entry", 2},
        {303, nfsu::TrackResidencyRule::zone_list, {303, 202}, true,
         "zone-list-entry", 0},
        {303, nfsu::TrackResidencyRule::zone_list, {101, 202}, false,
         "not-in-zone-list", nfsu::kTrackProbeNone},
    };
    for (const Case& item : cases) {
        const Fixture fixture = town(item.number);
        nfsu::TrackCullOptions options;
        options.policy = nfsu::TrackCullPolicy::draw_everything;
        options.residency.rule = item.rule;
        options.residency.zone_sections = item.zone_sections;
        const Run run = probe_run(fixture, street_camera(), options);
        const std::string what = "section " + std::to_string(item.number) +
                                 " under rule " +
                                 std::to_string(static_cast<int>(item.rule));
        check_ok(run.ok, what + " probes", run.error);
        if (!run.ok) {
            continue;
        }
        check_eq_u64(run.probe.sections.size(), 1, what + ": one section");
        if (run.probe.sections.empty()) {
            continue;
        }
        const nfsu::TrackSectionProbe& section = run.probe.sections[0];
        check(section.resident == item.resident,
              what + ": residency is " + (section.resident ? "yes" : "no") +
                  " and must be " + (item.resident ? "yes" : "no"));
        check_eq_text(
            nfsu::track_residency_reason_name(section.residency_reason),
            item.reason, what + ": the reason");
        check_eq_u64(section.zone_position == nfsu::kTrackProbeNone
                         ? 9999
                         : section.zone_position,
                     item.zone_position == nfsu::kTrackProbeNone
                         ? 9999
                         : item.zone_position,
                     what + ": the zone list position");
        check_eq_u64(run.probe.cost.resident_sections, item.resident ? 1 : 0,
                     what + ": the resident section count");
        check_eq_u64(run.probe.cost.disagreements, 0,
                     what + ": the culler agrees about residency");
        // A section that is not resident contributes no visible instance, and
        // every probed instance carries the section's reason.
        for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
            check(probe.section_resident == item.resident,
                  what + ": an instance carries the section's residency");
            check_eq_text(
                nfsu::track_residency_reason_name(probe.residency_reason),
                item.reason, what + ": an instance carries the reason");
            if (!item.resident) {
                check(!probe.visible,
                      what + ": nothing of a non-resident section is visible");
            }
        }
    }
}

// The bounding tree, and the difference between an instance whose own box was
// tested and one the traversal never reached.
void test_tree_provenance() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;  // retail_tree by default
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the retail-tree probe runs", run.error);
    if (!run.ok) {
        return;
    }
    check_eq_u64(run.probe.cost.disagreements, 0,
                 "the probe reproduces the traversal");
    check_eq_u64(run.probe.issues.size(), 0, "and raises no issue");
    check_eq_text(join(probe_visible_set(run.probe)),
                  join(culler_visible_set(run.cull)),
                  "the visible sets are the same instances");

    check_eq_u64(run.probe.sections.size(), 1, "one section");
    const nfsu::TrackSectionProbe& section = run.probe.sections[0];
    check(section.tree_walked, "the probe walked the tree");
    check_eq_u64(section.nodes, 3, "the tree holds three nodes");
    check_eq_u64(section.tree_depth, 1, "and is one level deep");
    check(section.policy_used == nfsu::TrackCullPolicy::retail_tree,
          "the section was culled with the recovered rule");
    check(section.containment_checked && section.containment_holds,
          "the fixture tree contains what is below it");

    // Node 2 holds instances 4, 5 and 6 and is entirely outside, so all three
    // are pruned and no gate is applied to any of them. Their own boxes are
    // still classified, which is the sentence no other report can make.
    const std::size_t pruned_instances[3] = {4, 5, 6};
    for (const std::size_t k : pruned_instances) {
        const nfsu::TrackInstanceProbe* probe = find_instance(run.probe, k);
        check(probe != nullptr, "instance " + std::to_string(k) + " is reported");
        if (probe == nullptr) {
            continue;
        }
        check_eq_u64(probe->node_index, 2,
                     "instance " + std::to_string(k) + " is named by node 2");
        check_eq_u64(probe->node_depth, 1, "node 2 is one level down");
        check_eq_u64(probe->pruned_by_node, 2, "and node 2 is what pruned it");
        check(probe->mask_gate == nfsu::TrackGateVerdict::not_applied,
              "a pruned instance reaches no mask gate");
        check(probe->box_gate == nfsu::TrackGateVerdict::not_applied,
              "a pruned instance reaches no box gate");
        check(probe->size_gate == nfsu::TrackGateVerdict::not_applied,
              "a pruned instance reaches no size gate");
        check(probe->own_box_class == nfsu::TrackBoxClass::outside,
              "and its own box is measured anyway");
        check(!probe->visible && !probe->culler_visible,
              "a pruned instance is not visible");
    }
    check_eq_u64(run.probe.cost.pruned, 3, "three instances were pruned");
    check_eq_u64(section.pruned, 3, "and the section says so too");

    // Instance 0 sits under node 1, which the frustum crosses, so its own box
    // IS tested and rejects it. That is a different route to the same answer,
    // and the two must not be reported the same way.
    const nfsu::TrackInstanceProbe* rejected = find_instance(run.probe, 0);
    check(rejected != nullptr, "instance 0 is reported");
    if (rejected != nullptr) {
        check_eq_u64(rejected->node_index, 1, "instance 0 hangs off node 1");
        check_eq_u64(rejected->pruned_by_node, nfsu::kTrackProbeNone,
                     "and was not pruned");
        check(rejected->node_class == nfsu::TrackBoxClass::intersecting,
              "node 1 crosses the frustum");
        check(rejected->mask_gate == nfsu::TrackGateVerdict::passed,
              "the mask gate ran and passed");
        check(rejected->box_gate == nfsu::TrackGateVerdict::rejected,
              "and the box gate rejected it");
        check(rejected->instance_class == nfsu::TrackBoxClass::outside,
              "because its own box is outside");
        check(!rejected->visible, "so it is not visible");
    }
    check_eq_u64(run.probe.cost.rejected_by_box, 1,
                 "exactly one instance was rejected by its own box");
    check_eq_u64(run.probe.cost.visible, 3, "three instances survive");
    check_eq_u64(run.probe.cost.probed, 7, "over seven probed");
    check_eq_u64(run.probe.cost.culler_visible, 3, "and the culler agrees");
    check_eq_u64(run.probe.cost.placed, 6, "six of the seven place a mesh");
    check_eq_u64(run.probe.cost.unplaced, 1, "and one does not");

    // Instance 2 sits squarely in the middle of the frustum, and its node
    // crosses the frustum, so the box test runs and reports inside rather
    // than being skipped.
    const nfsu::TrackInstanceProbe* middle = find_instance(run.probe, 2);
    check(middle != nullptr, "instance 2 is reported");
    if (middle != nullptr) {
        check(middle->box_gate == nfsu::TrackGateVerdict::passed,
              "the middle instance's box gate ran");
        check(middle->instance_class == nfsu::TrackBoxClass::inside,
              "and found it inside");
        check(middle->visible && middle->culler_visible,
              "so it is visible on both counts");
    }
}

// The same region under the two policies that walk no tree.
void test_other_policies() {
    const Fixture fixture = town();

    nfsu::TrackCullOptions everything;
    everything.policy = nfsu::TrackCullPolicy::draw_everything;
    const Run all = probe_run(fixture, street_camera(), everything);
    check_ok(all.ok, "draw-everything probes", all.error);
    if (all.ok) {
        check_eq_u64(all.probe.cost.visible, 7,
                     "draw-everything makes every instance visible");
        check_eq_u64(all.probe.cost.disagreements, 0,
                     "draw-everything agrees with the culler");
        check_eq_u64(all.probe.cost.pruned, 0, "and prunes nothing");
        check_eq_u64(all.probe.cost.rejected_by_box, 0,
                     "and rejects nothing by box");
        for (const nfsu::TrackInstanceProbe& probe : all.probe.instances) {
            check(probe.mask_gate == nfsu::TrackGateVerdict::not_applied,
                  "no gate runs under draw-everything");
            check(probe.box_gate == nfsu::TrackGateVerdict::not_applied,
                  "including the box gate");
            check_eq_u64(probe.node_index, 0,
                         "and the node is the flat placeholder");
        }
    }

    nfsu::TrackCullOptions flat;
    flat.policy = nfsu::TrackCullPolicy::flat_instance_test;
    const Run per_instance = probe_run(fixture, street_camera(), flat);
    check_ok(per_instance.ok, "flat-instance-test probes", per_instance.error);
    if (per_instance.ok) {
        check_eq_u64(per_instance.probe.cost.disagreements, 0,
                     "the flat test agrees with the culler");
        check_eq_u64(per_instance.probe.cost.pruned, 0,
                     "a flat test prunes nothing");
        check_eq_u64(per_instance.probe.cost.rejected_by_box, 4,
                     "and rejects the four the tree pruned or rejected");
        check_eq_u64(per_instance.probe.cost.visible, 3,
                     "reaching the same visible set as the tree");
    }

    // The two routes reach the same answer, which is what makes the tree an
    // optimisation rather than a second rule.
    if (all.ok && per_instance.ok) {
        const Run tree =
            probe_run(fixture, street_camera(), nfsu::TrackCullOptions{});
        check_ok(tree.ok, "the tree probes", tree.error);
        if (tree.ok) {
            check_eq_text(join(probe_visible_set(tree.probe)),
                          join(probe_visible_set(per_instance.probe)),
                          "the tree and the flat test see the same instances");
        }
    }
}

void test_mask_gate() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::flat_instance_test;
    options.instance_mask = 0x01U;
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the mask probe runs", run.error);
    if (!run.ok) {
        return;
    }
    check_eq_u64(run.probe.cost.disagreements, 0,
                 "the mask gate agrees with the culler");
    check_eq_u64(run.probe.cost.rejected_by_mask, 1,
                 "one instance carries a bit the mask sees");
    const nfsu::TrackInstanceProbe* masked = find_instance(run.probe, 2);
    check(masked != nullptr, "the masked instance is reported");
    if (masked != nullptr) {
        check(masked->mask_gate == nfsu::TrackGateVerdict::rejected,
              "its mask gate rejects");
        check_eq_u64(masked->mask_bits, 0x01U,
                     "and the bits the gate saw are reported");
        check(masked->box_gate == nfsu::TrackGateVerdict::not_applied,
              "the box gate after a mask rejection does not run");
        check(masked->own_box_class == nfsu::TrackBoxClass::inside,
              "even though its own box is inside");
        check(!masked->visible, "so it is not visible");
    }
    const nfsu::TrackInstanceProbe* quiet = find_instance(run.probe, 1);
    check(quiet != nullptr, "an unmasked instance is reported");
    if (quiet != nullptr) {
        check(quiet->mask_gate == nfsu::TrackGateVerdict::passed,
              "an unmasked instance passes the gate");
        check_eq_u64(quiet->mask_bits, 0, "with no bits set");
    }

    // A zero mask, which is the default, sees nothing at all.
    nfsu::TrackCullOptions open = options;
    open.instance_mask = 0;
    const Run unmasked = probe_run(fixture, street_camera(), open);
    check_ok(unmasked.ok, "the unmasked probe runs", unmasked.error);
    if (unmasked.ok) {
        check_eq_u64(unmasked.probe.cost.rejected_by_mask, 0,
                     "a zero mask rejects nothing");
        check_eq_u64(unmasked.probe.cost.disagreements, 0,
                     "and still agrees with the culler");
    }

    // THE BIAS ITSELF. At mask 0x20 the xor decides the answer for every
    // instance in the fixture and decides it the other way round: instance 3
    // carries 0x0040, whose biased low byte is 0x20, and every other
    // instance carries 0x0060, whose biased low byte is 0. Drop the bias and
    // instance 3 passes while the others are rejected - one rejection
    // becomes five, in the opposite places.
    nfsu::TrackCullOptions biased = options;
    biased.instance_mask = 0x20U;
    const Run by_bias = probe_run(fixture, street_camera(), biased);
    check_ok(by_bias.ok, "the bias probe runs", by_bias.error);
    if (by_bias.ok) {
        check_eq_u64(by_bias.probe.cost.disagreements, 0,
                     "the bias gate agrees with the culler");
        check_eq_u64(by_bias.probe.cost.rejected_by_mask, 1,
                     "exactly one instance is rejected at mask 0x20");
        const nfsu::TrackInstanceProbe* biased_out =
            find_instance(by_bias.probe, 3);
        check(biased_out != nullptr, "instance 3 is reported");
        if (biased_out != nullptr) {
            check(biased_out->mask_gate == nfsu::TrackGateVerdict::rejected,
                  "and it is the one the bias rejects");
            check_eq_u64(biased_out->mask_bits, 0x20U,
                         "with the biased bits reported");
        }
        const nfsu::TrackInstanceProbe* quiet_at_20 =
            find_instance(by_bias.probe, 1);
        check(quiet_at_20 != nullptr, "an 0x0060 instance is reported");
        if (quiet_at_20 != nullptr) {
            check(quiet_at_20->mask_gate == nfsu::TrackGateVerdict::passed,
                  "and the bias lets it through");
            check_eq_u64(quiet_at_20->mask_bits, 0, "with no bits set");
        }
    }
}

// A node the traversal reached with the INSIDE code makes the instance box
// test unnecessary, exactly as retail skips it. The street camera never
// produces one - its frustum is too narrow to contain a whole node - so this
// widens it until node 1 is fully inside.
void test_the_box_test_a_node_makes_unnecessary() {
    const Fixture fixture = town();
    nfsu::TrackFreeCamera wide = street_camera();
    wide.fov_y_deg = 120.0;
    wide.far_plane = 200.0;
    wide.eye = nfsu::Vec3{-11.0, 0.0, 30.0};
    wide.target = nfsu::Vec3{-11.0, 0.0, 0.0};
    nfsu::TrackCullOptions options;  // retail_tree
    const Run run = probe_run(fixture, wide, options);
    check_ok(run.ok, "the wide probe runs", run.error);
    if (!run.ok) {
        return;
    }
    check_eq_u64(run.probe.cost.disagreements, 0, "the culler agrees");
    std::uint64_t skipped = 0;
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        if (probe.node_class != nfsu::TrackBoxClass::inside ||
            probe.pruned_by_node != nfsu::kTrackProbeNone) {
            continue;
        }
        ++skipped;
        check(probe.box_gate == nfsu::TrackGateVerdict::not_applied,
              "a node that tested inside makes the box test unnecessary");
        check(probe.instance_class == nfsu::TrackBoxClass::inside,
              "and the instance is recorded inside without being tested");
        check(probe.visible, "so the instance is visible");
    }
    check(skipped >= 3,
          "at least three instances hang off a node that tested inside, so "
          "the case above is not vacuous: got " + std::to_string(skipped));
}

void test_screen_size_gate() {
    const Fixture fixture = town();
    const nfsu::TrackFreeCamera camera = street_camera();
    nfsu::TrackCameraBasis basis;
    std::string error;
    check_ok(nfsu::track_camera_basis(camera, basis, error),
             "the camera has a basis", error);

    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::flat_instance_test;
    options.use_screen_size = true;
    const float eye_world[3] = {static_cast<float>(camera.eye.x),
                                static_cast<float>(camera.eye.y),
                                static_cast<float>(camera.eye.z)};
    nfsu::track_raw_from_world(eye_world, options.screen_eye);
    const float forward_world[3] = {static_cast<float>(basis.forward.x),
                                    static_cast<float>(basis.forward.y),
                                    static_cast<float>(basis.forward.z)};
    nfsu::track_raw_from_world(forward_world, options.screen_forward);

    // A generous scale keeps everything the frustum kept.
    options.screen_scale = 1000.0F;
    const Run generous = probe_run(fixture, camera, options);
    check_ok(generous.ok, "the generous screen-size probe runs", generous.error);
    if (generous.ok) {
        check_eq_u64(generous.probe.cost.rejected_by_size, 0,
                     "a large scale rejects nothing");
        check_eq_u64(generous.probe.cost.visible, 3,
                     "and the visible set is the frustum's");
        check_eq_u64(generous.probe.cost.disagreements, 0,
                     "and the culler agrees");
        const nfsu::TrackInstanceProbe* probe = find_instance(generous.probe, 1);
        check(probe != nullptr, "an instance is reported");
        if (probe != nullptr) {
            check(probe->screen_level_measured, "its level was measured");
            check(probe->size_gate == nfsu::TrackGateVerdict::passed,
                  "and the gate passed it");
            // The level is the retail arithmetic over this instance's own
            // radius, not a number this test recorded.
            const nfsu::TrackInstance& source =
                fixture.objects.sections[0].instances[1];
            const float radius =
                fixture.objects.sections[0].objects[source.object_index].radius +
                nfsu::kTrackRadiusBias;
            const int expected = nfsu::track_screen_size_level(
                options.screen_eye, options.screen_forward,
                options.screen_scale, source.position, radius);
            check_exact(static_cast<double>(probe->screen_level),
                        static_cast<double>(expected),
                        "the reported level is the retail one");
            check(expected >= nfsu::kTrackMinDrawLevel,
                  "and it is above the draw threshold");
        }
    }

    // A small one starts rejecting, and WHICH instances it rejects is the
    // object radius rather than the distance: the three the frustum kept sit
    // at the same distance from the eye, and the one that survives is the one
    // whose object record carries the larger radius. Every verdict below is
    // derived from track_screen_size_level over the record's own radius, not
    // recorded from a run.
    options.screen_scale = 4.0F;
    const Run mean = probe_run(fixture, camera, options);
    check_ok(mean.ok, "the small screen-size probe runs", mean.error);
    if (mean.ok) {
        check_eq_u64(mean.probe.cost.disagreements, 0,
                     "and the culler agrees");
        std::uint64_t expected_visible = 0;
        std::uint64_t expected_rejected = 0;
        const std::size_t frustum_kept[3] = {1, 2, 3};
        for (const std::size_t k : frustum_kept) {
            const nfsu::TrackInstance& source =
                fixture.objects.sections[0].instances[k];
            const float radius =
                fixture.objects.sections[0].objects[source.object_index].radius +
                nfsu::kTrackRadiusBias;
            const int level = nfsu::track_screen_size_level(
                options.screen_eye, options.screen_forward,
                options.screen_scale, source.position, radius);
            const bool keeps = level >= nfsu::kTrackMinDrawLevel;
            if (keeps) {
                ++expected_visible;
            } else {
                ++expected_rejected;
            }
            const nfsu::TrackInstanceProbe* probe = find_instance(mean.probe, k);
            check(probe != nullptr,
                  "instance " + std::to_string(k) + " is reported");
            if (probe != nullptr) {
                check_exact(static_cast<double>(probe->screen_level),
                            static_cast<double>(level),
                            "the level of instance " + std::to_string(k));
                check(probe->visible == keeps,
                      "the size gate decides instance " + std::to_string(k) +
                          " by its own radius");
            }
        }
        check(expected_rejected >= 1 && expected_visible >= 1,
              "the small scale separates the instances rather than rejecting "
              "or keeping them all, which is what makes this case measure the "
              "gate");
        check_eq_u64(mean.probe.cost.visible, expected_visible,
                     "the visible count is the derived one");
        check_eq_u64(mean.probe.cost.rejected_by_size, expected_rejected,
                     "and so is the rejected count");
    }

    // With the gate off the level is not measured, and reporting a level there
    // would be reporting a number nothing computed.
    options.use_screen_size = false;
    const Run off = probe_run(fixture, camera, options);
    check_ok(off.ok, "the gate-off probe runs", off.error);
    if (off.ok) {
        for (const nfsu::TrackInstanceProbe& probe : off.probe.instances) {
            check(!probe.screen_level_measured,
                  "no level is measured with the gate off");
            check_exact(static_cast<double>(probe.screen_level), 0.0,
                        "and the reported level is zero");
            check(probe.size_gate == nfsu::TrackGateVerdict::not_applied,
                  "and the gate did not run");
        }
    }

    // A forward axis of some other LENGTH gives the same answers, because
    // the gate compares a dot product against a negated radius and that only
    // means "behind by more than the radius" for a unit axis. The level is
    // asserted instance by instance against the unit run, so a normalization
    // that stopped happening changes numbers this case reads rather than a
    // count it might not.
    options.use_screen_size = true;
    options.screen_scale = 4.0F;
    const Run unit = probe_run(fixture, camera, options);
    check_ok(unit.ok, "the unit-axis probe runs", unit.error);
    nfsu::TrackCullOptions stretched = options;
    for (int k = 0; k < 3; ++k) {
        stretched.screen_forward[k] *= 37.5F;
    }
    const Run long_axis = probe_run(fixture, camera, stretched);
    check_ok(long_axis.ok, "the long-axis probe runs", long_axis.error);
    if (unit.ok && long_axis.ok) {
        check_eq_u64(long_axis.probe.cost.disagreements, 0,
                     "the long axis agrees with the culler");
        check_eq_u64(long_axis.probe.instances.size(),
                     unit.probe.instances.size(), "the same instances");
        for (std::size_t i = 0; i < unit.probe.instances.size() &&
                                i < long_axis.probe.instances.size();
             ++i) {
            check_exact(
                static_cast<double>(long_axis.probe.instances[i].screen_level),
                static_cast<double>(unit.probe.instances[i].screen_level),
                "the axis length does not change a screen-size level");
        }
        check_eq_u64(long_axis.probe.cost.visible, unit.probe.cost.visible,
                     "nor the visible set");
        check(unit.probe.cost.rejected_by_size > 0,
              "and this scale rejects something, so the comparison is not "
              "between two runs that reject nothing");
    }

    // A forward axis that cannot be normalized disables the gate and says so,
    // the same answer cull_track_region reaches.
    options.use_screen_size = true;
    options.screen_scale = 1000.0F;
    for (int k = 0; k < 3; ++k) {
        options.screen_forward[k] = 0.0F;
    }
    const Run broken = probe_run(fixture, camera, options);
    check_ok(broken.ok, "the zero-axis probe runs", broken.error);
    if (broken.ok) {
        check_eq_u64(broken.probe.issues.size(), 1,
                     "a zero forward axis raises exactly one issue");
        check_eq_u64(broken.probe.cost.rejected_by_size, 0,
                     "and the gate rejects nothing");
        check_eq_u64(broken.probe.cost.disagreements, 0,
                     "and the culler reached the same set");
    }
}

void test_selection() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;

    nfsu::TrackProbeSelection one_section;
    one_section.has_section = true;
    one_section.section_index = 0;
    const Run section_run =
        probe_run(fixture, street_camera(), options, one_section);
    check_ok(section_run.ok, "a section selection probes", section_run.error);
    if (section_run.ok) {
        check_eq_u64(section_run.probe.cost.probed, 7,
                     "the only section holds every instance");
    }

    nfsu::TrackProbeSelection two;
    two.has_section = true;
    two.section_index = 0;
    two.instances = {5, 1};
    const Run instance_run = probe_run(fixture, street_camera(), options, two);
    check_ok(instance_run.ok, "an instance selection probes",
             instance_run.error);
    if (instance_run.ok) {
        check_eq_u64(instance_run.probe.cost.probed, 2, "two instances probed");
        check_eq_u64(instance_run.probe.instances.size(), 2, "and two reported");
        // Ascending, whatever order the selection was written in.
        check_eq_u64(instance_run.probe.instances[0].instance_index, 1,
                     "the report is ascending");
        check_eq_u64(instance_run.probe.instances[1].instance_index, 5,
                     "whatever order the selection was written in");
        check_eq_u64(instance_run.probe.cost.instances, 7,
                     "the region's own instance count is unchanged");
    }

    nfsu::TrackProbeSelection by_mesh;
    by_mesh.mesh_name = "POST";
    const Run mesh_run = probe_run(fixture, street_camera(), options, by_mesh);
    check_ok(mesh_run.ok, "a mesh selection probes", mesh_run.error);
    if (mesh_run.ok) {
        check_eq_u64(mesh_run.probe.cost.probed, 2, "two instances place POST");
        for (const nfsu::TrackInstanceProbe& probe : mesh_run.probe.instances) {
            check_eq_text(probe.mesh_name, "POST",
                          "every reported instance places the named mesh");
        }
        // A filtered instance contributes to no count at all, which is what
        // "over the probed instances" means.
        check_eq_u64(mesh_run.probe.cost.unplaced, 0,
                     "the unplaced instance is filtered out of every count");
        check_eq_u64(mesh_run.probe.cost.placed, 2, "and the placed count");
        check_eq_u64(mesh_run.probe.sections[0].probed, 2,
                     "and the section's own count");
    }

    // The refusals. A selection that matches nothing is refused rather than
    // answered with an empty report, which cannot be told from a region that
    // holds nothing.
    struct Bad {
        const char* what;
        const char* needle;
        nfsu::TrackProbeSelection select;
    };
    std::vector<Bad> bad_cases;
    nfsu::TrackProbeSelection bad;
    bad.instances = {0};
    bad_cases.push_back({"an instance with no section", "needs a section", bad});
    bad = nfsu::TrackProbeSelection{};
    bad.has_section = true;
    bad.section_index = 9;
    bad_cases.push_back({"a section past the end", "outside the region's", bad});
    bad = nfsu::TrackProbeSelection{};
    bad.has_section = true;
    // Exactly one past the end of a seven-instance section, so a bound that
    // became one too loose is caught rather than only a wild value.
    bad.instances = {0, 7};
    bad_cases.push_back({"an instance past the end", "outside section", bad});
    bad = nfsu::TrackProbeSelection{};
    bad.has_section = true;
    bad.instances = {3, 1, 3};
    bad_cases.push_back({"a repeated instance", "more than once", bad});
    bad = nfsu::TrackProbeSelection{};
    bad.mesh_name = "NOSUCHMESH";
    bad_cases.push_back({"an unknown mesh name", "is named", bad});

    for (const Bad& item : bad_cases) {
        const Run run =
            probe_run(fixture, street_camera(), options, item.select);
        check_fails(run.ok, run.error, item.needle,
                    std::string("the probe refuses ") + item.what);
    }

    // A limit the selection crosses, and the boundary it does not, so a `>=`
    // that became `>` is caught.
    nfsu::TrackProbeOptions limited;
    limited.cull = options;
    limited.limits.max_instances = 3;
    nfsu::TrackCamera cull_camera;
    nfsu::TrackFrustum frustum;
    std::string error;
    check_ok(nfsu::track_cull_camera_of(street_camera(), cull_camera, error),
             "the limit case converts its camera", error);
    check_ok(nfsu::track_frustum_look_at(cull_camera, frustum),
             "the limit case builds a frustum", error);
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, options);
    nfsu::TrackProbeReport report;
    std::string message;
    check_fails(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                        fixture.scene, cull, frustum, limited,
                                        report, message),
                message, "more than 3 instances",
                "the instance limit is enforced");
    // ONE BELOW the count is the case that separates `>=` from `>`: both
    // refuse a limit far below, and both accept one at or above, so only a
    // limit of exactly count - 1 tells them apart.
    limited.limits.max_instances = 6;
    check_fails(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                        fixture.scene, cull, frustum, limited,
                                        report, message),
                message, "more than 6 instances",
                "a limit one below the count is enforced");
    limited.limits.max_instances = 7;
    check_ok(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                     fixture.scene, cull, frustum, limited,
                                     report, message),
             "the limit accepts exactly its own count", message);
    check_eq_u64(report.cost.probed, 7, "and probes all seven");
}

// The reports have to be about the same two regions, or every offset in the
// answer describes something the caller did not ask about.
void test_mismatched_inputs() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;
    nfsu::TrackCamera cull_camera;
    nfsu::TrackFrustum frustum;
    std::string error;
    check_ok(nfsu::track_cull_camera_of(street_camera(), cull_camera, error),
             "the camera converts", error);
    check_ok(nfsu::track_frustum_look_at(cull_camera, frustum),
             "the frustum builds", error);
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, options);

    nfsu::TrackProbeOptions probe_options;
    probe_options.cull = options;
    nfsu::TrackProbeReport report;
    std::string message;

    nfsu::TrackObjectReport other_objects = fixture.objects;
    other_objects.region.offset += 16;
    check_fails(nfsu::probe_track_scene(fixture.geometry, other_objects,
                                        fixture.scene, cull, frustum,
                                        probe_options, report, message),
                message, "object region",
                "an object report from another region is refused");

    nfsu::TrackRegionReport other_geometry = fixture.geometry;
    other_geometry.region.size += 16;
    check_fails(nfsu::probe_track_scene(other_geometry, fixture.objects,
                                        fixture.scene, cull, frustum,
                                        probe_options, report, message),
                message, "geometry region",
                "a geometry report from another region is refused");

    nfsu::TrackCullReport short_cull = cull;
    short_cull.sections.clear();
    check_fails(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                        fixture.scene, short_cull, frustum,
                                        probe_options, report, message),
                message, "does not cover valid section",
                "a cull report covering no section is refused");

    nfsu::TrackCullReport wide_cull = cull;
    check(!wide_cull.visible.empty(), "the fallback cull sees something");
    if (!wide_cull.visible.empty()) {
        wide_cull.visible[0].instance_index = 99;
        check_fails(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                            fixture.scene, wide_cull, frustum,
                                            probe_options, report, message),
                    message, "which this object region does not hold",
                    "a cull report naming an instance the region lacks is "
                    "refused");

        nfsu::TrackCullReport twice_cull = cull;
        twice_cull.visible.push_back(twice_cull.visible[0]);
        check_fails(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                            fixture.scene, twice_cull, frustum,
                                            probe_options, report, message),
                    message, "twice",
                    "a cull report naming one instance twice is refused");
    }

    nfsu::TrackCullReport wide_summary = cull;
    wide_summary.sections[0].section_index = 42;
    check_fails(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                        fixture.scene, wide_summary, frustum,
                                        probe_options, report, message),
                message, "summarises section",
                "a cull summary of a section the region lacks is refused");

    // The prepared groups of one mesh have to be consecutive. A file cannot
    // produce a region where they are not, so this tampers with a prepared
    // copy - which is exactly the case the check exists for, because the
    // scene is a public type a caller may hand in.
    nfsu::PreparedTrackScene shuffled = fixture.scene;
    check(shuffled.region.draws.size() >= 3,
          "the fixture has enough groups to shuffle");
    if (shuffled.region.draws.size() >= 3) {
        // The fixture's three groups are mesh 0, mesh 1, mesh 1. Swapping the
        // first two INTERLEAVES them - 1, 0, 1 - which is the shape the check
        // is about; swapping the ends would only reverse two contiguous runs
        // and pass, which an earlier draft of this case did.
        std::swap(shuffled.region.draws[0], shuffled.region.draws[1]);
        check_fails(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                            shuffled, cull, frustum,
                                            probe_options, report, message),
                    message, "not consecutive",
                    "a region whose groups are not consecutive is refused");
    }
}

// A disagreement is reported and does not stop the report, because a caller
// needs both answers to know which to distrust.
void test_disagreement_is_reported() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;
    nfsu::TrackCamera cull_camera;
    nfsu::TrackFrustum frustum;
    std::string error;
    check_ok(nfsu::track_cull_camera_of(street_camera(), cull_camera, error),
             "the camera converts", error);
    check_ok(nfsu::track_frustum_look_at(cull_camera, frustum),
             "the frustum builds", error);
    nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, options);
    check_eq_u64(cull.visible.size(), 7, "the fallback policy sees everything");

    // Drop one visible entry: the probe still says visible and the culler now
    // does not.
    cull.visible.erase(cull.visible.begin() + 3);
    nfsu::TrackProbeOptions probe_options;
    probe_options.cull = options;
    nfsu::TrackProbeReport report;
    std::string message;
    check_ok(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                     fixture.scene, cull, frustum,
                                     probe_options, report, message),
             "a disagreeing cull report still produces a report", message);
    check_eq_u64(report.cost.disagreements, 1, "one disagreement is counted");
    check_eq_u64(report.issues.size(), 1, "and one issue names it");
    check(!report.ok(), "and the report is not ok");
    check_eq_u64(report.cost.visible, 7, "the probe kept its own answer");
    check_eq_u64(report.cost.culler_visible, 6, "beside the culler's");

    // A residency disagreement is its own issue, at the section.
    nfsu::TrackCullReport moved =
        nfsu::cull_track_region(fixture.objects, frustum, options);
    moved.sections[0].resident = false;
    nfsu::TrackProbeReport second;
    check_ok(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                     fixture.scene, moved, frustum,
                                     probe_options, second, message),
             "a disagreeing residency still produces a report", message);
    check(second.cost.disagreements >= 1, "the residency disagreement counts");
    check(!second.ok(), "and the report is not ok");
    check(!second.sections[0].resident,
          "the culler's answer is the one carried forward");

    // A node the culler recorded differently is a third issue shape, and the
    // box class it recorded is a fourth. Each is a separate arm of the same
    // comparison and one of them can go while the others stand.
    const nfsu::TrackCullReport tree_cull = nfsu::cull_track_region(
        fixture.objects, frustum, nfsu::TrackCullOptions{});
    check(!tree_cull.visible.empty(), "the tree cull sees something");
    if (!tree_cull.visible.empty()) {
        struct Tamper {
            const char* what;
            void (*apply)(nfsu::TrackVisibleInstance&);
        };
        const std::vector<Tamper> tampers = {
            {"a node index",
             [](nfsu::TrackVisibleInstance& item) { item.node_index += 1; }},
            {"a carried node class",
             [](nfsu::TrackVisibleInstance& item) {
                 item.node_class = item.node_class == nfsu::TrackBoxClass::inside
                                       ? nfsu::TrackBoxClass::intersecting
                                       : nfsu::TrackBoxClass::inside;
             }},
            {"an instance box class",
             [](nfsu::TrackVisibleInstance& item) {
                 item.instance_class =
                     item.instance_class == nfsu::TrackBoxClass::inside
                         ? nfsu::TrackBoxClass::intersecting
                         : nfsu::TrackBoxClass::inside;
             }},
        };
        for (const Tamper& item : tampers) {
            nfsu::TrackCullReport tampered = tree_cull;
            item.apply(tampered.visible[0]);
            nfsu::TrackProbeOptions tree_options;
            nfsu::TrackProbeReport third;
            check_ok(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                             fixture.scene, tampered, frustum,
                                             tree_options, third, message),
                     std::string("a disagreeing ") + item.what +
                         " still produces a report",
                     message);
            check_eq_u64(third.cost.disagreements, 1,
                         std::string("the ") + item.what +
                             " disagreement is counted");
            check(!third.ok(), std::string("and the report is not ok for ") +
                                   item.what);
        }
    }
}

// Two sections in one region. Everything the probe keys by section index -
// the culler's per-instance answers, the per-section summary, the selection -
// is a table whose base is zero for the first section and non-zero for the
// second, so a region with one section cannot tell a right answer from a
// table that always answers zero.
void test_two_sections() {
    const Fixture fixture = make_fixture(fixture_meshes(),
                                         two_sections(303, 310));
    check_eq_u64(fixture.objects.sections.size(), 2,
                 "the region holds two sections");
    nfsu::TrackCullOptions options;  // retail_tree
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the two-section probe runs", run.error);
    if (!run.ok) {
        return;
    }
    check_eq_u64(run.probe.cost.disagreements, 0, "the culler agrees");
    check_eq_u64(run.probe.issues.size(), 0, "and nothing is raised");
    check_eq_u64(run.probe.sections.size(), 2, "both sections are reported");
    check_eq_u64(run.probe.cost.instances, 13, "seven and six instances");
    check_eq_u64(run.probe.cost.probed, 13, "all thirteen are probed");
    check_eq_u64(run.probe.sections[0].instances, 7, "the first holds seven");
    check_eq_u64(run.probe.sections[1].instances, 6, "the second holds six");
    check_eq_u64(run.probe.sections[1].number, 310, "the second's number");
    check_eq_u64(run.probe.sections[1].nodes, 3, "over three nodes");
    check_eq_u64(run.probe.sections[1].tree_depth, 2, "two levels down");
    check(run.probe.sections[1].tree_walked, "and the probe walked it");

    // The second section is entirely outside the frustum, and the prune root
    // is found at node 1 and CARRIED DOWN to node 2's five instances. A carry
    // that stopped where the prune was found would leave those five
    // unpruned, and they would be rejected by their own box instead.
    check_eq_u64(run.probe.sections[1].visible, 0,
                 "nothing of the second section is visible");
    check_eq_u64(run.probe.sections[1].pruned, 6,
                 "and all six of its instances are pruned");
    check_eq_u64(run.probe.sections[1].rejected_by_box, 0,
                 "none of them reaches its own box test");
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        if (probe.section_index != 1) {
            continue;
        }
        check_eq_u64(probe.pruned_by_node, 1,
                     "every instance of it is pruned by node 1");
        if (probe.instance_index == 0) {
            check_eq_u64(probe.node_index, 1, "instance 0 hangs off node 1");
            check_eq_u64(probe.node_depth, 1, "one level down");
        } else {
            check_eq_u64(probe.node_index, 2, "the rest hang off node 2");
            check_eq_u64(probe.node_depth, 2, "two levels down");
        }
    }

    // Ascending (section, instance), and every record carries its own
    // section. A per-section base that was always zero would fold the second
    // section's culler answers onto the first section's instances, which is
    // exactly what the agreement check above would then report.
    std::size_t last_section = 0;
    std::size_t last_instance = 0;
    bool first = true;
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        if (!first) {
            check(probe.section_index > last_section ||
                      (probe.section_index == last_section &&
                       probe.instance_index > last_instance),
                  "the report is ascending in section and instance");
        }
        first = false;
        last_section = probe.section_index;
        last_instance = probe.instance_index;
        check_eq_u64(probe.section_number,
                     run.probe.sections[probe.section_index].number,
                     "an instance carries its own section's number");
        check_eq_u64(probe.section_offset,
                     fixture.objects.sections[probe.section_index].offset,
                     "and its own section's chunk offset");
    }
    check_eq_text(join(probe_visible_set(run.probe)),
                  join(culler_visible_set(run.cull)),
                  "the two visible sets hold the same instance indices");

    // A selection of the SECOND section probes only its three.
    nfsu::TrackProbeSelection second;
    second.has_section = true;
    second.section_index = 1;
    const Run only_second = probe_run(fixture, street_camera(), options,
                                      second);
    check_ok(only_second.ok, "the second section probes", only_second.error);
    if (only_second.ok) {
        check_eq_u64(only_second.probe.cost.probed, 6, "six instances");
        check_eq_u64(only_second.probe.sections[0].probed, 0,
                     "and none from the first");
        check_eq_u64(only_second.probe.sections[1].probed, 6,
                     "and six from the second");
        for (const nfsu::TrackInstanceProbe& probe :
             only_second.probe.instances) {
            check_eq_u64(probe.section_index, 1,
                         "every record is from the selected section");
        }
    }

    // And the two sections can disagree about residency, which one section
    // can never show: 303 modulo 100 is 3 and 310 modulo 100 is 10.
    nfsu::TrackCullOptions no_zone;
    no_zone.residency.rule = nfsu::TrackResidencyRule::no_zone;
    const Run mixed = probe_run(fixture, street_camera(), no_zone);
    check_ok(mixed.ok, "the mixed-residency probe runs", mixed.error);
    if (mixed.ok) {
        check_eq_u64(mixed.probe.cost.disagreements, 0, "the culler agrees");
        check_eq_u64(mixed.probe.cost.resident_sections, 1,
                     "one of the two is resident");
        check(mixed.probe.sections[0].resident, "303 is resident");
        check(!mixed.probe.sections[1].resident, "and 310 is not");
        check_eq_text(nfsu::track_residency_reason_name(
                          mixed.probe.sections[1].residency_reason),
                      "sub-index-out-of-range", "for the reason it gives");
        for (const nfsu::TrackInstanceProbe& probe : mixed.probe.instances) {
            if (probe.section_index == 1) {
                check(!probe.visible,
                      "nothing of the second section is visible");
                check_eq_u64(probe.node_index, nfsu::kTrackProbeNone,
                             "and no node is reported for it");
            }
        }
    }
}

// A section the parser refused. It is kept with valid == false and no
// decoded record, exactly as an invalid mesh is, and contributes to no count.
void test_invalid_section() {
    const Fixture fixture = make_fixture(fixture_meshes(),
                                         one_broken_section());
    check_eq_u64(fixture.objects.sections.size(), 2,
                 "both sections are still reported by the parser");
    check(fixture.objects.sections[0].valid, "the first is valid");
    check(!fixture.objects.sections[1].valid, "and the second is not");

    nfsu::TrackCullOptions options;
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the probe runs over a refused section", run.error);
    if (!run.ok) {
        return;
    }
    check_eq_u64(run.probe.cost.disagreements, 0, "the culler agrees");
    check_eq_u64(run.probe.sections.size(), 2, "both are reported");
    check(!run.probe.sections[1].valid, "the second is marked invalid");
    check_eq_u64(run.probe.sections[1].probed, 0, "nothing of it is probed");
    check_eq_u64(run.probe.sections[1].instances, 0,
                 "a refused section carries no decoded instance");
    check_eq_u64(run.probe.cost.sections, 2, "the section total counts both");
    check_eq_u64(run.probe.cost.valid_sections, 1, "and the valid one is one");
    check_eq_u64(run.probe.cost.instances, 7,
                 "the instance total covers the valid section alone");
    check_eq_u64(run.probe.cost.probed, 7, "and so does the probed count");
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        check_eq_u64(probe.section_index, 0,
                     "no record comes from the refused section");
    }

    // Selecting it is refused rather than answered with an empty report.
    nfsu::TrackProbeSelection broken;
    broken.has_section = true;
    broken.section_index = 1;
    const Run refused = probe_run(fixture, street_camera(), options, broken);
    check_fails(refused.ok, refused.error, "is not valid",
                "selecting a refused section is refused");
}

// A section whose bounding tree fails containment degrades to the flat test,
// and the probe must follow the policy the culler ACTUALLY used rather than
// the one it was asked for. src/track_objects.cpp refuses such a tree at
// parse time, so this tampers with the decoded report, which is what
// tests/track_draw_list_tests.cpp does for the same reason.
void test_degraded_section() {
    Fixture fixture = town();
    check(fixture.objects.sections[0].nodes.size() >= 3,
          "the fixture tree has a node to shrink");
    if (fixture.objects.sections[0].nodes.size() < 3) {
        return;
    }
    // Node 2 covers instances 4..6 at x = 20, 25 and 30; shrinking it makes
    // it stop containing them.
    fixture.objects.sections[0].nodes[2].bounds_max[0] = 21;

    nfsu::TrackCullOptions options;  // retail_tree, verify_containment on
    const Run run = probe_run(fixture, street_camera(), options);
    check_ok(run.ok, "the degraded probe runs", run.error);
    if (!run.ok) {
        return;
    }
    check(!run.cull.policy_held, "the culler reports the policy did not hold");
    check_eq_u64(run.probe.sections.size(), 1, "one section");
    const nfsu::TrackSectionProbe& section = run.probe.sections[0];
    check(section.policy_used == nfsu::TrackCullPolicy::flat_instance_test,
          "the probe follows the policy the culler used, not the one asked "
          "for");
    check(section.containment_checked && !section.containment_holds,
          "and reports that containment is what broke");
    check(!section.tree_walked, "no tree is walked under the flat rule");
    check_eq_u64(run.probe.cost.pruned, 0, "a flat test prunes nothing");
    check_eq_u64(run.probe.cost.disagreements, 0,
                 "and the probe agrees with the culler all the same");
    check_eq_text(join(probe_visible_set(run.probe)),
                  join(culler_visible_set(run.cull)),
                  "over the same visible set");
    for (const nfsu::TrackInstanceProbe& probe : run.probe.instances) {
        check(probe.policy_used == nfsu::TrackCullPolicy::flat_instance_test,
              "every record carries the degraded policy");
        check_eq_u64(probe.pruned_by_node, nfsu::kTrackProbeNone,
                     "and none of them was pruned");
    }
}

// A tree the CULLER walked and this probe cannot.
//
// Every refusal inside the probe's own tree walk is unreachable from a
// well-formed pair of reports, because cull_track_region applies the same
// shape rules before it walks: a tree this probe would refuse makes the
// culler degrade to the flat test, and then the probe never walks a tree at
// all. The branches are reachable only the way they are reached here, which
// is also the way a caller could reach them - probe_track_scene is public and
// takes two reports it did not produce, so it has to answer when they
// disagree rather than read past the end of a vector.
//
// Each case below culls with a sound tree and then breaks the DECODED
// section, so the cull report still says retail_tree.
void test_tree_the_probe_cannot_walk() {
    struct Case {
        const char* what;
        void (*damage)(nfsu::TrackSection&);
        const char* needle;
    };
    const std::vector<Case> cases = {
        {"a node with no entry",
         [](nfsu::TrackSection& section) { section.nodes[1].entry_count = 0; },
         "entry count of 0"},
        {"a node with more entries than it has room for",
         [](nfsu::TrackSection& section) { section.nodes[1].entry_count = 6; },
         "entry count of 6"},
        // Exactly one past the end, because the fixture holds three nodes:
        // an index of 3 is out of range and an index of 9 would be caught by
        // a bound that was one too loose as well.
        {"a child index one past the end of the tree",
         [](nfsu::TrackSection& section) { section.nodes[0].entries[0] = -3; },
         "outside the tree or already reached"},
        {"one node named by two parents",
         [](nfsu::TrackSection& section) { section.nodes[0].entries[1] = -1; },
         "outside the tree or already reached"},
        {"an instance index one past the end of the array",
         [](nfsu::TrackSection& section) { section.nodes[1].entries[0] = 7; },
         "outside the array or already reached"},
        {"one instance named twice",
         [](nfsu::TrackSection& section) { section.nodes[1].entries[0] = 1; },
         "outside the array or already reached"},
    };

    for (const Case& item : cases) {
        Fixture fixture = town();
        nfsu::TrackCullOptions options;  // retail_tree
        nfsu::TrackCamera cull_camera;
        nfsu::TrackFrustum frustum;
        std::string error;
        check_ok(nfsu::track_cull_camera_of(street_camera(), cull_camera, error),
                 "the camera converts", error);
        check_ok(nfsu::track_frustum_look_at(cull_camera, frustum),
                 "the frustum builds", error);
        const nfsu::TrackCullReport cull =
            nfsu::cull_track_region(fixture.objects, frustum, options);
        check(cull.policy_held,
              std::string(item.what) + ": the culler walked the sound tree");

        item.damage(fixture.objects.sections[0]);

        nfsu::TrackProbeOptions probe_options;
        probe_options.cull = options;
        nfsu::TrackProbeReport report;
        std::string message;
        const bool ok = nfsu::probe_track_scene(
            fixture.geometry, fixture.objects, fixture.scene, cull, frustum,
            probe_options, report, message);
        check_ok(ok, std::string(item.what) + ": the probe still reports",
                 message);
        if (!ok) {
            continue;
        }
        check_eq_u64(report.sections.size(), 1,
                     std::string(item.what) + ": one section");
        if (report.sections.empty()) {
            continue;
        }
        check(!report.sections[0].tree_walked,
              std::string(item.what) + ": the probe did not walk the tree");
        check_eq_u64(report.issues.size(), 1,
                     std::string(item.what) + ": exactly one issue");
        if (!report.issues.empty()) {
            const std::string& text = report.issues[0].message;
            check(text.find("this probe cannot") != std::string::npos,
                  std::string(item.what) + ": the issue says the probe cannot "
                  "walk it, and reads '" + text + "'");
            check(text.find(item.needle) != std::string::npos,
                  std::string(item.what) + ": the issue names the reason, and "
                  "reads '" + text + "'");
        }
        check_eq_u64(report.cost.disagreements, 1,
                     std::string(item.what) + ": one disagreement");
        check(!report.ok(), std::string(item.what) + ": the report is not ok");
        // With no tree of its own the probe carries the culler's answer
        // rather than inventing one, so the two sets still agree and no
        // per-instance issue is added on top of the section's.
        check_eq_text(join(probe_visible_set(report)),
                      join(culler_visible_set(cull)),
                      std::string(item.what) +
                          ": the culler's visible set is carried");
        for (const nfsu::TrackInstanceProbe& probe : report.instances) {
            check_eq_u64(probe.node_index, nfsu::kTrackProbeNone,
                         std::string(item.what) + ": no node is reported");
            check(probe.mask_gate == nfsu::TrackGateVerdict::not_applied,
                  std::string(item.what) + ": and no gate verdict either");
        }
    }

    // The same shape one level up: a tree that is sound but needs a deeper
    // pending stack than the limits allow.
    Fixture fixture = town();
    nfsu::TrackCullOptions options;
    nfsu::TrackCamera cull_camera;
    nfsu::TrackFrustum frustum;
    std::string error;
    check_ok(nfsu::track_cull_camera_of(street_camera(), cull_camera, error),
             "the camera converts", error);
    check_ok(nfsu::track_frustum_look_at(cull_camera, frustum),
             "the frustum builds", error);
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, options);
    nfsu::TrackProbeOptions tight;
    tight.cull = options;
    tight.cull.limits.max_pending_nodes = 1;
    nfsu::TrackProbeReport report;
    std::string message;
    check_ok(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                     fixture.scene, cull, frustum, tight,
                                     report, message),
             "a tight pending-node limit still reports", message);
    check_eq_u64(report.issues.size(), 1, "and raises one issue");
    if (!report.issues.empty()) {
        check(report.issues[0].message.find("pending nodes") !=
                  std::string::npos,
              "which names the pending-node limit, and reads '" +
                  report.issues[0].message + "'");
    }
    check(!report.sections.empty() && !report.sections[0].tree_walked,
          "and no tree was walked");

    // The boundary the limit sits on: two pending nodes are enough for this
    // tree, so a `>=` that became `>` and a `>` that became `>=` are not the
    // same edit.
    nfsu::TrackProbeOptions exact;
    exact.cull = options;
    exact.cull.limits.max_pending_nodes = 2;
    nfsu::TrackProbeReport wide;
    check_ok(nfsu::probe_track_scene(fixture.geometry, fixture.objects,
                                     fixture.scene, cull, frustum, exact, wide,
                                     message),
             "two pending nodes are enough", message);
    check_eq_u64(wide.issues.size(), 0, "so nothing is raised");
    check(!wide.sections.empty() && wide.sections[0].tree_walked,
          "and the tree is walked");
}

void test_names() {
    check_eq_text(nfsu::track_residency_reason_name(
                      nfsu::TrackResidencyReason::every_section),
                  "every-section", "the every-section name");
    check_eq_text(nfsu::track_residency_reason_name(
                      nfsu::TrackResidencyReason::sub_index_in_range),
                  "sub-index-in-range", "the in-range name");
    check_eq_text(nfsu::track_residency_reason_name(
                      nfsu::TrackResidencyReason::sub_index_out_of_range),
                  "sub-index-out-of-range", "the out-of-range name");
    check_eq_text(nfsu::track_residency_reason_name(
                      nfsu::TrackResidencyReason::always_resident_band),
                  "always-resident-band", "the band name");
    check_eq_text(nfsu::track_residency_reason_name(
                      nfsu::TrackResidencyReason::zone_list_entry),
                  "zone-list-entry", "the zone entry name");
    check_eq_text(nfsu::track_residency_reason_name(
                      nfsu::TrackResidencyReason::not_in_zone_list),
                  "not-in-zone-list", "the absent name");
    check_eq_text(nfsu::track_gate_verdict_name(nfsu::TrackGateVerdict::passed),
                  "passed", "the passed name");
    check_eq_text(
        nfsu::track_gate_verdict_name(nfsu::TrackGateVerdict::rejected),
        "rejected", "the rejected name");
    check_eq_text(
        nfsu::track_gate_verdict_name(nfsu::TrackGateVerdict::not_applied),
        "not-applied", "the not-applied name");
}

// The probe is a function of its inputs: two runs over the same reports give
// the same records in the same order with the same numbers.
void test_determinism() {
    const Fixture fixture = town();
    nfsu::TrackCullOptions options;
    const Run first = probe_run(fixture, street_camera(), options);
    const Run second = probe_run(fixture, street_camera(), options);
    check_ok(first.ok && second.ok, "both runs succeed", first.error);
    if (!first.ok || !second.ok) {
        return;
    }
    check_eq_u64(first.probe.instances.size(), second.probe.instances.size(),
                 "the two runs report the same instances");
    for (std::size_t i = 0;
         i < first.probe.instances.size() && i < second.probe.instances.size();
         ++i) {
        const nfsu::TrackInstanceProbe& a = first.probe.instances[i];
        const nfsu::TrackInstanceProbe& b = second.probe.instances[i];
        check_eq_u64(a.instance_index, b.instance_index,
                     "the same instance at the same position");
        check_eq_u64(a.node_index, b.node_index, "the same node");
        check(a.visible == b.visible, "the same verdict");
        check(a.own_box_class == b.own_box_class, "the same box class");
        for (int k = 0; k < 3; ++k) {
            if (a.drawn_world.valid && b.drawn_world.valid) {
                check_exact(a.drawn_world.min[k], b.drawn_world.min[k],
                            "the same drawn box, bit for bit");
                check_exact(a.drawn_world.max[k], b.drawn_world.max[k],
                            "the same drawn box, bit for bit");
            }
        }
    }
}

} // namespace

int main() {
    test_camera_validation();
    test_camera_basis();
    test_default_camera_is_the_framing();
    test_camera_moves();
    test_cull_camera_conversion();
    test_source_provenance();
    test_bounds_agree_with_the_draw_list();
    test_materials();
    test_residency_reasons();
    test_tree_provenance();
    test_other_policies();
    test_mask_gate();
    test_the_box_test_a_node_makes_unnecessary();
    test_screen_size_gate();
    test_selection();
    test_two_sections();
    test_invalid_section();
    test_degraded_section();
    test_tree_the_probe_cannot_walk();
    test_mismatched_inputs();
    test_disagreement_is_reported();
    test_names();
    test_determinism();

    if (failures != 0) {
        std::cerr << failures << " track probe checks failed\n";
        return 1;
    }
    std::cout << "track probe tests passed\n";
    return 0;
}
