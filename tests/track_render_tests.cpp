#include "nfsu/geometry.h"
#include "nfsu/track_render.h"
#include "nfsu/track_reflections.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// The track backend of the CPU reference renderer.
//
// The fixtures go through the real parsers over byte images written at
// absolute offsets, exactly as tests/track_draw_list_tests.cpp does, so
// every alignment rule of docs/TRACK.md and docs/TRACK_OBJECTS.md applies
// and the vertex field order, the int16 instance matrix and the material
// tiling rule are all exercised rather than assumed. Building a
// PreparedTrackScene by hand would test the renderer against this file's
// idea of a scene instead of against the one the parsers produce.

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

void check_close(double got, double want, double tolerance,
                 const std::string& what) {
    const double delta = got - want;
    check(delta <= tolerance && -delta <= tolerance,
          what + ": got " + std::to_string(got) + ", want " +
              std::to_string(want) + " +- " + std::to_string(tolerance));
}

void check_exact(double got, double want, const std::string& what) {
    std::ostringstream message;
    message << std::setprecision(17) << what << ": got " << got
            << ", want " << want;
    check(got == want, message.str());
}

// The same as check, for a call that reports through an out-parameter.
//
// check(f(..., error), what + ": " + error) reads correctly and reports the
// WRONG string: both arguments of check are evaluated before it runs, and
// nothing orders the concatenation after the call, so the message carries
// whatever `error` held BEFORE f set it. Binding `error` to a reference
// here is not reading it; the body reads its value once the call returned.
void check_ok(bool ok, const std::string& what, const std::string& error) {
    check(ok, ok ? what : what + ": " + error);
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

// One vertex, in the FILE slot order. src/track.cpp:1031 reads the three
// position floats as px, pz, py in that order, so a fixture that wrote them
// in the struct's declaration order would place every vertex in the wrong
// plane and the renderer's placement would be tested against a matching
// mistake.
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
};

constexpr std::int16_t kOne = static_cast<std::int16_t>(nfsu::kSceneryMatrixOne);

struct InstanceSpec {
    std::int16_t bounds_min[3] = {-2, -2, -2};
    std::int16_t bounds_max[3] = {2, 2, 2};
    std::uint16_t object_index = 0;
    std::uint16_t flags = 0;
    // The record's three floats, in the raw slot order (px, pz, py).
    float position[3] = {0.0F, 0.0F, 0.0F};
    // The nine int16 the loader divides by kSceneryMatrixOne, row-major.
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

Bytes build_sections(const std::vector<SectionSpec>& sections, std::uint64_t base) {
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
// A flat colour with a known alpha, so an alpha-cutoff case has an exact
// boundary and every sampled texel gives the same RGB whatever the UVs are.
constexpr unsigned char kFlatR = 255;
constexpr unsigned char kFlatG = 120;
// 255 and not a middle value: (texel * shade + 128) >> 8 is a rounding, and
// for a texel of 60 the shades 89 and 90 produce the same byte, so a
// dropped + 0.5 in the shade factor would be unobservable. At 255 the two
// shades differ, which is what makes the unlit pixel below able to fail.
constexpr unsigned char kFlatB = 180;
constexpr unsigned char kFlatA = 128;

nfsu::RenderTexture flat_texture(std::uint32_t hash) {
    nfsu::RenderTexture texture;
    texture.hash = hash;
    texture.claims[0] = hash;
    texture.claim_count = 1;
    texture.width = 2;
    texture.height = 2;
    texture.rgba.clear();
    for (int i = 0; i < 4; ++i) {
        texture.rgba.push_back(kFlatR);
        texture.rgba.push_back(kFlatG);
        texture.rgba.push_back(kFlatB);
        texture.rgba.push_back(kFlatA);
    }
    texture.valid = true;
    return texture;
}

// Four distinct opaque texels, so a sampling change - a swapped u and v, a
// flipped v, a wrong row stride - moves colour around the frame.
nfsu::RenderTexture corner_texture(std::uint32_t hash) {
    nfsu::RenderTexture texture;
    texture.hash = hash;
    texture.claims[0] = hash;
    texture.claim_count = 1;
    texture.width = 2;
    texture.height = 2;
    static const unsigned char kTexels[4][3] = {
        {200, 0, 0}, {0, 200, 0}, {0, 0, 200}, {200, 200, 0}};
    texture.rgba.clear();
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) {
            texture.rgba.push_back(kTexels[i][k]);
        }
        texture.rgba.push_back(255);
    }
    texture.valid = true;
    return texture;
}

nfsu::RenderTextureSet two_textures() {
    nfsu::RenderTextureSet set;
    set.textures.push_back(corner_texture(kTexA));
    set.textures.push_back(flat_texture(kTexB));
    for (const nfsu::RenderTexture& texture : set.textures) {
        set.decoded_bytes += texture.rgba.size();
    }
    return set;
}

// ---------------------------------------------------------------------------
// The fixture: two meshes, one section, four instances.
//
//   mesh 0  PANEL  hash 0x3001  one group of 2 triangles, texture kTexA
//   mesh 1  POST   hash 0x3002  two groups of 1 triangle, kTexA then kTexB
//
// PANEL is a unit square in the world X-Y plane facing world +Z; POST is a
// pair of triangles in the same plane, offset, so a camera on +Z sees both
// without either occluding the other.
//
// The section places, in order: PANEL at the origin, PANEL again three
// units along world +X, POST three units along world -X, and one instance
// of a hash no mesh claims, which places nothing.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kPanel = 0x3001U;
constexpr std::uint32_t kPost = 0x3002U;
constexpr std::uint32_t kMissing = 0x00007777U;
constexpr std::uint64_t kGeometryBase = 0;
constexpr std::uint64_t kSceneryBase = 0x2000;

// One vertex of a square in the world X-Y plane at world z = 0. The world
// map is x = px, y = py, z = -pz, so pz stays zero and px and py carry the
// square; writing it any other way would put the square in a plane the
// camera below does not face.
VertexSpec plane_vertex(float x, float y, float u, float v) {
    VertexSpec vertex;
    vertex.px = x;
    vertex.pz = 0.0F;
    vertex.py = y;
    vertex.u = u;
    vertex.v = v;
    vertex.color[0] = 255;
    vertex.color[1] = 128;
    vertex.color[2] = 64;
    vertex.color[3] = 255;
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
    post.texture_hashes = {kTexA, kTexB};

    return {panel, post};
}

InstanceSpec placed(std::uint16_t object_index, float slot0, float slot1,
                    float slot2) {
    InstanceSpec instance;
    instance.object_index = object_index;
    instance.position[0] = slot0;
    instance.position[1] = slot1;
    instance.position[2] = slot2;
    // The stored box, in the raw slot order, around the translation. The
    // fixture keeps it honest rather than tight: the renderer never reads
    // it, and track_draw_list_bounds reports it beside the drawn box.
    for (int k = 0; k < 3; ++k) {
        const float centre[3] = {slot0, slot1, slot2};
        instance.bounds_min[k] = static_cast<std::int16_t>(centre[k] - 2.0F);
        instance.bounds_max[k] = static_cast<std::int16_t>(centre[k] + 2.0F);
    }
    instance.flags = static_cast<std::uint16_t>(0x1000U + object_index);
    return instance;
}

std::vector<SectionSpec> fixture_sections() {
    SectionSpec section;
    section.number = 303;
    section.objects.resize(3);
    section.objects[0].mesh_hash[0] = kPanel;
    section.objects[1].mesh_hash[0] = kPost;
    section.objects[2].mesh_hash[0] = kMissing;
    section.instances.push_back(placed(0, 0.0F, 0.0F, 0.0F));
    section.instances.push_back(placed(0, 3.0F, 0.0F, 0.0F));
    section.instances.push_back(placed(1, -3.0F, 0.0F, 0.0F));
    section.instances.push_back(placed(2, 6.0F, 0.0F, 0.0F));

    NodeSpec node;
    node.bounds_min[0] = -6;
    node.bounds_min[1] = -3;
    node.bounds_min[2] = -3;
    node.bounds_max[0] = 9;
    node.bounds_max[1] = 3;
    node.bounds_max[2] = 3;
    node.entry_count = 4;
    node.entries[0] = 0;
    node.entries[1] = 1;
    node.entries[2] = 2;
    node.entries[3] = 3;
    section.nodes.push_back(node);

    return {section};
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

Fixture town() { return make_fixture(fixture_meshes(), fixture_sections()); }

// A second fixture: one flat-textured quad, placed by an instance matrix
// that is NOT the identity, so the frame depends on the stored int16
// rotation as well as on the translation.
//
// The rotation is 45 degrees about the world X axis, written in the raw
// slot frame. It is here for the shading: with the quad facing the camera
// square on its normal is (0, 0, 1), and the shade factor of a (0, 0, 1)
// normal is the same whether the fixed light is normalized or not. Tilted,
// the normal is (0, 1, 1) / sqrt(2) and the two answers differ by one in
// 256, which is the whole difference the normalization makes.
constexpr std::int16_t kHalfRoot2 = 5793;  // round(kSceneryMatrixOne / sqrt(2))

std::vector<MeshSpec> tilted_meshes() {
    MeshSpec quad;
    quad.name = "TILT";
    quad.hash = kPanel;
    quad.vertices = {plane_vertex(-1.0F, -1.0F, 0.0F, 0.0F),
                     plane_vertex(1.0F, -1.0F, 1.0F, 0.0F),
                     plane_vertex(1.0F, 1.0F, 1.0F, 1.0F),
                     plane_vertex(-1.0F, 1.0F, 0.0F, 1.0F)};
    quad.triangles = {{0, 1, 2}, {0, 2, 3}};
    quad.materials = {MaterialSpec{6, 0}};
    quad.texture_hashes = {kTexB};
    return {quad};
}

std::vector<SectionSpec> tilted_sections() {
    SectionSpec section;
    section.number = 303;
    section.objects.resize(1);
    section.objects[0].mesh_hash[0] = kPanel;
    InstanceSpec instance = placed(0, 0.0F, 0.0F, 0.0F);
    instance.linear[0] = kOne;
    instance.linear[1] = 0;
    instance.linear[2] = 0;
    instance.linear[3] = 0;
    instance.linear[4] = kHalfRoot2;
    instance.linear[5] = static_cast<std::int16_t>(-kHalfRoot2);
    instance.linear[6] = 0;
    instance.linear[7] = kHalfRoot2;
    instance.linear[8] = kHalfRoot2;
    section.instances.push_back(instance);

    NodeSpec node;
    node.entry_count = 1;
    node.entries[0] = 0;
    section.nodes.push_back(node);
    return {section};
}

Fixture tilted() {
    return make_fixture(tilted_meshes(), tilted_sections());
}

Fixture floor() {
    auto meshes = fixture_meshes();
    for (auto& vertex : meshes[0].vertices) {
        vertex.px *= 8;
        vertex.pz = -vertex.py * 8;
        vertex.py = 0;
    }
    for (auto& vertex : meshes[1].vertices) vertex.py += 2;
    auto fixture = make_fixture(meshes, fixture_sections());
    fixture.scene.region.meshes[0].vertex_format_flags = 0x40;
    fixture.scene.instances[2].instance_flags = 0x40;
    return fixture;
}

// A third fixture: one mesh whose first triangle has a DIFFERENT unique
// extreme on each of its three vertices, and a second triangle that is
// degenerate because two of its indices name the same vertex.
//
// The first half exists because the town quads put their extremes on the
// first and third vertex of each triangle, so reading the second index
// twice lost nothing and no bound moved. The second half exists because
// nothing else in this suite submits a triangle with no area, which is the
// only thing that reaches the degenerate-source counter and the guard on
// the normal's length.
std::vector<MeshSpec> probe_meshes() {
    MeshSpec mesh;
    mesh.name = "PROBE";
    mesh.hash = kPanel;
    mesh.vertices = {plane_vertex(-1.0F, -1.0F, 0.0F, 0.0F),
                     plane_vertex(2.0F, 0.0F, 1.0F, 0.5F),
                     plane_vertex(0.0F, 3.0F, 0.5F, 1.0F),
                     plane_vertex(-1.0F, -1.0F, 0.0F, 0.0F)};
    // Vertex 0 is the unique minimum on both axes, vertex 1 the unique
    // maximum on x, vertex 2 the unique maximum on y. Vertex 3 is a copy of
    // vertex 0, so the second triangle has two coincident corners.
    // The second triangle names vertices 2, 3 and 0 and NOT vertex 1, so
    // vertex 1 - the unique maximum on x - is named exactly once in the
    // whole mesh, by the SECOND index of the first triangle. A walk that
    // read any index twice in place of that one loses it.
    mesh.triangles = {{0, 1, 2}, {2, 3, 0}};
    mesh.materials = {MaterialSpec{6, 0}};
    mesh.texture_hashes = {kTexB};
    return {mesh};
}

std::vector<SectionSpec> probe_sections() {
    SectionSpec section;
    section.number = 303;
    section.objects.resize(1);
    section.objects[0].mesh_hash[0] = kPanel;
    InstanceSpec instance = placed(0, 0.0F, 0.0F, 0.0F);
    instance.bounds_min[0] = -2;
    instance.bounds_min[1] = -2;
    instance.bounds_min[2] = -2;
    instance.bounds_max[0] = 3;
    instance.bounds_max[1] = 2;
    instance.bounds_max[2] = 4;
    section.instances.push_back(instance);
    NodeSpec node;
    node.bounds_min[0] = -2;
    node.bounds_min[1] = -2;
    node.bounds_min[2] = -2;
    node.bounds_max[0] = 3;
    node.bounds_max[1] = 2;
    node.bounds_max[2] = 4;
    node.entry_count = 1;
    node.entries[0] = 0;
    section.nodes.push_back(node);
    return {section};
}

Fixture probe() { return make_fixture(probe_meshes(), probe_sections()); }

// A fourth fixture: the probe mesh placed by a 45-degree rotation about the
// world Y axis, so its world normal is (-1, 0, 1) / sqrt(2).
//
// Every other fixture has a normal whose x component is zero. That makes
// three separate faults invisible: the cross product's first component is
// then zero, so negating one edge component negates the whole normal and
// the flip towards the eye silently undoes it; and the x term of that flip
// is multiplied by zero, so getting its sign wrong changes nothing. A
// normal with a non-zero x component is the only fixture shape that can
// fail on any of the three.
std::vector<SectionSpec> yawed_sections() {
    SectionSpec section = probe_sections()[0];
    InstanceSpec& instance = section.instances[0];
    // A rotation about the WORLD Y axis mixes world x and world z, which
    // are raw slots 0 and 1; raw slot 2 is world y and is left alone. A
    // matrix that mixed slots 0 and 2 instead would turn the quad inside
    // the world XY plane and leave the normal's x component at zero, which
    // is the very thing this fixture exists to avoid.
    instance.linear[0] = kHalfRoot2;
    instance.linear[1] = kHalfRoot2;
    instance.linear[2] = 0;
    instance.linear[3] = static_cast<std::int16_t>(-kHalfRoot2);
    instance.linear[4] = kHalfRoot2;
    instance.linear[5] = 0;
    instance.linear[6] = 0;
    instance.linear[7] = 0;
    instance.linear[8] = kOne;
    // The rotated triangle reaches further than the unrotated box, and a
    // node that did not contain its entry is refused at parse time.
    const std::int16_t low[3] = {-4, -4, -4};
    const std::int16_t high[3] = {4, 4, 4};
    for (int k = 0; k < 3; ++k) {
        instance.bounds_min[k] = low[k];
        instance.bounds_max[k] = high[k];
        section.nodes[0].bounds_min[k] = low[k];
        section.nodes[0].bounds_max[k] = high[k];
    }
    return {section};
}

Fixture yawed() { return make_fixture(probe_meshes(), yawed_sections()); }

// Every placed instance, in section and instance order. The culler is not
// called: build_track_draw_list's primary entry point takes a plain visible
// set, which is the boundary docs/TRACK_DRAW_LIST.md settles, and a
// renderer test that reached for a frustum would be testing the wrong
// layer.
std::vector<nfsu::TrackVisibleInstance> all_visible(
    const nfsu::PreparedTrackScene& scene) {
    std::vector<nfsu::TrackVisibleInstance> visible;
    for (const nfsu::TrackSceneSection& section : scene.sections) {
        for (std::size_t i = 0; i < section.source_instances; ++i) {
            nfsu::TrackVisibleInstance entry;
            entry.section_index = section.section_index;
            entry.instance_index = i;
            visible.push_back(entry);
        }
    }
    return visible;
}

nfsu::TrackDrawList make_list(const nfsu::PreparedTrackScene& scene,
                              nfsu::TrackDrawOrder order) {
    nfsu::TrackDrawListOptions options;
    options.order = order;
    nfsu::TrackDrawList list;
    std::string error;
    if (!nfsu::build_track_draw_list(scene, all_visible(scene), options, list,
                                     error)) {
        check(false, "the fixture draw list builds: " + error);
    }
    return list;
}

// A list over named prepared instances, in the order named.
//
// Every list a render test uses is built by build_track_draw_list rather
// than assembled field by field. That is not tidiness: a hand-built list
// carries no cost statistics, validate_track_draw_list recomputes and
// compares them, and render_track_draw_list runs that validator - so a
// hand-built list is refused before a pixel is drawn. This helper is how a
// test names exactly which entries it wants and still gets a valid list.
nfsu::TrackDrawList list_over(const nfsu::PreparedTrackScene& scene,
                              const std::vector<std::size_t>& instances) {
    std::vector<nfsu::TrackVisibleInstance> visible;
    for (const std::size_t handle : instances) {
        nfsu::TrackVisibleInstance entry;
        entry.section_index = scene.instances[handle].section_index;
        entry.instance_index = scene.instances[handle].instance_index;
        visible.push_back(entry);
    }
    nfsu::TrackDrawListOptions options;
    options.order = nfsu::TrackDrawOrder::visible_instance;
    nfsu::TrackDrawList list;
    std::string error;
    if (!nfsu::build_track_draw_list(scene, visible, options, list, error)) {
        check(false, "the named draw list builds: " + error);
    }
    return list;
}

// Which prepared instance of `scene` places mesh `mesh`, first one wins.
std::size_t instance_of_mesh(const nfsu::PreparedTrackScene& scene,
                             std::size_t mesh) {
    for (std::size_t i = 0; i < scene.instances.size(); ++i) {
        if (scene.instances[i].mesh == mesh) {
            return i;
        }
    }
    check(false, "the fixture places mesh " + std::to_string(mesh));
    return 0;
}

// The pixel the renderer's own camera sends a world point to, by the exact
// arithmetic src/render.cpp uses. Computing it rather than writing a
// coordinate down keeps the test readable when the fixture or the camera
// moves, and keeps it honest: a hard-coded pixel can be corrected to match
// a broken projection without anyone noticing.
bool screen_pixel_of(const nfsu::OrbitCamera& camera, std::uint32_t width,
                     std::uint32_t height, const nfsu::Vec3& world,
                     std::uint32_t& out_x, std::uint32_t& out_y) {
    nfsu::CameraFrame frame;
    std::string error;
    if (!nfsu::make_camera_frame(camera, width, height, frame, error)) {
        return false;
    }
    const nfsu::ClipVertex clip = nfsu::project_point(frame, world);
    if (!(clip.w > 0.0)) {
        return false;
    }
    const double nx = clip.x / clip.w;
    const double ny = clip.y / clip.w;
    if (nx < -1.0 || nx > 1.0 || ny < -1.0 || ny > 1.0) {
        return false;
    }
    const double sx = (nx + 1.0) * 0.5 * static_cast<double>(width);
    const double sy = (1.0 - ny) * 0.5 * static_cast<double>(height);
    const std::int64_t px = static_cast<std::int64_t>(sx);
    const std::int64_t py = static_cast<std::int64_t>(sy);
    if (px < 0 || py < 0 || px >= static_cast<std::int64_t>(width) ||
        py >= static_cast<std::int64_t>(height)) {
        return false;
    }
    out_x = static_cast<std::uint32_t>(px);
    out_y = static_cast<std::uint32_t>(py);
    return true;
}

// A camera on the world +Z axis looking at the origin: yaw 90 puts the eye
// at +Z, pitch 0 keeps it level, so the fixture's X-Y plane faces it square
// on and a texel lands on a predictable pixel.
nfsu::OrbitCamera facing_camera(double distance) {
    nfsu::OrbitCamera camera;
    camera.yaw_deg = 90.0;
    camera.pitch_deg = 0.0;
    camera.distance = distance;
    camera.target = nfsu::Vec3{0.0, 0.0, 0.0};
    camera.fov_y_deg = 45.0;
    camera.near_plane = 0.1;
    camera.far_plane = 100.0;
    return camera;
}

nfsu::RenderOptions small_options() {
    nfsu::RenderOptions options;
    options.width = 160;
    options.height = 120;
    options.shading = nfsu::ShadingMode::none;
    options.camera = facing_camera(6.0);
    options.background[0] = 10;
    options.background[1] = 20;
    options.background[2] = 30;
    options.background[3] = 255;
    return options;
}

std::array<unsigned char, 4> pixel_at(const nfsu::Frame& frame, std::uint32_t x,
                                      std::uint32_t y) {
    const std::size_t index = (std::size_t{y} * frame.width() + x) * 4;
    return {frame.rgba()[index], frame.rgba()[index + 1],
            frame.rgba()[index + 2], frame.rgba()[index + 3]};
}

std::string describe_pixel(const std::array<unsigned char, 4>& pixel) {
    return std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) + "," +
           std::to_string(pixel[2]) + "," + std::to_string(pixel[3]);
}

// ---------------------------------------------------------------------------
// The fixture itself
// ---------------------------------------------------------------------------

void test_fixture_parses() {
    const Fixture fixture = town();
    check(fixture.geometry.ok(), "the geometry fixture parses with no issue");
    check_eq_u64(fixture.geometry.valid_mesh_count(), 2,
                 "the geometry fixture holds two valid meshes");
    check(fixture.objects.ok(), "the scenery fixture parses with no issue");
    check_eq_u64(fixture.objects.cost.instances, 4,
                 "the scenery fixture places four instances");
    check_eq_u64(fixture.scene.instances.size(), 3,
                 "three of the four instances resolve a mesh");
    check_eq_u64(fixture.scene.cost.unplaced_unresolved, 1,
                 "the fourth names a hash no mesh claims");
    check_eq_u64(fixture.scene.region.draws.size(), 3,
                 "the fixture region holds three material groups");

    // The vertex field order. src/track.cpp reads px, pz, py from the file
    // in that order; a fixture or a parser that swapped the last two would
    // put the square in a different plane and every later expectation would
    // be measured against the swap.
    const nfsu::PreparedTrackMesh& panel = fixture.scene.region.meshes[0];
    check_eq_u64(panel.vertices.size(), 4, "PANEL has four vertices");
    check_exact(panel.vertices[0].px, -1.0, "PANEL vertex 0 px");
    check_exact(panel.vertices[0].py, -1.0, "PANEL vertex 0 py");
    check_exact(panel.vertices[0].pz, 0.0, "PANEL vertex 0 pz");
    check_exact(panel.vertices[2].px, 1.0, "PANEL vertex 2 px");
    check_exact(panel.vertices[2].py, 1.0, "PANEL vertex 2 py");
}

// ---------------------------------------------------------------------------
// track_placed_vertex
// ---------------------------------------------------------------------------

nfsu::TrackVertex raw_vertex(float px, float py, float pz) {
    nfsu::TrackVertex vertex;
    vertex.px = px;
    vertex.py = py;
    vertex.pz = pz;
    return vertex;
}

void test_placed_vertex_identity() {
    nfsu::TrackTransform identity;
    identity.linear[0] = 1.0F;
    identity.linear[4] = 1.0F;
    identity.linear[8] = 1.0F;
    // Three distinct magnitudes, so a swap of any two axes shows.
    const nfsu::Vec3 world =
        nfsu::track_placed_vertex(raw_vertex(2.0F, 5.0F, 11.0F), identity);
    check_exact(world.x, 2.0, "identity places px on world x");
    check_exact(world.y, 5.0, "identity places py on world y");
    check_exact(world.z, -11.0, "identity negates pz onto world z");
}

void test_placed_vertex_matches_the_culler_map() {
    // docs/FORMATS.md:286 is stated once, in track_world_from_raw, and used
    // by the culler. This is the check that the renderer did not write a
    // second copy of it that drifts.
    nfsu::TrackTransform identity;
    identity.linear[0] = 1.0F;
    identity.linear[4] = 1.0F;
    identity.linear[8] = 1.0F;
    static const float kPoints[5][3] = {{0.0F, 0.0F, 0.0F},
                                        {1.0F, 2.0F, 3.0F},
                                        {-4.0F, 5.5F, -6.25F},
                                        {100.0F, -0.5F, 7.0F},
                                        {-9.0F, -8.0F, -7.0F}};
    for (int i = 0; i < 5; ++i) {
        const float px = kPoints[i][0];
        const float py = kPoints[i][1];
        const float pz = kPoints[i][2];
        // track_world_from_raw takes the RAW triple, which is (px, pz, py).
        const float raw[3] = {px, pz, py};
        float expected[3] = {0.0F, 0.0F, 0.0F};
        nfsu::track_world_from_raw(raw, expected);
        const nfsu::Vec3 world =
            nfsu::track_placed_vertex(raw_vertex(px, py, pz), identity);
        const std::string what = "point " + std::to_string(i);
        check_exact(world.x, expected[0], what + " world x");
        check_exact(world.y, expected[1], what + " world y");
        check_exact(world.z, expected[2], what + " world z");
    }
}

void test_placed_vertex_is_row_major_row_vector() {
    // A permutation, so a transposed read gives a different answer for
    // every input rather than agreeing by symmetry. Row-major, row-vector:
    // moved[j] = sum over i of slot[i] * linear[i * 3 + j].
    nfsu::TrackTransform transform;
    transform.linear[0] = 0.0F;
    transform.linear[1] = 1.0F;
    transform.linear[2] = 0.0F;
    transform.linear[3] = 0.0F;
    transform.linear[4] = 0.0F;
    transform.linear[5] = 1.0F;
    transform.linear[6] = 1.0F;
    transform.linear[7] = 0.0F;
    transform.linear[8] = 0.0F;
    // slot = (px, pz, py) = (2, 11, 5); moved = (5, 2, 11);
    // world = (moved[0], moved[2], -moved[1]) = (5, 11, -2).
    const nfsu::Vec3 world =
        nfsu::track_placed_vertex(raw_vertex(2.0F, 5.0F, 11.0F), transform);
    check_exact(world.x, 5.0, "row-major row-vector world x");
    check_exact(world.y, 11.0, "row-major row-vector world y");
    check_exact(world.z, -2.0, "row-major row-vector world z");
}

void test_placed_vertex_adds_the_translation() {
    nfsu::TrackTransform transform;
    transform.linear[0] = 2.0F;
    transform.linear[4] = 2.0F;
    transform.linear[8] = 2.0F;
    transform.translation[0] = 10.0F;
    transform.translation[1] = 20.0F;
    transform.translation[2] = 30.0F;
    // slot = (1, 3, 2); moved = (2 + 10, 6 + 20, 4 + 30) = (12, 26, 34);
    // world = (12, 34, -26).
    const nfsu::Vec3 world =
        nfsu::track_placed_vertex(raw_vertex(1.0F, 2.0F, 3.0F), transform);
    check_exact(world.x, 12.0, "translated world x");
    check_exact(world.y, 34.0, "translated world y");
    check_exact(world.z, -26.0, "translated world z");
}

void test_scene_transform_divides_the_stored_matrix() {
    // The whole path, from the file's int16 to a world position: the
    // fixture writes kSceneryMatrixOne on the diagonal and a translation of
    // 3 in raw slot 0, and the vertex at px -1 must land at world x 2.
    const Fixture fixture = town();
    const nfsu::PreparedTrackInstance& second = fixture.scene.instances[1];
    check_exact(second.transform.linear[0], 1.0,
                "the stored matrix divides by kSceneryMatrixOne");
    check_exact(second.transform.translation[0], 3.0,
                "the second instance is translated by three in raw slot 0");
    const nfsu::Vec3 world = nfsu::track_placed_vertex(
        fixture.scene.region.meshes[second.mesh].vertices[0], second.transform);
    check_exact(world.x, 2.0, "the translated vertex lands at world x 2");
    check_exact(world.y, -1.0, "the translated vertex keeps world y");
    check_exact(world.z, 0.0, "the translated vertex keeps world z");
}

// ---------------------------------------------------------------------------
// track_draw_list_bounds
// ---------------------------------------------------------------------------

void test_bounds_are_exact() {
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::TrackDrawListBounds bounds;
    std::string error;
    check_ok(nfsu::track_draw_list_bounds(fixture.scene, list, bounds, error),
              "the fixture bounds are measurable", error);
    check(bounds.drawn.valid, "the drawn bounds hold a point");
    // PANEL spans x -1..1 at the origin and 2..4 three units along; POST
    // spans -3.5..-2.5. The union is -3.5..4 by -1..1 by 0..0.
    check_exact(bounds.drawn.min[0], -3.5, "drawn min x");
    check_exact(bounds.drawn.max[0], 4.0, "drawn max x");
    check_exact(bounds.drawn.min[1], -1.0, "drawn min y");
    check_exact(bounds.drawn.max[1], 1.0, "drawn max y");
    check_exact(bounds.drawn.min[2], 0.0, "drawn min z");
    check_exact(bounds.drawn.max[2], 0.0, "drawn max z");
    // Three vertices per triangle of every entry, and the entries hold
    // 2 + 2 + 1 + 1 triangles.
    check_eq_u64(bounds.drawn.points, 18, "drawn points visited");
    check_eq_u64(bounds.instances, 3, "instances the list names");
    check_eq_u64(bounds.instances_without_vertices, 0,
                 "every named instance contributed a point");
}

void test_stored_bounds_exchange_the_negated_axis() {
    // The world z interval of a raw box is the NEGATED raw slot 1 interval
    // with its ends exchanged. Writing it the obvious way gives min > max
    // and a box that contains nothing, so the fixture uses an instance
    // whose raw slot 1 box is not symmetric about zero.
    std::vector<SectionSpec> sections = fixture_sections();
    sections[0].instances[0].bounds_min[1] = 1;
    sections[0].instances[0].bounds_max[1] = 7;
    // The same instance is also the sole minimum on raw slot 2, which is
    // world y. Without that, the other two instances supply the world y
    // minimum and a corner that read slot 1 where slot 2 belongs would be
    // masked by them.
    sections[0].instances[0].bounds_min[2] = -5;
    sections[0].instances[0].bounds_max[2] = -3;
    sections[0].nodes[0].bounds_min[1] = -5;
    sections[0].nodes[0].bounds_max[1] = 9;
    sections[0].nodes[0].bounds_min[2] = -5;
    sections[0].nodes[0].bounds_max[2] = 9;
    const Fixture fixture = make_fixture(fixture_meshes(), sections);
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::TrackDrawListBounds bounds;
    std::string error;
    check_ok(nfsu::track_draw_list_bounds(fixture.scene, list, bounds, error),
              "the skewed fixture bounds are measurable", error);
    check(bounds.stored.valid, "the stored bounds hold a point");
    check(bounds.stored.min[2] <= bounds.stored.max[2],
          "the stored world z interval is not inverted: got " +
              std::to_string(bounds.stored.min[2]) + ".." +
              std::to_string(bounds.stored.max[2]));
    // Every axis, not only the negated one. The two corners the map sends
    // the raw corners to are computed separately, so a slot order that is
    // right in one of them and wrong in the other shows only here.
    //
    // Instance 0 has raw box x -2..2, slot1 1..7, slot2 -5..-3 and sits
    // at the origin; instance 1 is the default box around raw slot0 3 and
    // instance 2 around raw slot0 -3. World x is raw slot 0, world y is
    // raw slot 2, world z is the negated raw slot 1. Instance 0 alone
    // supplies the world y minimum -5 and the world z minimum -7.
    check_exact(bounds.stored.min[0], -5.0, "stored min x");
    check_exact(bounds.stored.max[0], 5.0, "stored max x");
    check_exact(bounds.stored.min[1], -5.0, "stored min y");
    check_exact(bounds.stored.max[1], 2.0, "stored max y");
    check_exact(bounds.stored.min[2], -7.0, "stored min z");
    check_exact(bounds.stored.max[2], 2.0, "stored max z");
}

void test_bounds_refuse_a_mismatched_entry() {
    const Fixture fixture = town();
    nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    check(!list.entries.empty(), "the fixture list is not empty");

    // Each refusal is checked by the message it produces, not only by the
    // false it returns. Three separate checks guard these handles and a
    // later one catches what an earlier one lets through, so a test that
    // only asked for false would pass with any one of the three removed.
    nfsu::TrackDrawList bad = list;
    bad.entries[0].instance =
        static_cast<std::uint32_t>(fixture.scene.instances.size());
    nfsu::TrackDrawListBounds bounds;
    std::string error;
    check(!nfsu::track_draw_list_bounds(fixture.scene, bad, bounds, error),
          "an instance handle past the scene is refused");
    check(error.find("names instance") != std::string::npos,
          "and the instance check is what refused it: " + error);

    bad = list;
    bad.entries[0].draw =
        static_cast<std::uint32_t>(fixture.scene.region.draws.size());
    check(!nfsu::track_draw_list_bounds(fixture.scene, bad, bounds, error),
          "a group handle past the region is refused");
    check(error.find("names group") != std::string::npos,
          "and the group check is what refused it: " + error);

    // An entry whose group belongs to a different mesh than its instance.
    // The two meshes of this fixture have different groups, so pairing
    // instance 0 (PANEL) with POST's first group is exactly that.
    bad = list;
    std::uint32_t post_group = 0;
    bool found = false;
    for (std::size_t d = 0; d < fixture.scene.region.draws.size(); ++d) {
        if (fixture.scene.region.draws[d].mesh != fixture.scene.instances[0].mesh) {
            post_group = static_cast<std::uint32_t>(d);
            found = true;
            break;
        }
    }
    check(found, "the fixture has a group of the other mesh");
    bad.entries[0].instance = 0;
    bad.entries[0].draw = post_group;
    check(!nfsu::track_draw_list_bounds(fixture.scene, bad, bounds, error),
          "a group of another mesh is refused");
    check(error.find("and an instance of mesh") != std::string::npos,
          "and the mesh check is what refused it: " + error);

    // A group whose mesh handle is past the region, and a group whose index
    // range runs off the end of its mesh. Neither can come out of a parsed
    // region, and this function runs no validator, so both guards are its
    // own and both are one comparison from an out-of-range read.
    nfsu::PreparedTrackScene scene = fixture.scene;
    scene.region.draws[list.entries[0].draw].mesh =
        scene.region.meshes.size();
    scene.instances[list.entries[0].instance].mesh =
        scene.region.meshes.size();
    check(!nfsu::track_draw_list_bounds(scene, list, bounds, error),
          "a group naming a mesh past the region is refused");
    check(error.find("names a group of mesh") != std::string::npos,
          "and the mesh-range check is what refused it: " + error);

    scene = fixture.scene;
    nfsu::TrackDraw& draw = scene.region.draws[list.entries[0].draw];
    draw.index_count = static_cast<std::uint32_t>(
        scene.region.meshes[draw.mesh].triangles.size() * 3 + 3);
    check(!nfsu::track_draw_list_bounds(scene, list, bounds, error),
          "a group whose range runs past its mesh is refused");
    check(error.find("reaches triangle") != std::string::npos,
          "and the triangle-range check is what refused it: " + error);
}

void test_bounds_skip_a_triangle_with_a_bad_index() {
    // A triangle index at exactly the vertex count is one past the end.
    // No parsed region holds one - src/track.cpp refuses it and
    // validate_prepared_track_region would too - but this function runs
    // neither validator, so the guard is its own. A boundary tested only
    // from well inside it is a guard that can be moved by one without
    // anything noticing.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::PreparedTrackScene scene = fixture.scene;
    const std::size_t vertices = scene.region.meshes[0].vertices.size();
    // The FIRST triangle only. A walk that stopped at a bad index instead
    // of skipping it would lose the second as well, and a fixture with
    // every triangle broken cannot tell the two apart.
    scene.region.meshes[0].triangles[0].a =
        static_cast<std::uint16_t>(vertices);
    nfsu::TrackDrawListBounds bounds;
    std::string error;
    check_ok(nfsu::track_draw_list_bounds(scene, list, bounds, error),
             "the bad-index scene is measurable", error);
    // PANEL loses one triangle of two per copy; POST keeps both.
    check_eq_u64(bounds.drawn.points, 12,
                 "the bad triangle is skipped and the next one is not");
    check_eq_u64(bounds.instances_without_vertices, 0,
                 "every instance still contributed a vertex");

    // Now every triangle of every mesh, so that the LAST instance is one of
    // the ones contributing nothing: a loop one short of the end would
    // report one fewer.
    for (nfsu::PreparedTrackMesh& mesh : scene.region.meshes) {
        for (nfsu::MeshTriangle& triangle : mesh.triangles) {
            triangle.a = static_cast<std::uint16_t>(mesh.vertices.size());
        }
    }
    check_ok(nfsu::track_draw_list_bounds(scene, list, bounds, error),
             "the fully bad-index scene is measurable", error);
    check(!bounds.drawn.valid, "nothing contributed a point");
    check_eq_u64(bounds.instances, 3, "all three instances are still named");
    check_eq_u64(bounds.instances_without_vertices, 3,
                 "and all three are counted, the last one included");
}

void test_bounds_ignore_an_empty_group() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene = fixture.scene;
    // A group with no indices draws nothing, so the instance it names
    // contributes no vertex. The count that says so is what a caller reads
    // to tell "drew nothing" from "was not in the list".
    nfsu::TrackDrawList list;
    nfsu::TrackDrawListEntry entry;
    entry.instance = 0;
    entry.draw = 0;
    list.entries.push_back(entry);
    scene.region.draws[0].index_count = 0;
    nfsu::TrackDrawListBounds bounds;
    std::string error;
    check_ok(nfsu::track_draw_list_bounds(scene, list, bounds, error),
              "an empty group is measurable", error);
    check(!bounds.drawn.valid, "an empty group contributes no drawn point");
    check_eq_u64(bounds.instances, 1, "the instance is still named");
    check_eq_u64(bounds.instances_without_vertices, 1,
                 "and is counted as contributing no vertex");
}

// ---------------------------------------------------------------------------
// frame_track_bounds
// ---------------------------------------------------------------------------

nfsu::TrackWorldBounds box(double x0, double y0, double z0, double x1,
                           double y1, double z1) {
    nfsu::TrackWorldBounds bounds;
    bounds.valid = true;
    bounds.min[0] = x0;
    bounds.min[1] = y0;
    bounds.min[2] = z0;
    bounds.max[0] = x1;
    bounds.max[1] = y1;
    bounds.max[2] = z1;
    bounds.points = 8;
    return bounds;
}

// The property the whole rule exists for: every corner of the box is inside
// the frame. It is checked by projecting through the renderer's own camera,
// so a wrong aspect direction, a wrong half-angle or a missing margin fails
// here rather than being restated as arithmetic this file agrees with.
void check_box_is_framed(const nfsu::TrackWorldBounds& bounds,
                         std::uint32_t width, std::uint32_t height,
                         const nfsu::TrackFramingOptions& options,
                         const std::string& what) {
    nfsu::OrbitCamera camera;
    std::string error;
    if (!nfsu::frame_track_bounds(bounds, width, height, options, camera,
                                  error)) {
        check(false, what + ": framing failed: " + error);
        return;
    }
    check_ok(nfsu::validate_camera(camera, error),
             what + ": the framed camera validates", error);
    nfsu::CameraFrame frame;
    if (!nfsu::make_camera_frame(camera, width, height, frame, error)) {
        check(false, what + ": the framed camera makes a frame: " + error);
        return;
    }
    for (int corner = 0; corner < 8; ++corner) {
        const nfsu::Vec3 point{
            (corner & 1) ? bounds.max[0] : bounds.min[0],
            (corner & 2) ? bounds.max[1] : bounds.min[1],
            (corner & 4) ? bounds.max[2] : bounds.min[2]};
        const nfsu::ClipVertex clip = nfsu::project_point(frame, point);
        const std::string where = what + " corner " + std::to_string(corner);
        if (!(clip.w > 0.0)) {
            check(false, where + ": is behind the camera, w " +
                             std::to_string(clip.w));
            continue;
        }
        check(clip.w >= camera.near_plane,
              where + ": is nearer than the near plane, w " +
                  std::to_string(clip.w));
        check(clip.w <= camera.far_plane,
              where + ": is beyond the far plane, w " + std::to_string(clip.w));
        const double x = clip.x / clip.w;
        const double y = clip.y / clip.w;
        check(x >= -1.0 && x <= 1.0,
              where + ": x is outside the frame, " + std::to_string(x));
        check(y >= -1.0 && y <= 1.0,
              where + ": y is outside the frame, " + std::to_string(y));
    }
}

void test_framing_contains_the_box() {
    nfsu::TrackFramingOptions options;
    // The retail region of docs/TRACK_RENDER.md, rounded, and three shapes
    // that stress a different axis each.
    check_box_is_framed(box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0), 1280,
                        720, options, "wide region, wide frame");
    check_box_is_framed(box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0), 480,
                        960, options, "wide region, tall frame");
    check_box_is_framed(box(-1.0, -1.0, -1.0, 1.0, 1.0, 1.0), 1280, 720,
                        options, "unit cube");
    check_box_is_framed(box(-500.0, -1.0, -1.0, 500.0, 1.0, 1.0), 320, 320,
                        options, "long thin box, square frame");
    check_box_is_framed(box(-1.0, -500.0, -1.0, 1.0, 500.0, 1.0), 1280, 720,
                        options, "tall thin box");

    // Every yaw and pitch, because the sphere fit is meant to be
    // view-independent: a box fit would pass at one angle and fail at
    // another.
    for (int yaw = 0; yaw < 360; yaw += 45) {
        for (int pitch = -80; pitch <= 80; pitch += 40) {
            nfsu::TrackFramingOptions angled = options;
            angled.yaw_deg = yaw;
            angled.pitch_deg = pitch;
            check_box_is_framed(box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0),
                                1280, 720, angled,
                                "yaw " + std::to_string(yaw) + " pitch " +
                                    std::to_string(pitch));
        }
    }

    // A margin of exactly 1 is the boundary: the sphere touches the frame
    // edge, so the box - which is inside the sphere - must still fit.
    nfsu::TrackFramingOptions tight = options;
    tight.margin = 1.0;
    check_box_is_framed(box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0), 1280,
                        720, tight, "margin 1");

    // A narrow field of view stands the camera much farther back; a wide one
    // brings it in. Both must still contain the box.
    nfsu::TrackFramingOptions narrow = options;
    narrow.fov_y_deg = 5.0;
    check_box_is_framed(box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0), 1280,
                        720, narrow, "fov 5");
    nfsu::TrackFramingOptions wide = options;
    wide.fov_y_deg = 120.0;
    check_box_is_framed(box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0), 1280,
                        720, wide, "fov 120");
}

void test_framing_numbers() {
    // The rule, written out for one box so that a change to it is visible
    // as a number and not only as a still-contained corner.
    //
    //   half extents (214.5, 80.5, 70), radius = sqrt(214.5^2 + 80.5^2 + 70^2)
    //   ty = tan(17.5 deg), tx = ty * 1280 / 720
    //   distance = radius / min(sin ax, sin ay) * 1.05
    const nfsu::TrackWorldBounds bounds =
        box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0);
    nfsu::TrackFramingOptions options;
    nfsu::OrbitCamera camera;
    std::string error;
    check_ok(nfsu::frame_track_bounds(bounds, 1280, 720, options, camera, error),
              "the retail-shaped box frames", error);
    check_exact(camera.target.x, 650.5, "target x is the box centre");
    check_exact(camera.target.y, 56.5, "target y is the box centre");
    check_exact(camera.target.z, -139.0, "target z is the box centre");
    check_exact(camera.yaw_deg, options.yaw_deg, "yaw is the framing yaw");
    check_exact(camera.pitch_deg, options.pitch_deg, "pitch is the framing pitch");
    check_exact(camera.fov_y_deg, options.fov_y_deg, "fov is the framing fov");

    const double half[3] = {214.5, 80.5, 70.0};
    const double radius = std::sqrt(half[0] * half[0] + half[1] * half[1] +
                                    half[2] * half[2]);
    const double ty = nfsu::det_sin_deg(17.5) / nfsu::det_cos_deg(17.5);
    const double tx = ty * 1280.0 / 720.0;
    const double sy = ty / std::sqrt(1.0 + ty * ty);
    const double sx = tx / std::sqrt(1.0 + tx * tx);
    const double expected = radius / std::min(sx, sy) * 1.05;
    check_close(camera.distance, expected, 1e-9, "distance follows the rule");
    check_close(camera.near_plane,
                std::min(10.0, std::max(1e-4, (expected - radius) * 0.5)), 1e-9,
                "near plane follows the rule");
    check_close(camera.far_plane, (expected + radius) * 1.25, 1e-6,
                "far plane follows the rule");

    // The near plane, on a box small enough that the clamp to kMaxNear is
    // not what produced it. On the retail-shaped box above, (distance -
    // radius) / 2 is about 300 and the clamp answers 10 whatever the rule
    // computed, so that assertion cannot see the rule at all.
    const nfsu::TrackWorldBounds small = box(-1.0, -1.0, -1.0, 1.0, 1.0, 1.0);
    nfsu::OrbitCamera close;
    check_ok(nfsu::frame_track_bounds(small, 1280, 720, options, close, error),
             "the unit cube frames", error);
    const double small_radius = std::sqrt(3.0);
    const double small_distance = small_radius / std::min(sx, sy) * 1.05;
    check_close(close.distance, small_distance, 1e-9,
                "the unit cube distance follows the rule");
    check(close.near_plane < nfsu::kMaxNear,
          "the unit cube near plane is not at the clamp: " +
              std::to_string(close.near_plane));
    check_close(close.near_plane, (small_distance - small_radius) * 0.5, 1e-9,
                "the unit cube near plane follows the rule");

    // A frame twice as wide needs the camera no farther back, because the
    // vertical half-angle is the smaller one at this aspect and stays so.
    nfsu::OrbitCamera wider;
    check_ok(nfsu::frame_track_bounds(bounds, 2560, 720, options, wider, error),
              "the same box frames at 2560x720", error);
    check_exact(wider.distance, camera.distance,
                "a wider frame does not move the camera when the vertical "
                "angle already binds");

    // A frame narrower than it is tall makes the horizontal angle bind, and
    // then it must.
    nfsu::OrbitCamera taller;
    check_ok(nfsu::frame_track_bounds(bounds, 360, 720, options, taller, error),
              "the same box frames at 360x720", error);
    check(taller.distance > camera.distance,
          "a narrower frame stands the camera farther back: got " +
              std::to_string(taller.distance) + ", was " +
              std::to_string(camera.distance));
}

void test_framing_radius_is_single_rounded() {
    nfsu::OrbitCamera camera;
    std::string error;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 298, 84, 0), 1280, 720,
                                     {}, camera, error),
             "the sqrt(23965) framing box frames", error);
    check_exact(camera.distance, 540.55042310081103,
                "framing radius uses single-rounded sqrt: distance");
    check_exact(camera.far_plane, 869.19594215089774,
                "framing radius uses single-rounded sqrt: far plane");
}

void test_framing_half_angle_is_single_rounded() {
    nfsu::TrackFramingOptions options;
    options.fov_y_deg = 37.48;
    nfsu::OrbitCamera camera;
    std::string error;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 2, 0, 0), 1280, 720,
                                     options, camera, error),
             "the fov 37.48 framing box frames", error);
    check_exact(camera.distance, 3.2682363941296151,
                "framing half-angle uses single-rounded sqrt: distance");
    check_exact(camera.far_plane, 5.3352954926620191,
                "framing half-angle uses single-rounded sqrt: far plane");
}

void test_framing_is_a_function_of_its_inputs() {
    const nfsu::TrackWorldBounds bounds =
        box(436.0, -24.0, -209.0, 865.0, 137.0, -69.0);
    nfsu::TrackFramingOptions options;
    nfsu::OrbitCamera first;
    nfsu::OrbitCamera second;
    std::string error;
    check(nfsu::frame_track_bounds(bounds, 1280, 720, options, first, error),
          "the first framing succeeds");
    check(nfsu::frame_track_bounds(bounds, 1280, 720, options, second, error),
          "the second framing succeeds");
    check_exact(second.distance, first.distance, "distance repeats exactly");
    check_exact(second.target.x, first.target.x, "target x repeats exactly");
    check_exact(second.target.y, first.target.y, "target y repeats exactly");
    check_exact(second.target.z, first.target.z, "target z repeats exactly");
    check_exact(second.near_plane, first.near_plane, "near repeats exactly");
    check_exact(second.far_plane, first.far_plane, "far repeats exactly");
}

void test_framing_refusals() {
    nfsu::OrbitCamera camera;
    std::string error;
    const nfsu::TrackFramingOptions options;

    // Each refusal is identified by its message. Without that, any
    // refusal is interchangeable with any other and a check deleted from
    // the middle of the list is caught by whichever one runs after it.
    nfsu::TrackWorldBounds empty;
    check(!nfsu::frame_track_bounds(empty, 1280, 720, options, camera, error),
          "bounds holding no point are refused");
    check(error.find("nothing to frame") != std::string::npos,
          "and the empty-bounds check is what refused it: " + error);

    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 0, 720, options,
                                    camera, error),
          "a zero width is refused");
    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 0, options,
                                    camera, error),
          "a zero height is refused");

    nfsu::TrackWorldBounds inverted = box(0, 0, 0, 1, 1, 1);
    inverted.min[1] = 5.0;
    check(!nfsu::frame_track_bounds(inverted, 1280, 720, options, camera, error),
          "inverted bounds are refused");
    check(error.find("inverted") != std::string::npos,
          "and the inversion check is what refused it: " + error);

    nfsu::TrackWorldBounds infinite = box(0, 0, 0, 1, 1, 1);
    infinite.max[0] = std::numeric_limits<double>::infinity();
    check(!nfsu::frame_track_bounds(infinite, 1280, 720, options, camera, error),
          "infinite bounds are refused");
    check(error.find("not finite") != std::string::npos,
          "and the finiteness check is what refused it: " + error);

    nfsu::TrackFramingOptions bad = options;
    bad.fov_y_deg = nfsu::kMaxFov + 1.0;
    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, bad,
                                    camera, error),
          "a field of view above the limit is refused");
    check(error.find("field of view") != std::string::npos,
          "and the fov check is what refused it: " + error);
    bad = options;
    bad.margin = std::numeric_limits<double>::quiet_NaN();
    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, bad,
                                    camera, error),
          "a non-finite framing option is refused");
    check(error.find("must be finite") != std::string::npos,
          "and the option finiteness check is what refused it: " + error);
    bad = options;
    bad.fov_y_deg = nfsu::kMinFov - 0.5;
    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, bad,
                                    camera, error),
          "a field of view below the limit is refused");
    bad = options;
    bad.pitch_deg = nfsu::kMaxPitch + 1.0;
    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, bad,
                                    camera, error),
          "a pitch above the limit is refused");
    bad = options;
    bad.margin = nfsu::kMinTrackFramingMargin - 0.5;
    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, bad,
                                    camera, error),
          "a margin below one is refused");
    bad = options;
    bad.margin = nfsu::kMaxTrackFramingMargin + 1.0;
    check(!nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, bad,
                                    camera, error),
          "a margin above the limit is refused");

    // Accepted at the boundary, because a refusal one step inside the range
    // would pass every test that only drives the middle of it.
    nfsu::TrackFramingOptions edge = options;
    edge.margin = nfsu::kMinTrackFramingMargin;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, edge,
                                   camera, error),
              "a margin of exactly one is accepted", error);
    edge = options;
    edge.margin = nfsu::kMaxTrackFramingMargin;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, edge,
                                   camera, error),
              "a margin at the limit is accepted", error);
    edge = options;
    edge.fov_y_deg = nfsu::kMaxFov;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720, edge,
                                   camera, error),
              "a field of view at the limit is accepted", error);

    // A box centred beyond the camera target range, and one so large that
    // no distance inside kMaxDistance contains it.
    check(!nfsu::frame_track_bounds(
              box(nfsu::kMaxTargetMagnitude + 10.0, 0, 0,
                  nfsu::kMaxTargetMagnitude + 12.0, 1, 1),
              1280, 720, options, camera, error),
          "a box centred outside the target range is refused");
    // By the explicit check, not by the validate_camera self-check that
    // follows it. Both refuse, so a test asking only for false would pass
    // with the explicit one deleted and would say nothing about which
    // diagnostic a caller sees.
    check(error.find("camera target range") != std::string::npos,
          "and the target-range check is what refused it: " + error);
    // Every axis, not only x: the check is a loop and a loop bound one
    // short of three leaves one axis unguarded, with only the
    // validate_camera self-check behind it.
    check(!nfsu::frame_track_bounds(
              box(0, nfsu::kMaxTargetMagnitude + 10.0, 0, 1,
                  nfsu::kMaxTargetMagnitude + 12.0, 1),
              1280, 720, options, camera, error),
          "a box centred outside the target range on y is refused");
    check(error.find("camera target range") != std::string::npos,
          "and the target-range check is what refused y: " + error);
    check(!nfsu::frame_track_bounds(
              box(0, 0, nfsu::kMaxTargetMagnitude + 10.0, 1, 1,
                  nfsu::kMaxTargetMagnitude + 12.0),
              1280, 720, options, camera, error),
          "a box centred outside the target range on z is refused");
    check(error.find("camera target range") != std::string::npos,
          "and the target-range check is what refused z: " + error);

    check(!nfsu::frame_track_bounds(box(-9000, -9000, -9000, 9000, 9000, 9000),
                                    1280, 720, options, camera, error),
          "a box too large to fit inside the distance limit is refused");
    check(error.find("camera distance") != std::string::npos,
          "and the distance limit is what refused it: " + error);

    // Accepted at every other range boundary. A refusal one step inside any
    // of these would pass every case that drives the middle of the range.
    nfsu::TrackFramingOptions at_edge = options;
    at_edge.yaw_deg = nfsu::kMaxYawMagnitude;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720,
                                      at_edge, camera, error),
             "the largest yaw is accepted", error);
    at_edge = options;
    at_edge.yaw_deg = -nfsu::kMaxYawMagnitude;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720,
                                      at_edge, camera, error),
             "the smallest yaw is accepted", error);
    at_edge = options;
    at_edge.pitch_deg = nfsu::kMaxPitch;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720,
                                      at_edge, camera, error),
             "the highest pitch is accepted", error);
    at_edge = options;
    at_edge.pitch_deg = nfsu::kMinPitch;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720,
                                      at_edge, camera, error),
             "the lowest pitch is accepted", error);
    at_edge = options;
    at_edge.fov_y_deg = nfsu::kMinFov;
    check_ok(nfsu::frame_track_bounds(box(0, 0, 0, 1, 1, 1), 1280, 720,
                                      at_edge, camera, error),
             "the narrowest field of view is accepted", error);
    check(!nfsu::frame_track_bounds(
              box(nfsu::kMaxTargetMagnitude - 1.0, 0, 0,
                  nfsu::kMaxTargetMagnitude + 1.0, 1, 1),
              1280, 720, options, camera, error) == false,
          "a box whose centre is exactly at the target limit is accepted");
}

void test_framing_a_single_point() {
    // A degenerate box has radius zero, so the computed distance is zero and
    // the clamp to kMinDistance is the only thing that keeps the camera
    // valid. It is a real case: a draw list holding one degenerate triangle
    // produces exactly this.
    nfsu::OrbitCamera camera;
    std::string error;
    const nfsu::TrackFramingOptions options;
    check_ok(nfsu::frame_track_bounds(box(5, 6, 7, 5, 6, 7), 1280, 720, options,
                                   camera, error),
              "a single point frames", error);
    check_exact(camera.distance, nfsu::kMinDistance,
                "a single point sits at the minimum distance");
    check_ok(nfsu::validate_camera(camera, error),
              "the single-point camera validates", error);
}

// ---------------------------------------------------------------------------
// render_track_draw_list
// ---------------------------------------------------------------------------

void test_render_counts() {
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                       nfsu::RenderLimits{}, frame, stats, track,
                                       error),
              "the fixture renders", error);

    // Three placed instances: two of PANEL (one group each) and one of POST
    // (two groups), so four entries and six triangles.
    check_eq_u64(track.entries, 4, "entries walked");
    check_eq_u64(track.entries_drawn, 4, "entries that submitted a triangle");
    check_eq_u64(track.entries_resolved, 4, "entries with a resolved texture");
    check_eq_u64(track.entries_unresolved, 0, "entries with no texture");
    check_eq_u64(track.entries_unusable, 0, "entries with an unusable binding");
    check_eq_u64(track.instances_drawn, 3, "instances that reached the frame");
    check_eq_u64(track.meshes_drawn, 2, "meshes that reached the frame");
    check_eq_u64(track.groups, 3, "distinct groups the entries named");
    check_eq_u64(stats.triangles_submitted, 6, "triangles submitted");
    check_eq_u64(stats.triangles_resolved, 6, "triangles drawn with a texture");
    check_eq_u64(stats.triangles_fallback, 0, "triangles drawn with a fallback");
    check_eq_u64(stats.triangles_bad_index, 0, "triangles with a bad index");

    // The region's own numbers, not the frame's: the fixture has no invalid
    // mesh and leaves no hash unresolved.
    check_eq_u64(stats.meshes, 2, "region meshes");
    check_eq_u64(stats.meshes_rendered, 2, "region meshes prepared");
    check_eq_u64(stats.meshes_skipped_invalid, 0, "region meshes skipped");
    check_eq_u64(stats.unresolved_hashes, 0, "region unresolved hashes");

    // The copies that reached the frame, and the claim that none of them is
    // a stand-in.
    check_eq_u64(stats.instances, 3, "copies that submitted a triangle");
    check_eq_u64(stats.instances_exact, 3, "copies at the recovered placement");
    check_eq_u64(stats.instances_stand_in, 0, "copies at a stand-in placement");

    // The counters this path deliberately leaves alone. A later change that
    // starts filling one of them with a copy count would read like a car
    // number and mean something else.
    check_eq_u64(stats.material_groups, 0, "material_groups is not used here");
    check_eq_u64(stats.groups_resolved, 0, "groups_resolved is not used here");
    check_eq_u64(stats.groups_unresolved, 0, "groups_unresolved is not used here");
    check_eq_u64(stats.groups_alpha_tested, 0, "no group is alpha tested");
    check_eq_u64(stats.groups_blended, 0, "no group is blended");
    check_eq_u64(stats.fragments_blended, 0, "no fragment is blended");

    check(stats.pixels_drawn > 0, "the frame has drawn pixels");
    check_eq_u64(stats.pixels_fallback, 0, "no drawn pixel is a fallback");
    check(track.bounds.drawn.valid, "the render measured the drawn bounds");
    check_exact(track.bounds.drawn.min[0], -3.5,
                "the render's bounds are the list's bounds");
    check_exact(track.bounds.drawn.max[0], 4.0,
                "the render's bounds are the list's bounds");
}

void test_render_texture_binds_match_the_draw_list_cost() {
    const Fixture fixture = town();
    for (const nfsu::TrackDrawOrder order :
         {nfsu::TrackDrawOrder::visible_instance,
          nfsu::TrackDrawOrder::material_group}) {
        const nfsu::TrackDrawList list = make_list(fixture.scene, order);
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        nfsu::TrackRenderStats track;
        std::string error;
        check_ok(nfsu::render_track_draw_list(fixture.scene, list,
                                              small_options(),
                                              nfsu::RenderLimits{}, frame,
                                              stats, track, error),
                 "the fixture renders in order " +
                     std::string(nfsu::track_draw_order_name(order)), error);
        check_eq_u64(track.texture_binds, list.cost.texture_binds,
                     std::string("binds agree with the draw list cost in ") +
                         nfsu::track_draw_order_name(order));
    }
}

void test_render_order_changes_the_binds_not_the_pixels() {
    const Fixture fixture = town();
    const nfsu::TrackDrawList by_instance =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    const nfsu::TrackDrawList by_group =
        make_list(fixture.scene, nfsu::TrackDrawOrder::material_group);
    check(by_instance.entries.size() == by_group.entries.size(),
          "the two orders hold the same number of entries");

    nfsu::Frame first;
    nfsu::Frame second;
    nfsu::RenderStats stats_a;
    nfsu::RenderStats stats_b;
    nfsu::TrackRenderStats track_a;
    nfsu::TrackRenderStats track_b;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, by_instance,
                                       small_options(), nfsu::RenderLimits{},
                                       first, stats_a, track_a, error),
              "the visible-instance order renders", error);
    check_ok(nfsu::render_track_draw_list(fixture.scene, by_group, small_options(),
                                       nfsu::RenderLimits{}, second, stats_b,
                                       track_b, error),
              "the material-group order renders", error);
    check_eq_u64(stats_b.triangles_submitted, stats_a.triangles_submitted,
                 "the two orders submit the same triangles");
    // Nothing in this fixture overlaps, so the two orders must agree pixel
    // for pixel. A fixture where they did not would be testing the depth
    // tie rule, which test_render_depth_tie does on purpose.
    check(first.rgba() == second.rgba(),
          "the two orders draw the same pixels when nothing overlaps");
}

void test_render_depth_tie_goes_to_the_later_entry() {
    // Two surfaces at exactly the same depth, drawn in each order. The
    // renderer breaks a depth tie by triangle id and the later entry gets
    // the larger id, so which surface survives must follow the list order.
    //
    // This is the one visible consequence of TrackDrawOrder a backend
    // cannot ignore, and docs/TRACK_RENDER.md measures how many pixels it
    // moves over the retail region rather than claiming it moves none.
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene = fixture.scene;
    const std::size_t panel = instance_of_mesh(scene, 0);
    const std::size_t post = instance_of_mesh(scene, 1);
    // Put POST exactly on top of PANEL. The two then share a plane, so
    // every pixel both cover is a depth tie.
    scene.instances[post].transform = scene.instances[panel].transform;

    // A point inside POST's SECOND group, which is the only group on the
    // flat texture: POST's triangles are (0,1,2), the lower right half, and
    // (0,2,3), the upper left half, and the second group holds the second.
    const double x = scene.instances[panel].transform.translation[0];
    const nfsu::Vec3 probe{x - 0.25, 0.5, 0.0};
    const nfsu::RenderOptions options = small_options();
    std::uint32_t px = 0;
    std::uint32_t py = 0;
    check(screen_pixel_of(options.camera, options.width, options.height, probe,
                          px, py),
          "the probe point is inside the frame");

    const auto render_order = [&](const std::vector<std::size_t>& order,
                                  nfsu::Frame& frame) {
        const nfsu::TrackDrawList list = list_over(scene, order);
        check_eq_u64(list.entries.size(), 3,
                     "the coplanar list holds PANEL's group and POST's two");
        nfsu::RenderStats stats;
        nfsu::TrackRenderStats track;
        std::string error;
        check_ok(nfsu::render_track_draw_list(scene, list, options,
                                              nfsu::RenderLimits{}, frame,
                                              stats, track, error),
                 "the coplanar pair renders", error);
    };

    nfsu::Frame panel_last;
    nfsu::Frame post_last;
    render_order({post, panel}, panel_last);
    render_order({panel, post}, post_last);

    const std::array<unsigned char, 4> a = pixel_at(panel_last, px, py);
    const std::array<unsigned char, 4> b = pixel_at(post_last, px, py);
    check(a != b,
          "the coplanar winner follows the draw order: both frames gave " +
              describe_pixel(a));
    check(b[0] == kFlatR && b[1] == kFlatG && b[2] == kFlatB,
          "the flat texture wins when POST is last: got " + describe_pixel(b));
    check(!(a[0] == kFlatR && a[1] == kFlatG && a[2] == kFlatB),
          "and loses when PANEL is last: got " + describe_pixel(a));
}

// PR #225: the track path requests no depth writes for blended groups.
// Exercise the actual pixels/depth, not the observer's declared state.
void test_blended_track_preserves_depth_for_later_draw() {
    auto meshes = fixture_meshes();
    meshes[1] = meshes[0];
    meshes[1].name = "FRONT";
    meshes[1].hash = kPost;
    meshes[1].texture_hashes = {kTexB};
    auto sections = fixture_sections();
    sections[0].instances[2] = placed(1, 0.0F, -1.0F, 0.0F);
    Fixture fixture = make_fixture(meshes, sections);
    for (unsigned char blend : {1, 2}) {
        fixture.textures.textures[1].alpha_blend_mode = blend;
        nfsu::PreparedTrackScene scene;
        std::string error;
        if (!nfsu::build_track_scene(fixture.geometry, fixture.objects,
                fixture.textures, {}, scene, error)) {
            check(false, "blended track fixture preparation: " + error);
            continue;
        }
        const auto back = instance_of_mesh(scene, 0);
        const auto front = instance_of_mesh(scene, 1);
        auto options = small_options();
        options.material_state = nfsu::MaterialStateMode::retail;
        std::uint32_t px = 0, py = 0;
        check(screen_pixel_of(options.camera, options.width, options.height,
                              {0.25, 0.25, 0.0}, px, py),
              "blended track probe lies within both quads");
        const auto render = [&](const std::vector<std::size_t>& order) {
            nfsu::Frame frame;
            nfsu::RenderStats stats;
            nfsu::TrackRenderStats track;
            check_ok(nfsu::render_track_draw_list(scene, list_over(scene, order),
                         options, {}, frame, stats, track, error),
                     "blended track overlap renders", error);
            return frame;
        };
        const auto empty = render({});
        const auto back_only = render({back});
        const auto front_only = render({front});
        const auto overlap = render({front, back});
        const std::size_t index = std::size_t{py} * options.width + px;
        check(pixel_at(front_only, px, py) != pixel_at(empty, px, py),
              "blended front changes the probe color");
        check(front_only.depth()[index] == empty.depth()[index],
              "blended track preserves the stored depth");
        check(pixel_at(overlap, px, py) == pixel_at(back_only, px, py),
              "later opaque track draw behind blended front reaches the pixel");
        check(overlap.depth()[index] == back_only.depth()[index],
              "later opaque track draw stores its depth");
        options.material_state = nfsu::MaterialStateMode::off;
        const auto opaque_front = render({front});
        const auto opaque_overlap = render({front, back});
        check(opaque_front.depth()[index] < back_only.depth()[index],
              "opaque front control is nearer and writes depth");
        check(pixel_at(opaque_overlap, px, py) == pixel_at(opaque_front, px, py) &&
              opaque_overlap.depth()[index] == opaque_front.depth()[index],
              "opaque front control occludes the later back draw");
    }
}

void test_render_is_deterministic() {
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::material_group);
    nfsu::Frame first;
    nfsu::Frame second;
    nfsu::RenderStats stats_a;
    nfsu::RenderStats stats_b;
    nfsu::TrackRenderStats track_a;
    nfsu::TrackRenderStats track_b;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                       nfsu::RenderLimits{}, first, stats_a,
                                       track_a, error),
              "the first render succeeds", error);
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                       nfsu::RenderLimits{}, second, stats_b,
                                       track_b, error),
              "the second render succeeds", error);
    check(first.rgba() == second.rgba(), "two renders give identical pixels");
    check(first.depth() == second.depth(), "two renders give identical depth");
    check(first.ids() == second.ids(), "two renders give identical ids");
    check_eq_u64(stats_b.fragments_written, stats_a.fragments_written,
                 "two renders write the same fragments");
    check_eq_u64(track_b.texture_binds, track_a.texture_binds,
                 "two renders bind the same number of times");
}

void test_render_samples_the_texture() {
    // One instance of POST, face on, with shading off, so a pixel inside
    // its flat-textured half is the texel byte for byte. Anything that
    // changed the sampling, the shade factor or the channel order moves it.
    const Fixture fixture = town();
    const std::size_t post = instance_of_mesh(fixture.scene, 1);
    const nfsu::TrackDrawList list = list_over(fixture.scene, {post});
    check_eq_u64(list.entries.size(), 2, "POST contributes its two groups");

    const double x = fixture.scene.instances[post].transform.translation[0];
    nfsu::RenderOptions options = small_options();
    options.camera.target = nfsu::Vec3{x, 0.0, 0.0};

    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the single instance renders", error);
    check(stats.pixels_drawn > 0, "the single instance covers pixels");

    // Inside POST's second group, the upper left half, which is the only
    // one bound to the flat texture.
    std::uint32_t px = 0;
    std::uint32_t py = 0;
    check(screen_pixel_of(options.camera, options.width, options.height,
                          nfsu::Vec3{x - 0.25, 0.5, 0.0}, px, py),
          "the flat-half probe point is inside the frame");
    const std::array<unsigned char, 4> flat = pixel_at(frame, px, py);
    check(flat[0] == kFlatR && flat[1] == kFlatG && flat[2] == kFlatB,
          "the flat half is the texel: got " + describe_pixel(flat));

    // Inside POST's first group, the lower right half, which is bound to
    // the corner texture: a different colour, so a run that bound one
    // texture for both groups fails here.
    check(screen_pixel_of(options.camera, options.width, options.height,
                          nfsu::Vec3{x + 0.25, -0.5, 0.0}, px, py),
          "the corner-half probe point is inside the frame");
    const std::array<unsigned char, 4> corner_half = pixel_at(frame, px, py);
    check(!(corner_half[0] == kFlatR && corner_half[1] == kFlatG &&
            corner_half[2] == kFlatB),
          "the other half is not the flat texel: got " +
              describe_pixel(corner_half));

    // A corner pixel the geometry cannot reach is the background, so a
    // frame that was never cleared, or cleared with the wrong bytes, fails.
    const std::array<unsigned char, 4> corner = pixel_at(frame, 0, 0);
    check(corner[0] == options.background[0] &&
              corner[1] == options.background[1] &&
              corner[2] == options.background[2],
          "an untouched pixel is the background: got " + describe_pixel(corner));
}

void test_render_honours_the_options() {
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    const nfsu::RenderLimits limits;
    std::string error;

    nfsu::Frame plain;
    nfsu::RenderStats plain_stats;
    nfsu::TrackRenderStats plain_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                       limits, plain, plain_stats, plain_track,
                                       error),
              "the plain render succeeds", error);

    // Shading. With flat shading the same texel is multiplied by a shade
    // factor, so the frame must differ.
    nfsu::RenderOptions shaded = small_options();
    shaded.shading = nfsu::ShadingMode::flat;
    nfsu::Frame shaded_frame;
    nfsu::RenderStats shaded_stats;
    nfsu::TrackRenderStats shaded_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, shaded, limits,
                                       shaded_frame, shaded_stats, shaded_track,
                                       error),
              "the shaded render succeeds", error);
    check(shaded_frame.rgba() != plain.rgba(),
          "flat shading changes the frame");
    check_eq_u64(shaded_stats.pixels_drawn, plain_stats.pixels_drawn,
                 "flat shading covers the same pixels");

    // Wet reflections use the solid selector and the vertex alpha mask.
    const Fixture wet_fixture = floor();
    const nfsu::TrackDrawList wet_list =
        make_list(wet_fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::RenderOptions wet = small_options();
    wet.vertex_color = true;
    wet.camera.pitch_deg = 25.0;
    wet.camera.target.y = 2.0;
    wet.wet_road_reflections = true;
    nfsu::Frame wet_frame;
    nfsu::RenderStats wet_stats;
    nfsu::TrackRenderStats wet_track;
    check_ok(nfsu::render_track_draw_list(wet_fixture.scene, wet_list, wet, limits,
                                       wet_frame, wet_stats, wet_track, error),
              "the wet-reflection render succeeds", error);
    nfsu::Frame dry_tilt_frame;
    nfsu::RenderStats dry_tilt_stats;
    nfsu::TrackRenderStats dry_tilt_track;
    nfsu::RenderOptions dry_tilt = small_options();
    dry_tilt.vertex_color = true;
    dry_tilt.camera.pitch_deg = wet.camera.pitch_deg;
    dry_tilt.camera.target.y = wet.camera.target.y;
    check_ok(nfsu::render_track_draw_list(wet_fixture.scene, wet_list, dry_tilt,
                                       limits, dry_tilt_frame, dry_tilt_stats,
                                       dry_tilt_track, error),
              "the dry tilted render succeeds", error);
    check(wet_frame.rgba() != dry_tilt_frame.rgba(),
          "wet reflections sample the actual mirrored source geometry");
    check(wet_stats.pixels_drawn == dry_tilt_stats.pixels_drawn,
          "wet reflections preserve surface coverage");
    check(wet_track.reflection_draws > 0 && wet_track.reflection_fragments > 0,
          "the reflection producer draws a nonempty selected scene");
    auto render_wet = [&](const Fixture& changed) {
        nfsu::Frame result;
        check_ok(nfsu::render_track_draw_list(changed.scene, wet_list, wet, limits,
            result, wet_stats, wet_track, error), "changed reflection scene renders", error);
        return result.rgba();
    };
    auto changed = wet_fixture;
    changed.scene.region.meshes[0].vertex_format_flags = 0;
    check(render_wet(changed) == dry_tilt_frame.rgba(),
          "an unflagged horizontal surface never receives reflections");
    changed = wet_fixture;
    for (auto& v : changed.scene.region.meshes[0].vertices) v.color[3] = 0;
    check(render_wet(changed) == dry_tilt_frame.rgba(),
          "zero vertex alpha removes the reflection without changing diffuse");
    changed = wet_fixture;
    for (std::size_t p = 3; p < changed.scene.region.textures[0].rgba.size(); p += 4)
        changed.scene.region.textures[0].rgba[p] = 0;
    check(render_wet(changed) == dry_tilt_frame.rgba(),
          "zero diffuse alpha removes the reflection without changing opaque diffuse");
    changed = wet_fixture;
    changed.scene.instances[2].instance_flags = 0;
    check(render_wet(changed) == dry_tilt_frame.rgba(),
          "removing the source selector removes its reflection");
    changed = wet_fixture;
    for (auto& v : changed.scene.region.meshes[1].vertices) v.color[0] = 0;
    const auto recolored = render_wet(changed);
    std::size_t changed_reflections = 0;
    for (std::size_t p = 0; p < recolored.size(); p += 4)
        if (wet_frame.rgba()[p] != dry_tilt_frame.rgba()[p] &&
            recolored[p] != wet_frame.rgba()[p]) ++changed_reflections;
    check(changed_reflections > 0, "source color changes its road reflection");


    // The reflection visibility list is independent of the main visible list.
    std::vector<nfsu::TrackDrawListEntry> reflected;
    check_ok(nfsu::track_reflection_entries(wet_fixture.scene, wet, reflected, error),
             "separate reflection cull succeeds", error);
    check(!reflected.empty(), "a reflection query needs no main-view visible entries");
    changed = wet_fixture;
    changed.scene.instances[2].transform.translation[0] = 100000;
    check_ok(nfsu::track_reflection_entries(changed.scene, wet, reflected, error),
             "reflection cull rejects a remote source", error);
    check(reflected.empty(), "offscreen reflected bounds are rejected");
    // Keep the reflected source centered inside the side planes and move
    // it along the view axis. Only the far plane can reject this fixture.
    changed = wet_fixture;
    nfsu::CameraFrame reflection_camera;
    check_ok(nfsu::make_camera_frame(wet.camera, wet.width, wet.height,
        reflection_camera, error), "far-cull camera is valid", error);
    const auto move_source = [&](double distance) {
        const auto& c = reflection_camera;
        auto& translation = changed.scene.instances[2].transform.translation;
        translation[0] = float(c.eye.x + c.forward.x*distance);
        translation[1] = float(-(c.eye.z + c.forward.z*distance));
        translation[2] = float(c.eye.y + c.forward.y*distance);
    };
    move_source(reflection_camera.far_plane - 10);
    check_ok(nfsu::track_reflection_entries(changed.scene, wet, reflected, error),
             "source inside the far plane queries", error);
    check(!reflected.empty(), "centered source inside far plane is retained");
    move_source(reflection_camera.far_plane + 10);
    check_ok(nfsu::track_reflection_entries(changed.scene, wet, reflected, error),
             "source beyond the far plane queries", error);
    check(reflected.empty(), "centered source beyond far plane is rejected");
    check_ok(nfsu::track_reflection_entries(changed.scene, wet, reflected, error, nullptr, false),
             "unculled far source queries", error);
    check(!reflected.empty(), "far source still exists when culling is disabled");
    auto instance = wet_fixture.scene.instances[2];
    instance.transform.linear[8] = 3;
    instance.transform.translation[2] = 7;
    auto mirrored = nfsu::track_reflection_transform(instance);
    check(mirrored.linear[8] == -3 && mirrored.translation[2] == 7,
          "ordinary reflection negates local vertical without moving the origin");
    instance.instance_flags |= 0x200;
    mirrored = nfsu::track_reflection_transform(instance);
    check(mirrored.linear[8] == 3 && mirrored.translation[2] == 7,
          "baked 0x200 placement skips the second mirror");

    nfsu::WetRoadLamps lamps;
    lamps.texture = wet_fixture.scene.region.textures[0];
    lamps.texture.rgba.assign(lamps.texture.rgba.size(), 255);
    nfsu::WetRoadLamp light;
    check(!nfsu::wet_road_lamp_ground_height(light, 0) &&
          !nfsu::wet_road_lamp_ground_height(light, 0.049F) &&
          !nfsu::wet_road_lamp_ground_height(light, -0.049F),
          "mode 2 rejects ground hits within 0.05 of the lamp");
    check(nfsu::wet_road_lamp_ground_height(light, 0.051F) &&
          light.reflected_height == 0.102F,
          "mode 2 keeps ground hits outside the height tolerance");
    check(nfsu::wet_road_lamp_ground_height(light, 0.05F) &&
          nfsu::wet_road_lamp_ground_height(light, -0.05F),
          "mode 2 keeps ground hits exactly at the height tolerance");
    light.position[2] = 2; light.reflected_height = -2; light.direction[2] = -1;
    lamps.lights.push_back(light);
    for (std::size_t i = 0; i < 4; ++i) {
        auto& v = lamps.vertices[i];
        v.px = (i == 1 || i == 2) ? 1 : -1;
        v.py = i < 2 ? 1 : -1;
        v.color[0] = 110; v.color[1] = 100; v.color[2] = 75; v.color[3] = 255;
    }
    wet.wet_lamps = &lamps;
    const auto lit = render_wet(wet_fixture);
    check(lit != wet_frame.rgba(), "a reflected street lamp contributes a pool on wet geometry");
    lamps.lights.clear();
    check(render_wet(wet_fixture) == wet_frame.rgba(), "removing lamp records removes the pool");
    lamps.lights.push_back(light);
    changed = wet_fixture;
    for (auto& v : changed.scene.region.meshes[0].vertices) v.color[3] = 0;
    check(render_wet(changed) == dry_tilt_frame.rgba(), "lamp streak obeys receiver alpha");
    wet.camera.target.x += 1;
    check(render_wet(wet_fixture) != lit, "camera movement reprojects the lamp streak");
    wet.wet_lamps = nullptr;

    // The v flip. The corner texture has four distinct texels, so sampling
    // (u, 1 - v) must move colour.
    nfsu::RenderOptions flipped = small_options();
    flipped.flip_v = true;
    nfsu::Frame flipped_frame;
    nfsu::RenderStats flipped_stats;
    nfsu::TrackRenderStats flipped_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, flipped, limits,
                                       flipped_frame, flipped_stats,
                                       flipped_track, error),
              "the flipped render succeeds", error);
    check(flipped_frame.rgba() != plain.rgba(), "flip_v changes the frame");

    // The alpha cutoff. src/render.cpp:1052 keeps a texel whose alpha is AT
    // OR ABOVE the cutoff, so kFlatA + 1 discards every flat texel and
    // kFlatA keeps every one of them. The corner texture is opaque, so what
    // disappears is exactly POST's flat-textured half.
    nfsu::RenderOptions cut = small_options();
    cut.alpha_cutoff = static_cast<std::uint8_t>(kFlatA + 1);
    nfsu::Frame cut_frame;
    nfsu::RenderStats cut_stats;
    nfsu::TrackRenderStats cut_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, cut, limits,
                                       cut_frame, cut_stats, cut_track, error),
              "the cutoff render succeeds", error);
    check(cut_stats.fragments_alpha_discarded > 0,
          "a cutoff one above the texel alpha discards fragments");
    check(cut_stats.pixels_drawn < plain_stats.pixels_drawn,
          "and the frame loses pixels: " +
              std::to_string(cut_stats.pixels_drawn) + " of " +
              std::to_string(plain_stats.pixels_drawn));

    nfsu::RenderOptions keep = small_options();
    keep.alpha_cutoff = kFlatA;
    nfsu::Frame keep_frame;
    nfsu::RenderStats keep_stats;
    nfsu::TrackRenderStats keep_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, keep, limits,
                                       keep_frame, keep_stats, keep_track,
                                       error),
              "the at-the-alpha cutoff render succeeds", error);
    check_eq_u64(keep_stats.fragments_alpha_discarded, 0,
                 "a cutoff at exactly the texel alpha discards nothing");

    // Culling. The fixture's quads have one winding, so one mode keeps them
    // and the other removes them.
    nfsu::RenderOptions back = small_options();
    back.cull = nfsu::CullMode::back;
    nfsu::RenderOptions front = small_options();
    front.cull = nfsu::CullMode::front;
    nfsu::Frame back_frame;
    nfsu::Frame front_frame;
    nfsu::RenderStats back_stats;
    nfsu::RenderStats front_stats;
    nfsu::TrackRenderStats back_track;
    nfsu::TrackRenderStats front_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, back, limits,
                                       back_frame, back_stats, back_track,
                                       error),
              "the back-cull render succeeds", error);
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, front, limits,
                                       front_frame, front_stats, front_track,
                                       error),
              "the front-cull render succeeds", error);
    check(back_stats.pixels_drawn + front_stats.pixels_drawn ==
              plain_stats.pixels_drawn,
          "the two cull modes partition the two-sided frame: " +
              std::to_string(back_stats.pixels_drawn) + " + " +
              std::to_string(front_stats.pixels_drawn) + " != " +
              std::to_string(plain_stats.pixels_drawn));

    // The frame size, which the options carry and the camera reads for its
    // aspect.
    nfsu::RenderOptions sized = small_options();
    sized.width = 64;
    sized.height = 48;
    nfsu::Frame sized_frame;
    nfsu::RenderStats sized_stats;
    nfsu::TrackRenderStats sized_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, sized, limits,
                                       sized_frame, sized_stats, sized_track,
                                       error),
              "the resized render succeeds", error);
    check_eq_u64(sized_frame.width(), 64, "the frame is as wide as asked");
    check_eq_u64(sized_frame.height(), 48, "the frame is as tall as asked");
}

void test_render_refusals() {
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    std::string error;

    // Retail material state is accepted by the track renderer.
    nfsu::RenderOptions retail = small_options();
    retail.material_state = nfsu::MaterialStateMode::retail;
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, retail,
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the retail material state renders", error);
    check_eq_u64(frame.width(), retail.width, "retail render allocates the frame");

    // An invalid camera, refused before the frame is allocated.
    nfsu::RenderOptions bad_camera = small_options();
    bad_camera.camera.distance = -1.0;
    nfsu::Frame untouched;
    check(!nfsu::render_track_draw_list(fixture.scene, list, bad_camera,
                                        nfsu::RenderLimits{}, untouched, stats,
                                        track, error),
          "an invalid camera is refused");
    check_eq_u64(untouched.width(), 0,
                 "an invalid camera leaves the frame unallocated");

    // A tampered scene. The renderer runs the scene and list validators, so
    // a triangle index outside the mesh - which no parsed region can hold,
    // because src/track.cpp refuses one - is refused here rather than drawn.
    nfsu::PreparedTrackScene broken = fixture.scene;
    broken.region.meshes[0].triangles[0].a = 9999;
    check(!nfsu::render_track_draw_list(broken, list, small_options(),
                                        nfsu::RenderLimits{}, untouched, stats,
                                        track, error),
          "a triangle index outside the mesh is refused");

    // A tampered list.
    nfsu::TrackDrawList bad_list = list;
    bad_list.entries[0].draw =
        static_cast<std::uint32_t>(fixture.scene.region.draws.size());
    check(!nfsu::render_track_draw_list(fixture.scene, bad_list, small_options(),
                                        nfsu::RenderLimits{}, untouched, stats,
                                        track, error),
          "a group handle outside the region is refused");

    // A list whose entries are all valid and whose cost statistics are
    // not. track_draw_list_bounds does not read the cost, so this is the
    // one case only validate_track_draw_list catches - and the cost is what
    // the submitted-triangle limit above is measured against, so a render
    // that trusted it would size its work against a number the list made
    // up.
    nfsu::TrackDrawList lying = list;
    lying.cost.triangles = 0;
    check(!nfsu::render_track_draw_list(fixture.scene, lying, small_options(),
                                        nfsu::RenderLimits{}, untouched, stats,
                                        track, error),
          "a list whose cost does not match its entries is refused");

    // Work limits, both of them, one step below what the fixture needs.
    nfsu::RenderLimits tight;
    tight.max_submitted_triangles = list.cost.triangles - 1;
    check(!nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                        tight, untouched, stats, track, error),
          "a submitted-triangle limit below the list is refused");
    nfsu::RenderLimits tight_source;
    std::uint64_t source_triangles = 0;
    for (const nfsu::PreparedTrackMesh& mesh : fixture.scene.region.meshes) {
        source_triangles += mesh.triangles.size();
    }
    tight_source.max_source_triangles = source_triangles - 1;
    check(!nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                        tight_source, untouched, stats, track,
                                        error),
          "a source-triangle limit below the region is refused");

    // And accepted at exactly the number needed, because a limit checked
    // with the wrong comparison passes every test that stays away from the
    // boundary.
    nfsu::RenderLimits exact;
    exact.max_submitted_triangles = list.cost.triangles;
    exact.max_source_triangles = source_triangles;
    nfsu::Frame allowed;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                       exact, allowed, stats, track, error),
              "a limit at exactly what is needed is accepted", error);
}

void test_render_an_empty_list() {
    const Fixture fixture = town();
    nfsu::TrackDrawList empty;
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, empty, small_options(),
                                       nfsu::RenderLimits{}, frame, stats, track,
                                       error),
              "an empty list renders", error);
    check_eq_u64(track.entries, 0, "an empty list walks no entry");
    check_eq_u64(track.texture_binds, 0, "an empty list binds nothing");
    check_eq_u64(stats.triangles_submitted, 0,
                 "an empty list submits no triangle");
    check_eq_u64(stats.pixels_drawn, 0, "an empty list draws no pixel");
    check(!track.bounds.drawn.valid, "an empty list has no drawn bounds");
    check_eq_u64(frame.width(), small_options().width,
                 "an empty list still allocates the frame");
    // Every pixel is the background, which is the frame a viewer would get
    // rather than an uninitialised buffer.
    bool all_background = true;
    const nfsu::RenderOptions options = small_options();
    for (std::uint32_t y = 0; y < frame.height() && all_background; ++y) {
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::array<unsigned char, 4> pixel = pixel_at(frame, x, y);
            if (pixel[0] != options.background[0] ||
                pixel[1] != options.background[1] ||
                pixel[2] != options.background[2]) {
                all_background = false;
                break;
            }
        }
    }
    check(all_background, "an empty list leaves the whole frame at background");
}

void test_render_an_unresolved_group() {
    // A region whose material names a hash no texture claims. The group is
    // still drawable, and the renderer draws it with the fallback pattern,
    // which is what a backend does with a hole rather than skipping it.
    std::vector<MeshSpec> meshes = fixture_meshes();
    meshes[0].texture_hashes = {0x0000DEADU};
    const Fixture fixture = make_fixture(meshes, fixture_sections());
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                       nfsu::RenderLimits{}, frame, stats, track,
                                       error),
              "the unresolved fixture renders", error);
    check_eq_u64(track.entries_unresolved, 2,
                 "both PANEL copies have no texture");
    check_eq_u64(track.entries_resolved, 2, "the POST groups still resolve");
    check(stats.triangles_fallback > 0, "fallback triangles were submitted");
    check(stats.pixels_fallback > 0, "fallback pixels reached the frame");
    check_eq_u64(stats.unresolved_hashes, 1,
                 "the region reports one unresolved hash");

    // The bind count when the FIRST entry has no texture. Every unresolved
    // and unusable group shares one fallback binding, and the first entry
    // still counts as a bind; a fixture whose first entry resolves cannot
    // tell that from not counting the first entry at all.
    check_eq_u64(track.texture_binds, list.cost.texture_binds,
                 "binds agree with the draw list cost over an unresolved run");
    check_eq_u64(track.texture_binds, 3,
                 "the fallback run, then each of POST's two textures");

    // The fallback colour is keyed on the group's texture hash, which is
    // the same key the car path uses, so the same missing texture is the
    // same colour in both. Checking that a fallback pixel merely exists
    // cannot see a different key.
    unsigned char expected[4] = {0, 0, 0, 0};
    nfsu::fallback_color(0x0000DEADU, expected);
    bool found = false;
    for (std::uint32_t y = 0; y < frame.height() && !found; ++y) {
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::array<unsigned char, 4> pixel = pixel_at(frame, x, y);
            if (pixel[0] == expected[0] && pixel[1] == expected[1] &&
                pixel[2] == expected[2]) {
                found = true;
                break;
            }
        }
    }
    check(found,
          "a pixel carries fallback_color(0x0000DEAD), which is " +
              std::to_string(expected[0]) + "," + std::to_string(expected[1]) +
              "," + std::to_string(expected[2]));
}

void test_render_a_wrong_scene_is_refused_before_the_frame() {
    // The scene validator runs before Frame::reset, so a caller that passes
    // a corrupt scene gets its old frame back untouched rather than a
    // half-drawn one.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                       nfsu::RenderLimits{}, frame, stats, track,
                                       error),
              "the good render succeeds", error);
    const std::vector<unsigned char> good = frame.rgba();
    check(!good.empty(), "the good render produced pixels");

    nfsu::PreparedTrackScene broken = fixture.scene;
    // Swap two instances, which breaks the ascending order every lookup in
    // this layer relies on.
    check(broken.instances.size() >= 2, "the fixture has two instances");
    std::swap(broken.instances[0], broken.instances[1]);
    check(!nfsu::render_track_draw_list(broken, list, small_options(),
                                        nfsu::RenderLimits{}, frame, stats,
                                        track, error),
          "a scene with unordered instances is refused");
    check(frame.rgba() == good, "the refused render left the frame untouched");
}

// ---------------------------------------------------------------------------
// The shade factor, and the two things it depends on
// ---------------------------------------------------------------------------

void test_render_shading_is_the_flat_model() {
    // Two pinned pixels. Both are exact bytes, derived rather than
    // recorded, because a shade factor is an integer 0..256 and a rounded
    // one is indistinguishable from a right one at any tolerance.
    //
    // The quad's world normal is (0, 1, 1) / sqrt(2). The fixed light
    // (kLightX, kLightY, kLightZ) has length sqrt(1.005), so normalized it
    // is (0.349128, 0.847882, 0.399004) and
    //
    //   d     = (0.847882 + 0.399004) / sqrt(2) = 0.881684
    //   k     = 0.35 + 0.65 * d                 = 0.923095
    //   shade = floor(k * 256 + 0.5)            = 236
    //
    // and src/render.cpp writes (texel * shade + 128) >> 8 per channel,
    // which over the flat texel (255, 120, 180) is (235, 111, 166).
    //
    // Skipping the normalization gives d = 0.883883, k = 0.924524 and
    // shade 237, which is (236, 111, 166): one byte, on one channel. That
    // is the whole visible difference, and it is why this case needs the
    // tilted quad - at a normal of (0, 0, 1) the two shades are both 156.
    const Fixture fixture = tilted();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::RenderOptions options = small_options();
    options.shading = nfsu::ShadingMode::flat;

    nfsu::Frame front;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, front, stats,
                                          track, error),
             "the tilted quad renders", error);
    std::uint32_t px = 0;
    std::uint32_t py = 0;
    check(screen_pixel_of(options.camera, options.width, options.height,
                          nfsu::Vec3{0.0, 0.0, 0.0}, px, py),
          "the tilted quad centre is inside the frame");
    const std::array<unsigned char, 4> lit = pixel_at(front, px, py);
    check(lit[0] == 235 && lit[1] == 111 && lit[2] == 166,
          "the lit face is the derived shade: got " + describe_pixel(lit));

    // The same quad from the other side. The geometric normal is flipped
    // towards the eye first, so it becomes (0, -1, -1) / sqrt(2), its dot
    // with the light is negative, max(0, d) makes it zero and
    //
    //   k = 0.35, shade = floor(0.35 * 256 + 0.5) = 90
    //
    // which over the same texel is (90, 42, 63). Dropping the max gives
    // k = -0.223, clamped to 0 and shade 0, which is black; not flipping
    // the normal at all gives 236 again, which is the lit pixel above. So
    // this one assertion separates three different implementations.
    nfsu::RenderOptions behind = options;
    behind.camera.yaw_deg = 270.0;
    nfsu::Frame back;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, behind,
                                          nfsu::RenderLimits{}, back, stats,
                                          track, error),
             "the tilted quad renders from behind", error);
    check(screen_pixel_of(behind.camera, behind.width, behind.height,
                          nfsu::Vec3{0.0, 0.0, 0.0}, px, py),
          "the tilted quad centre is inside the reverse frame");
    const std::array<unsigned char, 4> unlit = pixel_at(back, px, py);
    check(unlit[0] == 90 && unlit[1] == 42 && unlit[2] == 63,
          "the unlit face is the ambient shade: got " + describe_pixel(unlit));
}

void test_render_uv_orientation() {
    // The four texels of the corner texture, at the four corners of the
    // quad that carries the four UV corners. A swap of u and v, a flipped
    // v, or a transposed row stride moves colour between these four points
    // while leaving every count and every pixel total exactly as it was.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    const nfsu::RenderOptions options = small_options();
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the corner-textured quad renders", error);

    // PANEL's vertex at world (-1, -1) carries (u, v) = (0, 0), and the
    // texture's first texel is at (0, 0), so the lower left corner of the
    // quad is the first texel. The three others follow the same way round.
    struct Corner {
        double x;
        double y;
        unsigned char rgb[3];
        const char* what;
    };
    static const Corner kCorners[4] = {
        {-0.9, -0.9, {200, 0, 0}, "lower left is texel (0, 0)"},
        {0.9, -0.9, {0, 200, 0}, "lower right is texel (1, 0)"},
        {-0.9, 0.9, {0, 0, 200}, "upper left is texel (0, 1)"},
        {0.9, 0.9, {200, 200, 0}, "upper right is texel (1, 1)"},
    };
    for (const Corner& corner : kCorners) {
        std::uint32_t px = 0;
        std::uint32_t py = 0;
        check(screen_pixel_of(options.camera, options.width, options.height,
                              nfsu::Vec3{corner.x, corner.y, 0.0}, px, py),
              std::string(corner.what) + ": the probe is inside the frame");
        const std::array<unsigned char, 4> got = pixel_at(frame, px, py);
        check(got[0] == corner.rgb[0] && got[1] == corner.rgb[1] &&
                  got[2] == corner.rgb[2],
              std::string(corner.what) + ": got " + describe_pixel(got));
    }
}

void test_render_vertex_colour() {
    // Off by default and exact when on. Every fixture vertex carries
    // (255, 128, 64), and src/render.cpp folds it in as
    // (texel * colour + 127) / 255 before the shade factor, so over the
    // flat texel (255, 120, 180) with shading off the result is
    // (255, 60, 45): the red lane is unchanged, the green lane is halved
    // and the blue lane is quartered.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    const std::size_t post = instance_of_mesh(fixture.scene, 1);
    const double x = fixture.scene.instances[post].transform.translation[0];
    nfsu::RenderOptions options = small_options();
    options.camera.target = nfsu::Vec3{x, 0.0, 0.0};

    std::uint32_t px = 0;
    std::uint32_t py = 0;
    check(screen_pixel_of(options.camera, options.width, options.height,
                          nfsu::Vec3{x - 0.25, 0.5, 0.0}, px, py),
          "the vertex-colour probe is inside the frame");

    nfsu::Frame plain;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, plain, stats,
                                          track, error),
             "the plain render succeeds", error);
    const std::array<unsigned char, 4> before = pixel_at(plain, px, py);
    check(before[0] == kFlatR && before[1] == kFlatG && before[2] == kFlatB,
          "vertex colour is off by default: got " + describe_pixel(before));

    options.vertex_color = true;
    nfsu::Frame tinted;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, tinted, stats,
                                          track, error),
             "the vertex-colour render succeeds", error);
    const std::array<unsigned char, 4> after = pixel_at(tinted, px, py);
    check(after[0] == 255 && after[1] == 60 && after[2] == 45,
          "vertex colour multiplies the texel: got " + describe_pixel(after));
}

void test_render_drawn_bounds_match_the_frame() {
    // The reported drawn bounds against a scan of the pixels themselves.
    // The scan reads the colour buffer and not the id buffer the statistic
    // is computed from, so the two are independent measurements of the
    // same rectangle rather than one restating the other.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    // Far enough back that the whole fixture is inside the frame: the
    // assertion below is that the reported rectangle is the geometry's and
    // not the frame's, and geometry clipped at an edge would make the two
    // agree for the wrong reason.
    nfsu::RenderOptions options = small_options();
    options.camera.distance = 14.0;
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the bounds fixture renders", error);
    check(stats.pixels_drawn > 0, "the bounds fixture draws pixels");

    bool any = false;
    std::uint32_t min_x = 0;
    std::uint32_t max_x = 0;
    std::uint32_t min_y = 0;
    std::uint32_t max_y = 0;
    std::uint64_t painted = 0;
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::array<unsigned char, 4> pixel = pixel_at(frame, x, y);
            if (pixel[0] == options.background[0] &&
                pixel[1] == options.background[1] &&
                pixel[2] == options.background[2]) {
                continue;
            }
            ++painted;
            if (!any) {
                min_x = max_x = x;
                min_y = max_y = y;
                any = true;
            } else {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    check(any, "the scan found painted pixels");
    check_eq_u64(painted, stats.pixels_drawn,
                 "the scan counts the reported drawn pixels");
    check_eq_u64(stats.min_x, min_x, "reported min x");
    check_eq_u64(stats.max_x, max_x, "reported max x");
    check_eq_u64(stats.min_y, min_y, "reported min y");
    check_eq_u64(stats.max_y, max_y, "reported max y");
    check(min_x > 0 && min_y > 0 && max_x + 1 < frame.width() &&
              max_y + 1 < frame.height(),
          "the fixture is inside the frame, so the bounds are not the frame");
}

void test_render_reports_an_invalid_mesh() {
    // A mesh the region refuses. src/track.cpp rejects a material whose
    // texture slot is outside the mesh's own hash list, so this mesh is
    // invalid at parse time and prepare_track_region records it. The
    // renderer's `meshes` statistic is the region's total INCLUDING the
    // rejected ones, and `meshes_rendered` is the prepared ones, so a
    // fixture with none of them cannot tell the two apart.
    std::vector<MeshSpec> meshes = fixture_meshes();
    MeshSpec broken;
    broken.name = "BROKEN";
    broken.hash = 0x3003U;
    broken.vertices = {plane_vertex(-1.0F, -1.0F, 0.0F, 0.0F),
                       plane_vertex(1.0F, -1.0F, 1.0F, 0.0F),
                       plane_vertex(1.0F, 1.0F, 1.0F, 1.0F)};
    broken.triangles = {{0, 1, 2}};
    broken.materials = {MaterialSpec{3, 7}};
    broken.texture_hashes = {kTexA};
    meshes.push_back(broken);
    const Fixture fixture = make_fixture(meshes, fixture_sections());
    check_eq_u64(fixture.scene.region.skipped_invalid_meshes.size(), 1,
                 "the region skipped the invalid mesh");

    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the region with an invalid mesh renders", error);
    check_eq_u64(stats.meshes, 3, "meshes counts the invalid one too");
    check_eq_u64(stats.meshes_rendered, 2, "meshes_rendered counts the valid");
    check_eq_u64(stats.meshes_skipped_invalid, 1, "and the skipped one");
    check_eq_u64(track.meshes_drawn, 2, "the frame drew the two valid meshes");
}

void test_bounds_read_every_vertex_of_every_triangle() {
    const Fixture fixture = probe();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::TrackDrawListBounds bounds;
    std::string error;
    check_ok(nfsu::track_draw_list_bounds(fixture.scene, list, bounds, error),
             "the probe bounds are measurable", error);
    // The maximum on x is vertex 1 and the maximum on y is vertex 2, and
    // each is named exactly once in the first triangle, so a walk that read
    // any index twice would lose one of the two.
    check_exact(bounds.drawn.min[0], -1.0, "probe drawn min x");
    check_exact(bounds.drawn.max[0], 2.0, "probe drawn max x");
    check_exact(bounds.drawn.min[1], -1.0, "probe drawn min y");
    check_exact(bounds.drawn.max[1], 3.0, "probe drawn max y");
    check_eq_u64(bounds.drawn.points, 6, "probe drawn points");
}

void test_bounds_are_cleared_before_they_are_measured() {
    // The out-parameter is cleared on entry, so a caller that reuses one
    // struct for two regions measures the second and not the union. A
    // renderer that framed a camera on a stale union would point it
    // somewhere neither region is.
    const Fixture wide = town();
    const Fixture narrow = probe();
    nfsu::TrackDrawListBounds bounds;
    std::string error;
    check_ok(nfsu::track_draw_list_bounds(
                 wide.scene, make_list(wide.scene,
                                       nfsu::TrackDrawOrder::visible_instance),
                 bounds, error),
             "the first measurement succeeds", error);
    check_exact(bounds.drawn.min[0], -3.5, "the first measurement is the town");
    check_ok(nfsu::track_draw_list_bounds(
                 narrow.scene,
                 make_list(narrow.scene,
                           nfsu::TrackDrawOrder::visible_instance),
                 bounds, error),
             "the second measurement succeeds", error);
    check_exact(bounds.drawn.min[0], -1.0,
                "the second measurement replaced the first");
    check_eq_u64(bounds.drawn.points, 6,
                 "and its point count did not accumulate");
    check_eq_u64(bounds.instances, 1, "nor its instance count");
}

void test_render_counts_a_degenerate_triangle() {
    const Fixture fixture = probe();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the probe fixture renders", error);
    check_eq_u64(stats.triangles_submitted, 2, "two triangles are submitted");
    check_eq_u64(stats.triangles_degenerate_source, 1,
                 "one of them has no area at the source");
    check(stats.pixels_drawn > 0, "the other one still covers pixels");
}

void test_render_reports_the_copies_that_drew() {
    // A list over one of three placed instances. The statistic is the
    // copies that reached the frame, not the copies the scene holds, and
    // the two coincide in every fixture that draws all of them.
    const Fixture fixture = town();
    const std::size_t post = instance_of_mesh(fixture.scene, 1);
    const nfsu::TrackDrawList list = list_over(fixture.scene, {post});
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the one-instance list renders", error);
    check_eq_u64(fixture.scene.instances.size(), 3, "the scene holds three");
    check_eq_u64(track.instances_drawn, 1, "the frame drew one");
    check_eq_u64(stats.instances, 1, "and the copy count is the frame's");
    check_eq_u64(stats.instances_exact, 1, "all of it at a real placement");
    check_eq_u64(stats.pixels_blended, 0,
                 "nothing is blended without the retail material state");
}

void test_render_edge_pixels_are_counted() {
    // Geometry that reaches every edge of the frame. The pixel scan runs to
    // the last row and the last column, and a scan one short of either
    // would drop pixels that are there - which no case framing its
    // geometry comfortably inside the frame can see.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::RenderOptions options = small_options();
    options.camera.distance = 1.5;
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the overflowing render succeeds", error);
    std::uint64_t painted = 0;
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::array<unsigned char, 4> pixel = pixel_at(frame, x, y);
            if (pixel[0] != options.background[0] ||
                pixel[1] != options.background[1] ||
                pixel[2] != options.background[2]) {
                ++painted;
            }
        }
    }
    check_eq_u64(painted, static_cast<std::uint64_t>(frame.width()) *
                              frame.height(),
                 "the geometry covers the whole frame");
    check_eq_u64(stats.pixels_drawn, painted, "and every pixel is counted");
    check_eq_u64(stats.min_x, 0, "the drawn bounds reach column 0");
    check_eq_u64(stats.min_y, 0, "the drawn bounds reach row 0");
    check_eq_u64(stats.max_x, frame.width() - 1,
                 "the drawn bounds reach the last column");
    check_eq_u64(stats.max_y, frame.height() - 1,
                 "the drawn bounds reach the last row");
}

void test_render_shading_uses_the_whole_normal() {
    // The quad's world normal is (1, 0, 1) / sqrt(2). Seen from +x it
    // faces the eye and is not flipped, its dot with the fixed light is
    // 0.528994, k = 0.693846 and the shade is 178, which over the flat
    // texel (255, 120, 180) is (177, 83, 125). Seen from -x it is flipped,
    // the dot is negative, the clamp gives the ambient k = 0.35 and shade
    // 90, which is (90, 42, 63).
    //
    // Three faults fail here and nowhere else. A wrong sign in the cross
    // product turns the normal in a direction the flip towards the eye
    // cannot undo, because the normal's first component is no longer zero
    // - in every other fixture it is, and a wrong cross product there is
    // exactly a negated normal, which the flip puts straight back. And the
    // flip decision from +x is dot = 1.3485 - 0.5 = +0.8485, while reading
    // the eye's x with the wrong sign gives 0.3485 - 0.5 = -0.1515: the
    // two disagree about whether to flip, which is the difference between
    // 178 and 90.
    const Fixture fixture = yawed();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::RenderOptions options = small_options();
    options.shading = nfsu::ShadingMode::flat;
    options.camera.yaw_deg = 0.0;
    options.camera.pitch_deg = 0.0;
    options.camera.distance = 1.2;
    options.camera.fov_y_deg = 120.0;

    // The first triangle's centroid, which is inside it by construction.
    const nfsu::PreparedTrackMesh& mesh = fixture.scene.region.meshes[0];
    const nfsu::PreparedTrackInstance& instance = fixture.scene.instances[0];
    nfsu::Vec3 centroid{0.0, 0.0, 0.0};
    for (int k = 0; k < 3; ++k) {
        const std::uint16_t index =
            k == 0 ? mesh.triangles[0].a
                   : (k == 1 ? mesh.triangles[0].b : mesh.triangles[0].c);
        const nfsu::Vec3 world =
            nfsu::track_placed_vertex(mesh.vertices[index], instance.transform);
        centroid.x += world.x / 3.0;
        centroid.y += world.y / 3.0;
        centroid.z += world.z / 3.0;
    }

    nfsu::Frame from_front;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, from_front,
                                          stats, track, error),
             "the yawed quad renders from +x", error);
    std::uint32_t px = 0;
    std::uint32_t py = 0;
    check(screen_pixel_of(options.camera, options.width, options.height,
                          centroid, px, py),
          "the yawed centroid is inside the +x frame");
    const std::array<unsigned char, 4> front = pixel_at(from_front, px, py);
    check(front[0] == 177 && front[1] == 83 && front[2] == 125,
          "the facing side is the lit shade: got " + describe_pixel(front));

    nfsu::RenderOptions behind = options;
    behind.camera.yaw_deg = 180.0;
    nfsu::Frame from_back;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, behind,
                                          nfsu::RenderLimits{}, from_back,
                                          stats, track, error),
             "the yawed quad renders from -x", error);
    check(screen_pixel_of(behind.camera, behind.width, behind.height, centroid,
                          px, py),
          "the yawed centroid is inside the -x frame");
    const std::array<unsigned char, 4> back = pixel_at(from_back, px, py);
    check(back[0] == 90 && back[1] == 42 && back[2] == 63,
          "the away-facing side is the ambient shade: got " +
              describe_pixel(back));
}

void test_render_an_unusable_binding() {
    // BindingKind::unusable is a third state beside resolved and
    // unresolved: a hash that IS claimed, by a record the decoder could
    // not use. It is drawable and it draws the fallback, so a counter that
    // folded it into `unresolved` would be wrong about what a backend is
    // looking at, and no fixture that only ever resolves or fails to
    // resolve can tell the two apart.
    Fixture fixture;
    fixture.geometry_bytes = build_geometry(fixture_meshes(), kGeometryBase);
    fixture.scenery_bytes = build_sections(fixture_sections(), kSceneryBase);
    fixture.geometry = nfsu::parse_track_region(fixture.geometry_bytes.data(),
                                                fixture.geometry_bytes.size(),
                                                kGeometryBase);
    fixture.objects = nfsu::parse_track_objects(fixture.scenery_bytes.data(),
                                                fixture.scenery_bytes.size(),
                                                kSceneryBase);
    fixture.textures = two_textures();
    // Claim kTexA from a record the set could not use, and remove the
    // usable texture that claims it.
    fixture.textures.textures.erase(fixture.textures.textures.begin());
    nfsu::UnusableClaim claim;
    claim.hash = kTexA;
    fixture.textures.unusable_claims.push_back(claim);
    std::string error;
    check_ok(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                     fixture.textures,
                                     nfsu::TrackSceneOptions{}, fixture.scene,
                                     error),
             "the unusable-claim scene builds", error);

    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the unusable-claim scene renders", error);
    check_eq_u64(track.entries_unusable, 3,
                 "the three groups on the claimed hash are unusable");
    check_eq_u64(track.entries_unresolved, 0,
                 "and none of them is counted as unresolved");
    check_eq_u64(track.entries_resolved, 1, "the flat group still resolves");
    check(stats.pixels_fallback > 0,
          "an unusable group draws the fallback, like an unresolved one");
}

void test_render_resets_its_out_parameters() {
    // Both statistics structures are cleared on entry, and nothing pinned
    // that in either direction.
    //
    // The scenario is not hypothetical: a viewer redrawing a frame calls
    // this with the same two objects every time. Without the reset every
    // ++-accumulated counter doubles on the second call, and - worse - the
    // material-state refusal and both validator refusals return EARLY, so a
    // caller that reads the statistics after a refused call gets the
    // previous call's numbers as though they described this one.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    const nfsu::RenderOptions options = small_options();
    std::string error;

    nfsu::Frame fresh_frame;
    nfsu::RenderStats fresh;
    nfsu::TrackRenderStats fresh_track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, fresh_frame,
                                          fresh, fresh_track, error),
             "the reference render succeeds", error);

    // The same two objects, twice. The second call must produce the first
    // call's numbers and not twice them.
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the first of two renders succeeds", error);
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, options,
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the second of two renders succeeds", error);
    check_eq_u64(stats.triangles_submitted, fresh.triangles_submitted,
                 "a re-used RenderStats is not accumulated into");
    check_eq_u64(stats.fragments_written, fresh.fragments_written,
                 "nor is its fragment count");
    check_eq_u64(stats.pixels_drawn, fresh.pixels_drawn,
                 "nor its pixel count");
    check_eq_u64(track.entries, fresh_track.entries,
                 "a re-used TrackRenderStats is not accumulated into");
    check_eq_u64(track.texture_binds, fresh_track.texture_binds,
                 "nor is its bind count");
    check_eq_u64(track.bounds.drawn.points, fresh_track.bounds.drawn.points,
                 "nor the points its bounds visited");

    // An invalid camera is refused before any draw and clears reused stats.
    nfsu::RenderOptions refused = options;
    refused.camera.distance = -1.0;
    check(!nfsu::render_track_draw_list(fixture.scene, list, refused,
                                        nfsu::RenderLimits{}, frame, stats,
                                        track, error),
          "the invalid camera is refused");
    check_eq_u64(stats.triangles_submitted, 0,
                 "a refused call leaves no triangle count behind");
    check_eq_u64(stats.pixels_drawn, 0,
                 "a refused call leaves no pixel count behind");
    check_eq_u64(track.entries, 0,
                 "a refused call leaves no entry count behind");
    check_eq_u64(track.texture_binds, 0,
                 "a refused call leaves no bind count behind");
    check(!track.bounds.drawn.valid,
          "a refused call leaves no bounds behind");
}

void test_render_triangle_ids_start_at_zero() {
    // The triangle id is the running count of submitted triangles, and two
    // things outside this function read it: stats.pixels_fallback indexes
    // its own table by it, and Frame::ids() is what
    // Inspector::draw_of_triangle turns back into a material group. An id
    // that starts at one shifts every lookup by one group boundary, and
    // nothing in the frame or in any count would show it.
    const Fixture fixture = town();
    const nfsu::TrackDrawList list =
        make_list(fixture.scene, nfsu::TrackDrawOrder::visible_instance);
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    nfsu::TrackRenderStats track;
    std::string error;
    check_ok(nfsu::render_track_draw_list(fixture.scene, list, small_options(),
                                          nfsu::RenderLimits{}, frame, stats,
                                          track, error),
             "the id fixture renders", error);
    check(stats.triangles_submitted > 0, "the id fixture submits triangles");

    bool any = false;
    std::uint32_t lowest = 0;
    std::uint32_t highest = 0;
    std::uint64_t past_the_end = 0;
    for (const std::uint32_t id : frame.ids()) {
        if (id == nfsu::kNoTriangle) {
            continue;
        }
        if (id >= stats.triangles_submitted) {
            ++past_the_end;
            continue;
        }
        if (!any) {
            lowest = highest = id;
            any = true;
        } else {
            lowest = std::min(lowest, id);
            highest = std::max(highest, id);
        }
    }
    check(any, "some pixel owns a triangle");
    check_eq_u64(past_the_end, 0,
                 "no pixel owns an id past the submitted count");
    check_eq_u64(lowest, 0, "the first submitted triangle is id 0");
    check(highest < stats.triangles_submitted,
          "and the last id is inside the submitted range: " +
              std::to_string(highest) + " of " +
              std::to_string(stats.triangles_submitted));
}

void test_framing_refuses_a_non_finite_option() {
    // Every framing option, one at a time. A NaN passes BOTH range
    // comparisons - NaN < low and NaN > high are each false - so the
    // finiteness gate is the only thing in front of it, and that gate is
    // one `||` chain: a single one of them turned into `&&` lets a NaN
    // through in three of the four lanes while the fourth still refuses.
    // Driving only one lane cannot see that.
    //
    // This is also what makes the validate_camera self-check at the end of
    // frame_track_bounds observable. A NaN that gets past the gate reaches
    // it, and with both gone orbit_eye produces a NaN eye and a frame of
    // nothing.
    const nfsu::TrackWorldBounds unit = box(-1.0, -1.0, -1.0, 1.0, 1.0, 1.0);
    const double nan_value = std::numeric_limits<double>::quiet_NaN();
    const double infinite = std::numeric_limits<double>::infinity();
    nfsu::OrbitCamera camera;
    std::string error;

    // Both infinities, because finite_value rejects them with two separate
    // comparisons and a fixture that drives only one leaves the other free.
    for (int lane = 0; lane < 4; ++lane) {
        for (int which = 0; which < 3; ++which) {
            const double bad = which == 0 ? nan_value
                                          : (which == 1 ? infinite : -infinite);
            const std::string kind = which == 0 ? "a NaN"
                                                : (which == 1 ? "an infinity"
                                                              : "a negative infinity");
            static const char* const kNames[4] = {"yaw", "pitch", "fov",
                                                  "margin"};
            nfsu::TrackFramingOptions options;
            if (lane == 0) {
                options.yaw_deg = bad;
            } else if (lane == 1) {
                options.pitch_deg = bad;
            } else if (lane == 2) {
                options.fov_y_deg = bad;
            } else {
                options.margin = bad;
            }
            const std::string what =
                kind + " " + kNames[lane] + " is refused";
            check(!nfsu::frame_track_bounds(unit, 1280, 720, options, camera,
                                            error),
                  what);
            check(error.find("must be finite") != std::string::npos,
                  what + " by the finiteness gate, not by something later: " +
                      error);
        }
    }
}

} // namespace

int main() {
    test_fixture_parses();

    test_placed_vertex_identity();
    test_placed_vertex_matches_the_culler_map();
    test_placed_vertex_is_row_major_row_vector();
    test_placed_vertex_adds_the_translation();
    test_scene_transform_divides_the_stored_matrix();

    test_bounds_are_exact();
    test_stored_bounds_exchange_the_negated_axis();
    test_bounds_refuse_a_mismatched_entry();
    test_bounds_skip_a_triangle_with_a_bad_index();
    test_bounds_ignore_an_empty_group();
    test_bounds_read_every_vertex_of_every_triangle();
    test_bounds_are_cleared_before_they_are_measured();

    test_framing_contains_the_box();
    test_framing_numbers();
    test_framing_radius_is_single_rounded();
    test_framing_half_angle_is_single_rounded();
    test_framing_is_a_function_of_its_inputs();
    test_framing_refusals();
    test_framing_a_single_point();
    test_framing_refuses_a_non_finite_option();

    test_render_counts();
    test_render_texture_binds_match_the_draw_list_cost();
    test_render_order_changes_the_binds_not_the_pixels();
    test_render_depth_tie_goes_to_the_later_entry();
    test_blended_track_preserves_depth_for_later_draw();
    test_render_is_deterministic();
    test_render_samples_the_texture();
    test_render_honours_the_options();
    test_render_refusals();
    test_render_an_empty_list();
    test_render_an_unresolved_group();
    test_render_shading_is_the_flat_model();
    test_render_shading_uses_the_whole_normal();
    test_render_an_unusable_binding();
    test_render_uv_orientation();
    test_render_vertex_colour();
    test_render_drawn_bounds_match_the_frame();
    test_render_reports_an_invalid_mesh();
    test_render_counts_a_degenerate_triangle();
    test_render_resets_its_out_parameters();
    test_render_triangle_ids_start_at_zero();
    test_render_reports_the_copies_that_drew();
    test_render_edge_pixels_are_counted();
    test_render_a_wrong_scene_is_refused_before_the_frame();

    if (failures != 0) {
        std::cerr << failures << " track render checks failed\n";
        return 1;
    }
    std::cout << "track render tests passed\n";
    return 0;
}
