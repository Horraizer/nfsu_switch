#include "nfsu/geometry.h"
#include "nfsu/track_draw_list.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

using Bytes = std::vector<unsigned char>;
using Pair = std::pair<std::uint32_t, std::uint32_t>;

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

// Writes a chunk tree at absolute file offsets, so every alignment rule of
// docs/TRACK.md and docs/TRACK_OBJECTS.md applies exactly as it would in a
// retail file.
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
// Geometry fixture: one 0x80134000 region of 24-byte-vertex meshes.
// ---------------------------------------------------------------------------

struct MaterialSpec {
    std::uint32_t index_count = 3;
    std::uint32_t texture_slot = 0;
};

struct MeshSpec {
    std::string name = "MESH";
    std::uint32_t hash = 0x1001U;
    std::uint32_t vertex_count = 3;
    std::uint32_t face_count = 1;
    std::vector<MaterialSpec> materials{MaterialSpec{}};
    std::vector<std::uint32_t> texture_hashes{0xAAAA0001U};
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
    put_u32(metadata, nfsu::kFaceCountOffset, spec.face_count);
    put_u32(metadata, nfsu::kVertexCountOffset, spec.vertex_count);
    writer.append(metadata);
    writer.end();

    writer.begin(nfsu::kVertexBufferId);
    writer.pad_to(nfsu::kVertexDataAlignment);
    Bytes vertices;
    for (std::uint32_t i = 0; i < spec.vertex_count; ++i) {
        append_f32(vertices, static_cast<float>(i));
        append_f32(vertices, static_cast<float>(i) + 0.5F);
        append_f32(vertices, static_cast<float>(i) + 0.25F);
        vertices.push_back(static_cast<unsigned char>(10 + i));
        vertices.push_back(static_cast<unsigned char>(20 + i));
        vertices.push_back(static_cast<unsigned char>(30 + i));
        vertices.push_back(0xFFU);
        append_f32(vertices, static_cast<float>(i) * 0.125F);
        append_f32(vertices, static_cast<float>(i) * 0.0625F);
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
    for (std::uint32_t i = 0; i < spec.face_count; ++i) {
        const std::uint32_t count = spec.vertex_count == 0 ? 1 : spec.vertex_count;
        Bytes triangle;
        append_u16(triangle, static_cast<std::uint16_t>((3 * i) % count));
        append_u16(triangle, static_cast<std::uint16_t>((3 * i + 1) % count));
        append_u16(triangle, static_cast<std::uint16_t>((3 * i + 2) % count));
        writer.append(triangle);
    }
    writer.zero_to(nfsu::kFaceTailAlignment);
    writer.end();

    writer.end();  // 0x80134100
    writer.end();  // 0x80134010
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
// Scenery fixture: one 0x80034100 region of sections.
// ---------------------------------------------------------------------------

struct ObjectSpec {
    std::uint32_t mesh_hash[nfsu::kSceneryObjectMeshSlots] = {0, 0, 0, 0, 0, 0};
    float radius = 5.0F;
};

struct InstanceSpec {
    std::int16_t bounds_min[3] = {0, 0, 0};
    std::int16_t bounds_max[3] = {0, 0, 0};
    std::uint16_t object_index = 0;
    std::uint16_t flags = 0;
    float position[3] = {0.0F, 0.0F, 0.0F};
};

struct NodeSpec {
    std::int16_t bounds_min[3] = {0, 0, 0};
    std::int16_t bounds_max[3] = {0, 0, 0};
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
        const std::int16_t one = static_cast<std::int16_t>(nfsu::kSceneryMatrixOne);
        const std::int16_t identity[9] = {one, 0, 0, 0, one, 0, 0, 0, one};
        for (std::size_t word = 0; word < 9; ++word) {
            append_i16(instances, identity[word]);
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

// A cube of half extent 5 centred on world (100 * (index + 1), 0, 0). The
// world map of docs/FORMATS.md:286 puts x in raw slot 0 and negates z into
// slot 1; the cube is symmetric about both of the other two axes, so the
// raw box below reads the same as the world one.
InstanceSpec row_instance(int index, std::uint16_t object_index) {
    InstanceSpec instance;
    const std::int16_t centre = static_cast<std::int16_t>(100 * (index + 1));
    instance.bounds_min[0] = static_cast<std::int16_t>(centre - 5);
    instance.bounds_min[1] = -5;
    instance.bounds_min[2] = -5;
    instance.bounds_max[0] = static_cast<std::int16_t>(centre + 5);
    instance.bounds_max[1] = 5;
    instance.bounds_max[2] = 5;
    instance.object_index = object_index;
    instance.position[0] = static_cast<float>(centre);
    // A distinct raw flags word per instance, so that a scene carrying a
    // constant there instead of the record's own is visible. Bit 0x10 is
    // avoided: the loader sets and clears it, so it is the one bit of this
    // word that does not mean what the file says.
    instance.flags = static_cast<std::uint16_t>(0x1000 + index);
    return instance;
}

NodeSpec node_over(std::int16_t low, std::int16_t high,
                   const std::vector<std::int16_t>& entries) {
    NodeSpec node;
    node.bounds_min[0] = low;
    node.bounds_min[1] = -5;
    node.bounds_min[2] = -5;
    node.bounds_max[0] = high;
    node.bounds_max[1] = 5;
    node.bounds_max[2] = 5;
    node.entry_count = static_cast<std::uint16_t>(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        node.entries[i] = entries[i];
    }
    return node;
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

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

nfsu::RenderTextureSet two_textures() {
    nfsu::RenderTextureSet set;
    set.textures.push_back(make_texture(0xAAAA0001U, 2));
    set.textures.push_back(make_texture(0xAAAA0002U, 2));
    for (const nfsu::RenderTexture& texture : set.textures) {
        set.decoded_bytes += texture.rgba.size();
    }
    return set;
}

// ---------------------------------------------------------------------------
// The town fixture: three meshes, two sections, seven instances.
//
//   mesh 0  BARRIER_A  hash 0x1001  two groups, on textures 0 and 1
//   mesh 1  BARRIER_B  hash 0x1002  one group, on texture 1
//   mesh 2  LAMP       hash 0x1003  one group, on texture 1
//
// Section 303 places, in order, mesh 0, mesh 1, SKYDOME (a hash the loader
// skips), a hash no mesh claims, mesh 2 and mesh 0 again. Section 410 places
// mesh 1 once. Every instance sits on the world +X axis 100 units apart, so
// a camera looking down it selects a prefix of the row.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kMeshA = 0x1001U;
constexpr std::uint32_t kMeshB = 0x1002U;
constexpr std::uint32_t kMeshLamp = 0x1003U;
constexpr std::uint32_t kMissingHash = 0x00009999U;
constexpr std::uint64_t kGeometryBase = 0;
constexpr std::uint64_t kSceneryBase = 0x2000;

std::vector<MeshSpec> town_meshes() {
    MeshSpec a;
    a.name = "BARRIER_A";
    a.hash = kMeshA;
    a.vertex_count = 6;
    a.face_count = 3;
    a.texture_hashes = {0xAAAA0001U, 0xAAAA0002U};
    a.materials = {MaterialSpec{3, 0}, MaterialSpec{6, 1}};

    MeshSpec b;
    b.name = "BARRIER_B";
    b.hash = kMeshB;
    b.vertex_count = 3;
    b.face_count = 1;
    b.texture_hashes = {0xAAAA0002U};
    b.materials = {MaterialSpec{3, 0}};

    MeshSpec lamp;
    lamp.name = "LAMP";
    lamp.hash = kMeshLamp;
    lamp.vertex_count = 3;
    lamp.face_count = 1;
    lamp.texture_hashes = {0xAAAA0002U};
    lamp.materials = {MaterialSpec{3, 0}};

    return {a, b, lamp};
}

std::vector<SectionSpec> town_sections() {
    SectionSpec first;
    first.number = 303;
    first.objects.resize(5);
    first.objects[0].mesh_hash[0] = kMeshA;
    first.objects[1].mesh_hash[0] = kMeshB;
    first.objects[2].mesh_hash[0] = nfsu::kSkydomeMeshHash;
    first.objects[3].mesh_hash[0] = kMissingHash;
    first.objects[4].mesh_hash[0] = kMeshLamp;
    const std::uint16_t objects[6] = {0, 1, 2, 3, 4, 0};
    for (int i = 0; i < 6; ++i) {
        first.instances.push_back(row_instance(i, objects[i]));
    }
    first.nodes.push_back(node_over(95, 605, {0, 1, 2, -1}));
    first.nodes.push_back(node_over(395, 605, {3, 4, 5}));

    SectionSpec second;
    second.number = 410;
    second.objects.resize(1);
    second.objects[0].mesh_hash[0] = kMeshB;
    second.instances.push_back(row_instance(6, 0));
    second.nodes.push_back(node_over(695, 705, {0}));

    return {first, second};
}

struct Fixture {
    Bytes geometry_bytes;
    Bytes scenery_bytes;
    nfsu::TrackRegionReport geometry;
    nfsu::TrackObjectReport objects;
    nfsu::RenderTextureSet textures;
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
    return fixture;
}

Fixture town() { return make_fixture(town_meshes(), town_sections()); }

// A camera at the world origin looking along +X. Only the far plane moves
// between the cases below, so each visible set is a statement about one
// distance and nothing else.
nfsu::TrackCamera looking_down_the_row(float far_plane) {
    nfsu::TrackCamera camera;
    camera.eye[0] = 0.0F;
    camera.eye[1] = 0.0F;
    camera.eye[2] = 0.0F;
    camera.target[0] = 1000.0F;
    camera.target[1] = 0.0F;
    camera.target[2] = 0.0F;
    camera.near_plane = 1.0F;
    camera.far_plane = far_plane;
    return camera;
}

std::string describe(const nfsu::TrackDrawList& list) {
    std::string text;
    for (const nfsu::TrackDrawListEntry& entry : list.entries) {
        text += " (" + std::to_string(entry.instance) + "," +
                std::to_string(entry.draw) + ")";
    }
    return text.empty() ? std::string(" <empty>") : text;
}

// The whole draw list, in order. Order is part of the claim: a list with the
// same entries in another sequence binds textures a different number of
// times and submits scenery in a different order, so a set comparison would
// not see the difference.
void check_entries(
    const nfsu::TrackDrawList& list,
    const std::vector<Pair>& want,
    const std::string& what) {
    bool same = list.entries.size() == want.size();
    for (std::size_t i = 0; same && i < want.size(); ++i) {
        same = list.entries[i].instance == want[i].first &&
               list.entries[i].draw == want[i].second;
    }
    if (!same) {
        std::string wanted;
        for (const Pair& pair : want) {
            wanted += " (" + std::to_string(pair.first) + "," +
                      std::to_string(pair.second) + ")";
        }
        check(false, what + ": got" + describe(list) + ", want" +
                         (wanted.empty() ? std::string(" <empty>") : wanted));
    }
}

void check_visible(
    const nfsu::TrackCullReport& report,
    const std::vector<Pair>& want,
    const std::string& what) {
    bool same = report.visible.size() == want.size();
    for (std::size_t i = 0; same && i < want.size(); ++i) {
        same = report.visible[i].section_index == want[i].first &&
               report.visible[i].instance_index == want[i].second;
    }
    if (!same) {
        std::string got;
        for (const nfsu::TrackVisibleInstance& item : report.visible) {
            got += " (" + std::to_string(item.section_index) + "," +
                   std::to_string(item.instance_index) + ")";
        }
        check(false, what + ": got" + (got.empty() ? std::string(" <empty>") : got));
    }
}

// ---------------------------------------------------------------------------

void test_fixture_parses() {
    const Fixture fixture = town();
    check(fixture.geometry.ok(), "the geometry fixture parses with no issue");
    check_eq_u64(fixture.geometry.valid_mesh_count(), 3,
                 "the geometry fixture holds three valid meshes");
    check(fixture.objects.ok(), "the scenery fixture parses with no issue");
    check_eq_u64(fixture.objects.valid_section_count(), 2,
                 "the scenery fixture holds two valid sections");
    check_eq_u64(fixture.objects.cost.instances, 7,
                 "the scenery fixture places seven instances");
}

// The prepared region the scene is built on, pinned entry by entry. Every
// later expectation is stated in terms of these four draws, so if the
// prepared region moved underneath this file the failure would be here
// rather than in an arithmetic-looking count somewhere else.
void test_prepared_region_shape() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    const nfsu::PreparedTrackRegion& region = scene.region;
    check_eq_u64(region.meshes.size(), 3, "three meshes are prepared");
    check_eq_u64(region.draws.size(), 4, "four material groups are prepared");
    check_eq_u64(region.textures.size(), 2, "two textures are copied in");
    if (region.draws.size() != 4 || region.meshes.size() != 3) {
        return;
    }
    check(region.meshes[0].name == "BARRIER_A" && region.meshes[0].mesh_hash == kMeshA,
          "prepared mesh 0 is BARRIER_A");
    check(region.meshes[1].name == "BARRIER_B" && region.meshes[1].mesh_hash == kMeshB,
          "prepared mesh 1 is BARRIER_B");
    check(region.meshes[2].name == "LAMP" && region.meshes[2].mesh_hash == kMeshLamp,
          "prepared mesh 2 is LAMP");

    check(region.draws[0].mesh == 0 && region.draws[0].drawable &&
              region.draws[0].index_count == 3 && region.draws[0].texture == 0,
          "draw 0 is BARRIER_A's first group on texture 0");
    check(region.draws[1].mesh == 0 && region.draws[1].drawable &&
              region.draws[1].index_count == 6 && region.draws[1].texture == 1,
          "draw 1 is BARRIER_A's second group on texture 1");
    check(region.draws[2].mesh == 1 && region.draws[2].drawable &&
              region.draws[2].index_count == 3 && region.draws[2].texture == 1,
          "draw 2 is BARRIER_B's only group on texture 1");
    check(region.draws[3].mesh == 2 && region.draws[3].drawable &&
              region.draws[3].index_count == 3 && region.draws[3].texture == 1,
          "draw 3 is LAMP's only group on texture 1");
}

// The placement, instance by instance. Which source instance became which
// prepared instance, which mesh each one names, and which two placed
// nothing and why.
void test_placement_is_exact() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    check_eq_u64(scene.instances.size(), 5, "five instances are placed");
    if (scene.instances.size() != 5) {
        return;
    }
    const struct {
        std::size_t section;
        std::size_t instance;
        std::size_t mesh;
        std::uint32_t hash;
        std::uint32_t number;
        char letter;
    } expected[5] = {
        {0, 0, 0, kMeshA, 303, 'C'},
        {0, 1, 1, kMeshB, 303, 'C'},
        {0, 4, 2, kMeshLamp, 303, 'C'},
        {0, 5, 0, kMeshA, 303, 'C'},
        {1, 0, 1, kMeshB, 410, 'D'},
    };
    for (std::size_t i = 0; i < 5; ++i) {
        const nfsu::PreparedTrackInstance& placed = scene.instances[i];
        check(placed.section_index == expected[i].section &&
                  placed.instance_index == expected[i].instance &&
                  placed.mesh == expected[i].mesh &&
                  placed.mesh_hash == expected[i].hash &&
                  placed.section_number == expected[i].number &&
                  placed.has_section_letter &&
                  placed.section_letter == expected[i].letter,
              "placed instance " + std::to_string(i) + " is section " +
                  std::to_string(expected[i].section) + " instance " +
                  std::to_string(expected[i].instance) + " on mesh " +
                  std::to_string(expected[i].mesh));
        check(placed.slot == 0 && placed.requested_slot == 0 &&
                  !placed.slot_fallback_used,
              "placed instance " + std::to_string(i) +
                  " came from slot 0 without a fallback");
    }

    // The translation is the stored float triple and the linear part is the
    // stored int16 over 8192, which is the identity here. Instance 0 sits at
    // raw slot 0 = 100.
    check(scene.instances[0].transform.translation[0] == 100.0F &&
              scene.instances[0].transform.translation[1] == 0.0F &&
              scene.instances[0].transform.translation[2] == 0.0F,
          "the first instance carries its stored translation");
    check(scene.instances[0].transform.linear[0] == 1.0F &&
              scene.instances[0].transform.linear[4] == 1.0F &&
              scene.instances[0].transform.linear[8] == 1.0F &&
              scene.instances[0].transform.linear[1] == 0.0F,
          "the first instance carries the identity linear part");
    check(scene.instances[0].bounds_min[0] == 95.0F &&
              scene.instances[0].bounds_max[0] == 105.0F,
          "the first instance carries its widened stored box");
    check(scene.instances[0].radius == 5.0F,
          "the first instance carries the object radius unbiased");

    check_eq_u64(scene.unplaced.size(), 2, "two instances place nothing");
    if (scene.unplaced.size() != 2) {
        return;
    }
    check(scene.unplaced[0].section_index == 0 &&
              scene.unplaced[0].instance_index == 2 &&
              scene.unplaced[0].mesh_hash == nfsu::kSkydomeMeshHash &&
              scene.unplaced[0].reason == nfsu::TrackUnplacedReason::skipped_hash,
          "instance 2 is unplaced because the loader skips SKYDOME");
    check(scene.unplaced[1].section_index == 0 &&
              scene.unplaced[1].instance_index == 3 &&
              scene.unplaced[1].mesh_hash == kMissingHash &&
              scene.unplaced[1].reason ==
                  nfsu::TrackUnplacedReason::unresolved_hash,
          "instance 3 is unplaced because no mesh claims its hash");
    check(scene.unplaced_hashes.size() == 2 &&
              scene.unplaced_hashes[0] == kMissingHash &&
              scene.unplaced_hashes[1] == nfsu::kSkydomeMeshHash,
          "the unplaced hashes are distinct and ascending");
}

void test_placement_cost() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    const nfsu::TrackScenePlacementCost& cost = scene.cost;
    check_eq_u64(cost.source_sections, 2, "two source sections");
    check_eq_u64(cost.valid_sections, 2, "two valid sections");
    check_eq_u64(cost.source_instances, 7, "seven source instances");
    check_eq_u64(cost.placed_instances, 5, "five placed instances");
    check_eq_u64(cost.unplaced_empty_slot, 0, "no empty slot");
    check_eq_u64(cost.unplaced_skipped_hash, 1, "one skipped hash");
    check_eq_u64(cost.unplaced_unresolved, 1, "one unresolved hash");
    check_eq_u64(cost.slot_fallbacks, 0, "no slot fallback at slot 0");
    check_eq_u64(cost.placed_hashes, 3, "three distinct placed hashes");
    check_eq_u64(cost.unplaced_hashes, 2, "two distinct unplaced hashes");
    check_eq_u64(cost.drawn_meshes, 3, "every prepared mesh is named");
    check_eq_u64(cost.undrawn_meshes, 0, "no prepared mesh is unnamed");
    // Two copies of mesh 0 at two groups, two of mesh 1 at one group and one
    // of mesh 2 at one: 2 * 2 + 2 * 1 + 1 * 1.
    check_eq_u64(cost.resident_entries, 7, "seven resident draw entries");
    // Mesh 0 draws 1 + 2 triangles, mesh 1 draws 1 and mesh 2 draws 1.
    check_eq_u64(cost.resident_triangles, 9, "nine resident triangles");
    check(cost.has_bounds && cost.bounds_min[0] == 95.0F &&
              cost.bounds_max[0] == 705.0F && cost.bounds_min[1] == -5.0F &&
              cost.bounds_max[2] == 5.0F,
          "the placement bounds span the whole row");

    check(scene.first_instance_of_mesh.size() == 3 &&
              scene.first_instance_of_mesh[0] == 0 &&
              scene.first_instance_of_mesh[1] == 1 &&
              scene.first_instance_of_mesh[2] == 2,
          "the first-instance witnesses name the first instance of each mesh");
    check(scene.lod_slot == 0 && scene.object_region.offset == kSceneryBase,
          "the scene records the slot it was built for and its object region");
}

// A prepared mesh no instance names is drawn zero times, not once. That is
// the car-scene rule that does not carry over: PreparedMeshInstances::copies
// answers 1 for a scene with no instances at all, because a car mesh is
// drawn in its own space. A track mesh with no instance has no place in the
// world and must not appear in any draw list.
void test_unnamed_mesh_is_drawn_zero_times() {
    std::vector<MeshSpec> meshes = town_meshes();
    MeshSpec spare;
    spare.name = "SPARE";
    spare.hash = 0x1004U;
    spare.vertex_count = 3;
    spare.face_count = 1;
    spare.texture_hashes = {0xAAAA0001U};
    spare.materials = {MaterialSpec{3, 0}};
    meshes.push_back(spare);

    const Fixture fixture = make_fixture(meshes, town_sections());
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the scene with a spare mesh is built: " + error);
    check_eq_u64(scene.region.meshes.size(), 4, "four meshes are prepared");
    check_eq_u64(scene.cost.drawn_meshes, 3, "three meshes are named");
    check_eq_u64(scene.cost.undrawn_meshes, 1, "the spare mesh is unnamed");
    check(scene.first_instance_of_mesh.size() == 4 &&
              scene.first_instance_of_mesh[3] == nfsu::kNoHandle,
          "the spare mesh has no first-instance witness");
    // Its group is drawable and it still contributes nothing.
    check(scene.region.draws.size() == 5 && scene.region.draws[4].mesh == 3 &&
              scene.region.draws[4].drawable,
          "the spare mesh's group is drawable");
    check_eq_u64(scene.cost.resident_entries, 7,
                 "the spare mesh adds no resident entry");

    nfsu::TrackCullOptions options;
    options.policy = nfsu::TrackCullPolicy::draw_everything;
    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, options);
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, cull, nfsu::TrackDrawListOptions{},
                                      list, error),
          "the draw list is built: " + error);
    bool drew_spare = false;
    for (const nfsu::TrackDrawListEntry& entry : list.entries) {
        drew_spare = drew_spare || entry.draw == 4;
    }
    check(!drew_spare, "no draw list entry draws the unnamed mesh");
}

void test_instance_lookup() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    check(nfsu::track_scene_instance_of(scene, 0, 0) == 0,
          "section 0 instance 0 is prepared instance 0");
    check(nfsu::track_scene_instance_of(scene, 0, 5) == 3,
          "section 0 instance 5 is prepared instance 3");
    check(nfsu::track_scene_instance_of(scene, 1, 0) == 4,
          "section 1 instance 0 is prepared instance 4");
    check(nfsu::track_scene_instance_of(scene, 0, 2) == nfsu::kNoHandle,
          "an unplaced instance has no handle");
    check(nfsu::track_scene_instance_of(scene, 0, 6) == nfsu::kNoHandle,
          "an instance past the end of a section has no handle");
    check(nfsu::track_scene_instance_of(scene, 2, 0) == nfsu::kNoHandle,
          "a section the scene does not hold has no handle");
}

// The whole region drawn, in both orders. The two hold the same entries and
// bind textures a different number of times, which is the only reason the
// second order exists.
void test_draw_everything_in_both_orders() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    nfsu::TrackCullOptions cull_options;
    cull_options.policy = nfsu::TrackCullPolicy::draw_everything;
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, cull_options);
    check_eq_u64(cull.stats.instances_visible, 7,
                 "the fallback policy makes every instance visible");

    nfsu::TrackDrawList by_instance;
    check(nfsu::build_track_draw_list(scene, cull, nfsu::TrackDrawListOptions{},
                                      by_instance, error),
          "the visible instance order is built: " + error);
    check_entries(by_instance,
                  {{0, 0}, {0, 1}, {1, 2}, {2, 3}, {3, 0}, {3, 1}, {4, 2}},
                  "the whole region in visible instance order");
    check_eq_u64(by_instance.cost.visible, 7, "seven visible instances");
    check_eq_u64(by_instance.cost.visible_placed, 5, "five of them are placed");
    check_eq_u64(by_instance.cost.visible_unplaced, 2, "two place nothing");
    check_eq_u64(by_instance.cost.instances_without_entries, 0,
                 "every placed instance has a drawable group");
    check_eq_u64(by_instance.cost.entries, 7, "seven entries");
    check_eq_u64(by_instance.cost.triangles, 9, "nine triangles");
    check_eq_u64(by_instance.cost.groups, 4, "four distinct groups");
    check_eq_u64(by_instance.cost.meshes, 3, "three distinct meshes");
    // Textures 0, 1, 1, 1, 0, 1, 1: four changes counting the first.
    check_eq_u64(by_instance.cost.texture_binds, 4,
                 "the instance order binds four times");
    check(by_instance.has_cull_provenance &&
              by_instance.requested_policy ==
                  nfsu::TrackCullPolicy::draw_everything &&
              by_instance.policy_held && by_instance.sections_resident == 2 &&
              by_instance.sections_degraded == 0,
          "the list carries the cull report's provenance");

    nfsu::TrackDrawListOptions grouped;
    grouped.order = nfsu::TrackDrawOrder::material_group;
    nfsu::TrackDrawList by_group;
    check(nfsu::build_track_draw_list(scene, cull, grouped, by_group, error),
          "the material group order is built: " + error);
    check_entries(by_group,
                  {{0, 0}, {3, 0}, {0, 1}, {3, 1}, {1, 2}, {4, 2}, {2, 3}},
                  "the whole region in material group order");
    check_eq_u64(by_group.cost.entries, 7, "the same seven entries");
    check_eq_u64(by_group.cost.triangles, 9, "the same nine triangles");
    // Textures 0, 0, 1, 1, 1, 1, 1: two changes.
    check_eq_u64(by_group.cost.texture_binds, 2,
                 "the material group order binds twice");
    check(std::string(nfsu::track_draw_order_name(by_group.order)) ==
              "material group",
          "the order names itself");
}

// The recovered rule, from a camera that sees a prefix of the row. The far
// plane at 250 puts instances 2 to 5 and the whole of section 410 beyond it,
// so the draw list is exactly the first two instances' groups.
void test_frustum_selects_a_prefix() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(250.0F), frustum),
          "the near camera builds a frustum");
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum,
                                nfsu::TrackCullOptions{});
    check_visible(cull, {{0, 0}, {0, 1}},
                  "the near camera sees the first two instances");
    check(cull.policy_held, "no section degraded");

    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, cull, nfsu::TrackDrawListOptions{},
                                      list, error),
          "the near draw list is built: " + error);
    check_entries(list, {{0, 0}, {0, 1}, {1, 2}},
                  "the near camera draws two instances and three groups");
    check_eq_u64(list.cost.visible, 2, "two visible instances");
    check_eq_u64(list.cost.visible_unplaced, 0, "both are placed");
    check_eq_u64(list.cost.triangles, 4, "four triangles");
    check_eq_u64(list.cost.texture_binds, 2, "two texture binds");
    check(list.requested_policy == nfsu::TrackCullPolicy::retail_tree,
          "the list records that the recovered rule produced it");

    // The far plane is what decides it: at 750 the whole row is back.
    nfsu::TrackFrustum wide;
    check(nfsu::track_frustum_look_at(looking_down_the_row(750.0F), wide),
          "the far camera builds a frustum");
    const nfsu::TrackCullReport all =
        nfsu::cull_track_region(fixture.objects, wide,
                                nfsu::TrackCullOptions{});
    nfsu::TrackDrawList everything;
    check(nfsu::build_track_draw_list(scene, all, nfsu::TrackDrawListOptions{},
                                      everything, error),
          "the far draw list is built: " + error);
    check_entries(everything,
                  {{0, 0}, {0, 1}, {1, 2}, {2, 3}, {3, 0}, {3, 1}, {4, 2}},
                  "the far camera draws the whole row");
}

// Residency is the caller's, and it changes the draw list. Section 410 is
// dropped by the no-zone rule (410 % 100 is 10), so the instance it places
// leaves the list even though the camera can see it.
void test_residency_narrows_the_list() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    nfsu::TrackCullOptions options;
    options.residency.rule = nfsu::TrackResidencyRule::no_zone;
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, options);
    check_eq_u64(cull.stats.sections_resident, 1,
                 "the no-zone rule keeps only section 303");

    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, cull, nfsu::TrackDrawListOptions{},
                                      list, error),
          "the no-zone draw list is built: " + error);
    check_entries(list, {{0, 0}, {0, 1}, {1, 2}, {2, 3}, {3, 0}, {3, 1}},
                  "section 410's instance is not drawn");
    check_eq_u64(list.sections_resident, 1, "the list records one resident section");
}

// ---------------------------------------------------------------------------
// The level-of-detail slot
// ---------------------------------------------------------------------------

std::vector<SectionSpec> lod_sections() {
    SectionSpec section;
    section.number = 303;
    section.objects.resize(9);
    // slot 1 filled, so slot 1 places mesh B.
    section.objects[0].mesh_hash[0] = kMeshA;
    section.objects[0].mesh_hash[1] = kMeshB;
    // nothing but slot 0.
    section.objects[1].mesh_hash[0] = kMeshA;
    // slot 4 filled, so slot 4 places mesh B with no fallback.
    section.objects[2].mesh_hash[0] = kMeshA;
    section.objects[2].mesh_hash[4] = kMeshB;
    // slot 4 empty, so slot 4 falls back to the first of slots 0 to 3.
    section.objects[3].mesh_hash[0] = kMeshA;
    // slot 0 names no mesh of the region, so the fallback has nothing to
    // find either.
    section.objects[4].mesh_hash[0] = kMissingHash;
    // The fallback reaches past slot 0: slot 2 is the first of slots 0 to 3
    // that resolves anything, so a chain that stopped at slot 0 would leave
    // this one unplaced.
    section.objects[5].mesh_hash[0] = kMissingHash;
    section.objects[5].mesh_hash[2] = kMeshB;
    // And it stops before slot 5: slots 0 to 3 resolve nothing, so at slot 4
    // this one is unplaced however much mesh slot 5 holds - while at slot 5
    // it is placed from its own slot with no fallback at all.
    section.objects[6].mesh_hash[0] = kMissingHash;
    section.objects[6].mesh_hash[5] = kMeshB;
    // The far end of the chain: only slot 3 resolves, so a chain that
    // stopped short of it would leave this one unplaced.
    section.objects[7].mesh_hash[0] = kMissingHash;
    section.objects[7].mesh_hash[3] = kMeshB;
    // A requested slot that names a hash rather than nothing. The fallback
    // still fires, and the hash the scene records must be the one that
    // resolved, not the one that was asked for.
    section.objects[8].mesh_hash[0] = kMeshA;
    section.objects[8].mesh_hash[4] = kMissingHash;
    for (int i = 0; i < 9; ++i) {
        section.instances.push_back(row_instance(i, static_cast<std::uint16_t>(i)));
    }
    section.nodes.push_back(node_over(95, 905, {0, 1, 2, 3, -1}));
    section.nodes.push_back(node_over(495, 905, {4, 5, 6, 7, 8}));
    return {section};
}

void test_lod_slot_zero() {
    const Fixture fixture = make_fixture(town_meshes(), lod_sections());
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the lod scene is built at slot 0: " + error);
    check_eq_u64(scene.cost.placed_instances, 5, "five instances place at slot 0");
    check_eq_u64(scene.instances.at(0).reflection_mesh, 1,
                 "reflection slot 4 takes the last populated earlier slot independently of main LOD");
    check(scene.instances.at(2).reflection_mesh == 1 && scene.instances.at(2).mesh == 0,
          "reflection slot 4 names mesh B while the main view names A");

    check_eq_u64(scene.cost.slot_fallbacks, 0, "slot 0 has no fallback");
    check_eq_u64(scene.cost.unplaced_unresolved, 4, "four hashes resolve nothing");
    for (const nfsu::PreparedTrackInstance& placed : scene.instances) {
        check(placed.mesh == 0 && placed.slot == 0,
              "every slot 0 placement is mesh A");
    }
}

void test_reflection_loader_uses_last_node() {
    // STREAML1RA.BUN section 0x2B52068, object 1 has these exact hashes.
    // Its 0x60 instance passes the reflection selector; retail uses slot 2.
    constexpr std::uint32_t kFirst = 0x3D6C395BU;
    constexpr std::uint32_t kLast = 0x3D6CC5BCU;
    std::vector<MeshSpec> meshes = town_meshes();
    meshes.resize(2);
    meshes[0].hash = kFirst;
    meshes[1].hash = kLast;
    SectionSpec section;
    section.number = 303;
    section.objects.resize(5);
    section.objects[0].mesh_hash[0] = kFirst;
    section.objects[1].mesh_hash[0] = kFirst;
    section.objects[1].mesh_hash[2] = kLast;
    section.objects[2].mesh_hash[0] = kFirst;
    section.objects[2].mesh_hash[4] = kMissingHash;
    section.objects[3].mesh_hash[0] = kFirst;
    section.objects[3].mesh_hash[2] = kMissingHash;
    section.objects[4].mesh_hash[0] = kFirst;
    section.objects[4].mesh_hash[3] = kLast;
    for (int i = 0; i < 3; ++i) {
        InstanceSpec instance = row_instance(i, static_cast<std::uint16_t>(i + 1));
        instance.flags = 0x60;
        section.instances.push_back(instance);
    }
    InstanceSpec slot_three_instance = row_instance(3, 4);
    slot_three_instance.flags = 0x60;
    section.instances.push_back(slot_three_instance);
    section.nodes.push_back(node_over(95, 405, {0, 1, 2, 3}));
    const Fixture fixture = make_fixture(meshes, {section});
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "reflection loader fixture builds: " + error);
    check_eq_u64(scene.instances.size(), 4,
                 "reflection loader fixture places all four instances");
    if (scene.instances.size() != 4) return;
    check(scene.instances[0].mesh_hash == kFirst &&
              scene.instances[0].reflection_mesh == 1 &&
              scene.region.meshes[scene.instances[0].reflection_mesh].mesh_hash == kLast,
          "retail object 1 reflects slot 2 rather than slot 0");
    check(scene.instances[1].reflection_mesh == nfsu::kNoReflectionMesh,
          "unresolved slot 4 retains its node and never falls back");
    check(scene.instances[2].reflection_mesh == nfsu::kNoReflectionMesh,
          "the last earlier node wins even when its hash is unresolved");
    check(scene.instances[3].reflection_mesh == 1,
          "slot 3 is included when it is the last earlier node");
}

// Slot 1 has no recovered fallback: an object that leaves it empty places
// nothing, and the reason names the empty slot rather than a hash.
void test_lod_slot_one_has_no_fallback() {
    const Fixture fixture = make_fixture(town_meshes(), lod_sections());
    nfsu::TrackSceneOptions options;
    options.lod_slot = 1;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, options, scene, error),
          "the lod scene is built at slot 1: " + error);
    check_eq_u64(scene.cost.placed_instances, 1, "one instance places at slot 1");
    check_eq_u64(scene.cost.slot_fallbacks, 0, "slot 1 never falls back");
    check_eq_u64(scene.cost.unplaced_empty_slot, 8, "eight slots are empty");
    check_eq_u64(scene.cost.unplaced_unresolved, 0,
                 "no slot 1 hash fails to resolve");
    check(scene.unplaced_hashes.empty(),
          "an empty slot names no hash, so the unplaced hash list is empty");
    if (scene.instances.size() == 1) {
        check(scene.instances[0].mesh == 1 && scene.instances[0].slot == 1 &&
                  !scene.instances[0].slot_fallback_used,
              "the one slot 1 placement is mesh B, taken from slot 1 itself");
    }
}

// 0x00445A81 and 0x00445A8F: slot 4 falls back to the first solid slots 0
// to 3 resolved, and slot 4's own hash wins when it has one.
void test_lod_slot_four_falls_back() {
    const Fixture fixture = make_fixture(town_meshes(), lod_sections());
    nfsu::TrackSceneOptions options;
    options.lod_slot = 4;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, options, scene, error),
          "the lod scene is built at slot 4: " + error);
    check_eq_u64(scene.cost.placed_instances, 7, "seven instances place at slot 4");
    check_eq_u64(scene.cost.slot_fallbacks, 6, "six of them fell back");
    check_eq_u64(scene.cost.unplaced_empty_slot, 2,
                 "the two whose chains resolve nothing stay unplaced");
    if (scene.instances.size() != 7) {
        return;
    }
    check(scene.instances[0].mesh == 0 && scene.instances[0].slot == 0 &&
              scene.instances[0].slot_fallback_used &&
              scene.instances[0].requested_slot == 4,
          "object 0 falls back from slot 4 to slot 0");
    check(scene.instances[2].mesh == 1 && scene.instances[2].slot == 4 &&
              !scene.instances[2].slot_fallback_used,
          "object 2 takes its own slot 4 with no fallback");
    // The chain walks slots 0 to 3 in order and stops at the first that
    // resolves, so this one lands on slot 2 rather than on slot 0.
    check(scene.instances[4].mesh == 1 && scene.instances[4].slot == 2 &&
              scene.instances[4].slot_fallback_used &&
              scene.instances[4].instance_index == 5,
          "object 5 falls back past slot 0 to slot 2");
    // The failure keeps the requested slot's own reason, which is that
    // slot 4 is empty, not that slot 0's hash resolved nothing. Object 6
    // fills slot 5, which the chain never reaches.
    check(scene.unplaced.size() == 2 &&
              scene.unplaced[0].slot == 4 && scene.unplaced[0].mesh_hash == 0 &&
              scene.unplaced[0].reason == nfsu::TrackUnplacedReason::empty_slot &&
              scene.unplaced[1].instance_index == 6,
          "the unplaced instances report the requested slot and its reason");
}

// Slot 5 is the other half of the recovered fallback, and it behaves
// differently from slot 4 on this fixture: object 6 fills slot 5 itself, so
// it is placed from its own hash while six others fall back. Dropping the
// slot 5 arm of the rule leaves only that one placed.
void test_lod_slot_five_falls_back_too() {
    const Fixture fixture = make_fixture(town_meshes(), lod_sections());
    nfsu::TrackSceneOptions options;
    options.lod_slot = 5;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, options, scene, error),
          "the lod scene is built at slot 5: " + error);
    check_eq_u64(scene.cost.placed_instances, 8, "eight instances place at slot 5");
    check_eq_u64(scene.cost.slot_fallbacks, 7, "seven of them fell back");
    check_eq_u64(scene.cost.unplaced_empty_slot, 1, "one chain resolves nothing");
    if (scene.instances.size() != 8) {
        return;
    }
    // Object 6 is instance 6, and it is the only one taking its own slot.
    const nfsu::PreparedTrackInstance* own = nullptr;
    for (const nfsu::PreparedTrackInstance& placed : scene.instances) {
        if (!placed.slot_fallback_used) {
            own = &placed;
        }
    }
    check(own != nullptr && own->instance_index == 6 && own->slot == 5 &&
              own->requested_slot == 5 && own->mesh == 1,
          "object 6 is placed from slot 5 itself");
}

// The chain walks all four of slots 0 to 3. Object 7 resolves only at slot
// 3, so a chain that stopped anywhere earlier would leave it unplaced.
void test_lod_chain_reaches_slot_three() {
    const Fixture fixture = make_fixture(town_meshes(), lod_sections());
    nfsu::TrackSceneOptions options;
    options.lod_slot = 4;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, options, scene, error),
          "the lod scene is built at slot 4: " + error);
    const nfsu::PreparedTrackInstance* seven = nullptr;
    for (const nfsu::PreparedTrackInstance& placed : scene.instances) {
        if (placed.instance_index == 7) {
            seven = &placed;
        }
    }
    check(seven != nullptr && seven->slot == 3 && seven->slot_fallback_used &&
              seven->mesh == 1,
          "object 7 falls back all the way to slot 3");
    // And the chain stops there: object 6 fills slot 5 and nothing else, so
    // a chain that ran past slot 3 would place it.
    for (const nfsu::TrackUnplacedInstance& miss : scene.unplaced) {
        check(miss.instance_index == 4 || miss.instance_index == 6,
              "only objects 4 and 6 are unplaced at slot 4");
    }
    check_eq_u64(scene.unplaced.size(), 2, "two are unplaced at slot 4");
}

// The hash a placement records is the one that resolved, not the one the
// caller asked for. Object 8 asks slot 4, which names a hash no mesh
// claims, and lands on slot 0; counting the requested hash instead would
// put a hash that resolves nothing into the placed set.
void test_placed_hashes_are_the_resolved_ones() {
    const Fixture fixture = make_fixture(town_meshes(), lod_sections());
    nfsu::TrackSceneOptions options;
    options.lod_slot = 4;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, options, scene, error),
          "the lod scene is built at slot 4: " + error);
    check_eq_u64(scene.cost.placed_hashes, 2,
                 "only the two hashes that resolved are counted");
    const nfsu::PreparedTrackInstance* eight = nullptr;
    for (const nfsu::PreparedTrackInstance& placed : scene.instances) {
        if (placed.instance_index == 8) {
            eight = &placed;
        }
    }
    check(eight != nullptr && eight->mesh_hash == kMeshA && eight->slot == 0 &&
              eight->requested_slot == 4 && eight->slot_fallback_used,
          "object 8 records the hash that resolved, from slot 0");
}

void test_lod_slot_must_exist() {
    const Fixture fixture = town();
    nfsu::TrackSceneOptions options;
    options.lod_slot = nfsu::kSceneryObjectMeshSlots;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(!nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                   fixture.textures, options, scene, error),
          "a slot outside the six is refused");
    check(error.find("outside") != std::string::npos,
          "the refusal says the slot is outside the record: " + error);
    check(scene.instances.empty() && scene.region.meshes.empty(),
          "a refused scene is left empty");
}

// ---------------------------------------------------------------------------
// Ownership, determinism and refusals
// ---------------------------------------------------------------------------

// The scene owns every byte a backend reads, so the reports it was built
// from may be destroyed afterwards.
void test_scene_outlives_its_inputs() {
    nfsu::PreparedTrackScene scene;
    std::string error;
    {
        const Fixture fixture = town();
        check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                      fixture.textures,
                                      nfsu::TrackSceneOptions{}, scene, error),
              "the town scene is built: " + error);
    }
    check(nfsu::validate_prepared_track_scene(scene, error),
          "the scene still validates after its inputs are gone: " + error);
    check_eq_u64(scene.instances.size(), 5, "it still holds five instances");
    check(scene.region.meshes.size() == 3 &&
              scene.region.meshes[0].vertices.size() == 6,
          "it still holds its own copy of the vertices");
    check(scene.region.textures.size() == 2 &&
              scene.region.textures[0].rgba.size() == 16,
          "it still holds its own copy of the textures");
}

bool same_scene(const nfsu::PreparedTrackScene& a,
                const nfsu::PreparedTrackScene& b) {
    if (a.lod_slot != b.lod_slot || a.instances.size() != b.instances.size() ||
        a.instance_handles != b.instance_handles ||
        a.unplaced_hashes != b.unplaced_hashes ||
        a.first_instance_of_mesh != b.first_instance_of_mesh ||
        a.unplaced.size() != b.unplaced.size() ||
        a.sections.size() != b.sections.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.instances.size(); ++i) {
        const nfsu::PreparedTrackInstance& x = a.instances[i];
        const nfsu::PreparedTrackInstance& y = b.instances[i];
        if (x.mesh != y.mesh || x.section_index != y.section_index ||
            x.instance_index != y.instance_index || x.slot != y.slot ||
            x.mesh_hash != y.mesh_hash ||
            x.slot_fallback_used != y.slot_fallback_used) {
            return false;
        }
        for (std::size_t k = 0; k < 9; ++k) {
            if (x.transform.linear[k] != y.transform.linear[k]) {
                return false;
            }
        }
        for (std::size_t k = 0; k < 3; ++k) {
            if (x.transform.translation[k] != y.transform.translation[k] ||
                x.bounds_min[k] != y.bounds_min[k] ||
                x.bounds_max[k] != y.bounds_max[k]) {
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < a.unplaced.size(); ++i) {
        if (a.unplaced[i].section_index != b.unplaced[i].section_index ||
            a.unplaced[i].instance_index != b.unplaced[i].instance_index ||
            a.unplaced[i].mesh_hash != b.unplaced[i].mesh_hash ||
            a.unplaced[i].reason != b.unplaced[i].reason) {
            return false;
        }
    }
    return true;
}

void test_build_is_deterministic() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene first;
    nfsu::PreparedTrackScene second;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  first, error),
          "the first build succeeds: " + error);
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  second, error),
          "the second build succeeds: " + error);
    check(same_scene(first, second), "two builds of one input agree");

    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum,
                                nfsu::TrackCullOptions{});
    nfsu::TrackDrawList one;
    nfsu::TrackDrawList two;
    nfsu::TrackDrawListOptions options;
    options.order = nfsu::TrackDrawOrder::material_group;
    check(nfsu::build_track_draw_list(first, cull, options, one, error) &&
              nfsu::build_track_draw_list(second, cull, options, two, error),
          "both draw lists are built: " + error);
    bool same = one.entries.size() == two.entries.size();
    for (std::size_t i = 0; same && i < one.entries.size(); ++i) {
        same = one.entries[i].instance == two.entries[i].instance &&
               one.entries[i].draw == two.entries[i].draw;
    }
    check(same && one.cost.texture_binds == two.cost.texture_binds,
          "two draw lists of one visible set agree");
}

// The twin claims BARRIER_A's hash and has its exact shape - same vertex
// and face counts, same two material groups, same textures. Only its
// position in the region differs, which is what makes it the fixture for
// the lowest-mesh rule: every count a validator recomputes is the same
// whichever of the two an instance binds, so nothing but the rule itself
// can tell a wrong binding from a right one.
std::vector<MeshSpec> twin_meshes() {
    std::vector<MeshSpec> meshes = town_meshes();
    MeshSpec twin = meshes[0];
    twin.name = "BARRIER_A_TWIN";
    meshes.push_back(twin);
    return meshes;
}

// A duplicate mesh hash binds to the lowest-numbered valid mesh, which is
// what resolve_track_objects binds, so the two layers never disagree about
// an instance.
void test_duplicate_hash_binds_the_lowest_mesh() {
    const Fixture fixture = make_fixture(twin_meshes(), town_sections());
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the scene with a duplicate hash is built: " + error);
    check(scene.region.meshes.size() == 4 &&
              scene.region.meshes[3].name == "BARRIER_A_TWIN" &&
              scene.region.meshes[3].mesh_hash == kMeshA,
          "the twin is prepared as mesh 3 and claims the same hash");
    // Asserted before the guard, or a fixture that placed nothing would
    // skip the comparison below and leave this test passing vacuously.
    check_eq_u64(scene.instances.size(), 5, "the twin scene places five instances");
    if (scene.instances.size() != 5) {
        return;
    }
    check(scene.instances[0].mesh == 0,
          "the instance binds the lowest mesh claiming the hash, not the twin");

    const nfsu::TrackObjectResolution resolution =
        nfsu::resolve_track_objects(fixture.objects, fixture.geometry);
    std::size_t bound = nfsu::kNoHandle;
    for (const nfsu::TrackObjectBinding& binding : resolution.bindings) {
        if (binding.hash == kMeshA && binding.resolved &&
            bound == nfsu::kNoHandle) {
            bound = binding.mesh_index;
        }
    }
    // Compared by identity, not by number. resolve_track_objects returns a
    // position in TrackRegionReport::meshes and this layer returns one in
    // PreparedTrackRegion::meshes; the two coincide only for a region with
    // no invalid mesh, so an equality between the numbers would be a
    // statement about the fixture rather than about the rule.
    check(bound != nfsu::kNoHandle &&
              bound < fixture.geometry.meshes.size() &&
              fixture.geometry.meshes[bound].name ==
                  scene.region.meshes[scene.instances[0].mesh].name,
          "resolve_track_objects binds the same mesh for that hash");
}

void test_draw_list_refuses_a_foreign_visible_set() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    nfsu::TrackDrawList list;
    std::vector<nfsu::TrackVisibleInstance> visible(1);
    visible[0].section_index = 7;
    visible[0].instance_index = 0;
    check(!nfsu::build_track_draw_list(scene, visible,
                                       nfsu::TrackDrawListOptions{}, list, error),
          "a visible entry from a section the scene does not hold is refused");
    check(list.entries.empty(), "a refused draw list is left empty");

    visible[0].section_index = 0;
    visible[0].instance_index = 9;
    check(!nfsu::build_track_draw_list(scene, visible,
                                       nfsu::TrackDrawListOptions{}, list, error),
          "a visible entry past the end of a section is refused");

    visible.resize(2);
    visible[0].section_index = 0;
    visible[0].instance_index = 0;
    visible[1].section_index = 0;
    visible[1].instance_index = 0;
    check(!nfsu::build_track_draw_list(scene, visible,
                                       nfsu::TrackDrawListOptions{}, list, error),
          "a repeated visible entry is refused");
    check(error.find("twice") != std::string::npos,
          "the refusal says the entry appears twice: " + error);
}

// An explicit visible set carries no cull provenance, and the list says so
// rather than pretending the defaults mean something.
void test_explicit_visible_set_carries_no_provenance() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    std::vector<nfsu::TrackVisibleInstance> visible(2);
    visible[0].section_index = 1;
    visible[0].instance_index = 0;
    visible[1].section_index = 0;
    visible[1].instance_index = 0;
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, visible,
                                      nfsu::TrackDrawListOptions{}, list, error),
          "an explicit visible set builds a list: " + error);
    // The visible set's own order, which is not ascending in anything.
    check_entries(list, {{4, 2}, {0, 0}, {0, 1}},
                  "the list follows the order it was given");
    check(!list.has_cull_provenance, "the list carries no cull provenance");
    check(list.sections_resident == 0 && list.sections_degraded == 0 &&
              list.policy_held,
          "the provenance fields are left at their defaults");
}

void test_draw_list_limits() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum,
                                nfsu::TrackCullOptions{});

    nfsu::TrackDrawListOptions options;
    options.limits.max_entries = 6;
    nfsu::TrackDrawList list;
    check(!nfsu::build_track_draw_list(scene, cull, options, list, error),
          "a list that would hold seven entries is refused at six");
    check(error.find("entries") != std::string::npos,
          "the refusal names the entry limit: " + error);

    options.limits.max_entries = 7;
    check(nfsu::build_track_draw_list(scene, cull, options, list, error),
          "seven entries are accepted at seven: " + error);

    options.limits.max_triangles = 8;
    check(!nfsu::build_track_draw_list(scene, cull, options, list, error),
          "a list that would draw nine triangles is refused at eight");
    check(error.find("triangles") != std::string::npos,
          "the refusal names the triangle limit: " + error);
}

void test_placement_limits() {
    const Fixture fixture = town();
    nfsu::TrackSceneOptions options;
    options.placement.max_instances = 6;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(!nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                   fixture.textures, options, scene, error),
          "seven source instances are refused at six");
    check(error.find("instances") != std::string::npos,
          "the refusal names the instance limit: " + error);

    options.placement.max_instances = 7;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, options, scene, error),
          "seven are accepted at seven: " + error);

    options.placement.max_sections = 1;
    check(!nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                   fixture.textures, options, scene, error),
          "two valid sections are refused at one");
    check(error.find("sections") != std::string::npos,
          "the refusal names the section limit: " + error);

    // A region limit reaches this layer through prepare_track_region.
    nfsu::TrackSceneOptions tight;
    tight.region.max_draws = 3;
    check(!nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                   fixture.textures, tight, scene, error),
          "a region limit refuses the scene too");
    check(error.find("material groups") != std::string::npos,
          "the region's own refusal reaches the caller: " + error);
}

// ---------------------------------------------------------------------------
// The validators, against a scene and a list edited under them
// ---------------------------------------------------------------------------

void test_validate_scene_rejects_tampering() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene good;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  good, error),
          "the town scene is built: " + error);
    check(nfsu::validate_prepared_track_scene(good, error),
          "the untouched scene validates: " + error);

    {
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].mesh = bad.region.meshes.size();
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance naming a mesh outside the region is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].transform.translation[1] =
            std::numeric_limits<float>::infinity();
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance with a non-finite transform is rejected");
    }
    {
        // The duplicate-hash rule: mesh 0 and mesh 2 are different meshes,
        // so rebinding the hash to the wrong one must not pass.
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].mesh = 2;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance bound to a mesh that does not claim its hash is"
              " rejected");
    }
    {
        // Swapping two instances alone breaks four invariants at once, so
        // the handle table and the mesh witnesses are swapped with them:
        // then the order is the only thing left wrong, and removing the
        // order check is what this catches.
        nfsu::PreparedTrackScene bad = good;
        std::swap(bad.instances[0], bad.instances[1]);
        std::swap(bad.instance_handles[0], bad.instance_handles[1]);
        std::swap(bad.first_instance_of_mesh[0], bad.first_instance_of_mesh[1]);
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "instances out of ascending order are rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instance_handles[0] = nfsu::kNoHandle;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance no handle names is rejected");
    }
    {
        // Two handles exchanged: every count still adds up and every
        // instance is still named once, so only the check that a handle
        // names the instance belonging at its own position is left.
        nfsu::PreparedTrackScene bad = good;
        std::swap(bad.instance_handles[0], bad.instance_handles[1]);
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a handle naming an instance of another position is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.sections[1].first_handle = 0;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a section whose handle slice does not follow the previous one"
              " is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.sections[0].placed = 3;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a section that miscounts its placed instances is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.unplaced[0].reason = nfsu::TrackUnplacedReason::unresolved_hash;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a skipped hash relabelled unresolved is rejected");
    }
    {
        // The other direction, with the two counts exchanged so that the
        // cost statistics still add up: what is left wrong is a hash the
        // loader does not skip being called skipped.
        nfsu::PreparedTrackScene bad = good;
        bad.unplaced[1].reason = nfsu::TrackUnplacedReason::skipped_hash;
        bad.cost.unplaced_skipped_hash = 2;
        bad.cost.unplaced_unresolved = 0;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an ordinary hash relabelled as one the loader skips is"
              " rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.unplaced_hashes.pop_back();
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an unplaced hash list that misses a hash is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.first_instance_of_mesh[0] = 3;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a witness that is not the first instance of its mesh is"
              " rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.cost.resident_triangles += 1;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a cost statistic that does not match the scene is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.cost.bounds_max[0] = 1000.0F;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "placement bounds that do not match the instances are rejected");
    }
    {
        // With the fallback total moved to match, the claim itself is the
        // only thing left wrong.
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].slot_fallback_used = true;
        bad.cost.slot_fallbacks = 1;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a fallback claimed for a slot that has none is rejected");
    }
    {
        // The unplaced entries name the scene's slot too, so they move with
        // it: what is left is instances that requested another one.
        nfsu::PreparedTrackScene bad = good;
        bad.lod_slot = 1;
        for (nfsu::TrackUnplacedInstance& miss : bad.unplaced) {
            miss.slot = 1;
        }
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a scene whose instances name another slot is rejected");
    }
    {
        // The region validator still runs underneath. The raw material
        // record is a region-level invariant that nothing in this layer
        // recomputes, so only that validator can catch it.
        nfsu::PreparedTrackScene bad = good;
        bad.region.draws[0].raw[12] =
            static_cast<unsigned char>(bad.region.draws[0].raw[12] ^ 0xFFU);
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a tampered prepared region is rejected");
    }
    {
        // The scene's own slot, with every instance and every unplaced
        // entry moved to match, so the range check on it is what is left.
        nfsu::PreparedTrackScene bad = good;
        bad.lod_slot = nfsu::kSceneryObjectMeshSlots;
        for (nfsu::PreparedTrackInstance& placed : bad.instances) {
            placed.requested_slot = nfsu::kSceneryObjectMeshSlots;
            placed.slot = nfsu::kSceneryObjectMeshSlots;
        }
        for (nfsu::TrackUnplacedInstance& miss : bad.unplaced) {
            miss.slot = nfsu::kSceneryObjectMeshSlots;
        }
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a scene built for a slot outside the record is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.first_instance_of_mesh.pop_back();
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a witness table that does not cover every mesh is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instance_handles[0] = bad.instances.size();
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a handle naming an instance past the end is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].radius = std::numeric_limits<float>::quiet_NaN();
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance with a radius that is not finite is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].bounds_max[2] =
            std::numeric_limits<float>::infinity();
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance with a bound that is not finite is rejected");
    }
    {
        // A hash the loader skips can never have placed a mesh, whatever
        // the region holds.
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].mesh_hash = nfsu::kSkydomeMeshHash;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance claiming to be placed by a skipped hash is"
              " rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].mesh_hash = 0;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance claiming to be placed by hash 0 is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.unplaced[0].slot = 1;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an unplaced entry naming another slot is rejected");
    }
    {
        // An unplaced entry that names a position the handle table says was
        // placed: the two halves of the same fact disagreeing.
        nfsu::PreparedTrackScene bad = good;
        bad.unplaced[0].instance_index = 0;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an unplaced entry naming a placed position is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.unplaced[0].reason = nfsu::TrackUnplacedReason::empty_slot;
        bad.cost.unplaced_empty_slot = 1;
        bad.cost.unplaced_skipped_hash = 0;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an empty slot that names a hash is rejected");
    }
    {
        // The section provenance an instance carries, against the
        // section table it belongs to. Nothing else in the structure
        // pins these three, and a backend reads section_number.
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].section_number = 999;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance whose section number its section contradicts"
              " is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].section_letter = 'Z';
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance whose section letter its section contradicts"
              " is rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.instances[0].has_section_letter = false;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "an instance denying a section letter its section has is"
              " rejected");
    }
    {
        // And the other side of the same pair.
        nfsu::PreparedTrackScene bad = good;
        bad.sections[0].number = 777;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a section whose number its instances contradict is"
              " rejected");
    }
    {
        nfsu::PreparedTrackScene bad = good;
        bad.cost.has_bounds = false;
        check(!nfsu::validate_prepared_track_scene(bad, error),
              "a scene that places instances and claims no bounds is"
              " rejected");
    }
}

// The lowest-mesh rule, isolated. The twin has BARRIER_A's hash and its
// exact shape, so rebinding an instance to it changes no count a validator
// recomputes: the witnesses and the drawn-mesh totals are moved with it, and
// what is left wrong is only that the instance bound the higher of the two
// meshes claiming its hash.
void test_validate_scene_rejects_the_wrong_duplicate() {
    const Fixture fixture = make_fixture(twin_meshes(), town_sections());
    nfsu::PreparedTrackScene good;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  good, error),
          "the twin scene is built: " + error);
    check(nfsu::validate_prepared_track_scene(good, error),
          "the untouched twin scene validates: " + error);
    if (good.instances.size() != 5 || good.first_instance_of_mesh.size() != 4) {
        check(false, "the twin scene has the shape this test needs");
        return;
    }

    nfsu::PreparedTrackScene bad = good;
    bad.instances[0].mesh = 3;
    // Instance 3 is the other copy of BARRIER_A, so it becomes mesh 0's
    // first instance, and instance 0 becomes the twin's.
    bad.first_instance_of_mesh[0] = 3;
    bad.first_instance_of_mesh[3] = 0;
    bad.cost.drawn_meshes = 4;
    bad.cost.undrawn_meshes = 0;
    check(!nfsu::validate_prepared_track_scene(bad, error),
          "an instance bound to the higher of two meshes claiming its hash"
          " is rejected");
}

// A group with no texture binds a fallback, and the first entry of a list
// counts as a bind whether or not it resolved anything. A sentinel that a
// real handle could equal would lose that first bind.
void test_unresolved_group_binds_a_fallback() {
    MeshSpec mesh;
    mesh.name = "FENCE";
    mesh.hash = kMeshA;
    mesh.vertex_count = 6;
    mesh.face_count = 2;
    // Slot 0 names a hash the texture set does not hold, so the first group
    // of the first mesh is unresolved and still drawable.
    mesh.texture_hashes = {0xBBBB0001U, 0xAAAA0001U};
    mesh.materials = {MaterialSpec{3, 0}, MaterialSpec{3, 1}};

    SectionSpec section;
    section.number = 303;
    section.objects.resize(1);
    section.objects[0].mesh_hash[0] = kMeshA;
    section.instances.push_back(row_instance(0, 0));
    section.nodes.push_back(node_over(95, 105, {0}));

    const Fixture fixture = make_fixture({mesh}, {section});
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the unresolved-group scene is built: " + error);
    check(scene.region.draws.size() == 2 &&
              scene.region.draws[0].binding == nfsu::BindingKind::unresolved &&
              scene.region.draws[0].drawable &&
              scene.region.draws[1].binding == nfsu::BindingKind::resolved,
          "the first group is unresolved and drawable, the second resolved");

    std::vector<nfsu::TrackVisibleInstance> visible(1);
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, visible,
                                      nfsu::TrackDrawListOptions{}, list, error),
          "the list is built: " + error);
    check_entries(list, {{0, 0}, {0, 1}},
                  "both groups of the one instance are drawn");
    // The fallback binding for the first entry, then the resolved texture.
    check_eq_u64(list.cost.texture_binds, 2,
                 "the fallback binding of the first entry counts as a bind");
}

void test_validate_draw_list_rejects_tampering() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);
    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum,
                                nfsu::TrackCullOptions{});
    nfsu::TrackDrawList good;
    check(nfsu::build_track_draw_list(scene, cull, nfsu::TrackDrawListOptions{},
                                      good, error),
          "the reference draw list is built: " + error);
    check(nfsu::validate_track_draw_list(scene, good, error),
          "the untouched draw list validates: " + error);

    {
        // Draw 3 is LAMP's group and instance 4 draws BARRIER_B. It has the
        // same texture and the same triangle count as the group it replaces,
        // so every statistic still matches and only the mesh the entry
        // claims to draw is wrong.
        nfsu::TrackDrawList bad = good;
        check(bad.entries.size() == 7 && bad.entries[6].draw == 2,
              "the reference list has the shape this tamper needs");
        bad.entries[6].draw = 3;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "an entry drawing one mesh's group with another mesh's instance"
              " is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.entries[0].instance = static_cast<std::uint32_t>(
            scene.instances.size());
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "an entry naming an instance outside the scene is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        std::swap(bad.entries[1], bad.entries[2]);
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "entries out of the visible instance order are rejected");
    }
    {
        // An instance reopened after another instance's run: the entries and
        // every count stay the same, so only the run shape catches it.
        nfsu::TrackDrawList bad = good;
        std::swap(bad.entries[1], bad.entries[3]);
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "an instance reopened after another one is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.entries[1] = bad.entries[0];
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "the same copy of the same group twice is rejected");
    }
    {
        // The same duplicate in the grouped order, where the run shape is
        // not checked and both copies are of the same group, so every
        // statistic still matches: only the rule that one copy of one group
        // is one entry is left.
        nfsu::TrackDrawListOptions grouped;
        grouped.order = nfsu::TrackDrawOrder::material_group;
        nfsu::TrackDrawList by_group;
        check(nfsu::build_track_draw_list(scene, cull, grouped, by_group, error),
              "the grouped reference list is built: " + error);
        check(nfsu::validate_track_draw_list(scene, by_group, error),
              "the untouched grouped list validates: " + error);
        nfsu::TrackDrawList bad = by_group;
        check(bad.entries.size() == 7 && bad.entries[0].draw == 0 &&
                  bad.entries[1].draw == 0,
              "the grouped list starts with two copies of group 0");
        bad.entries[1] = bad.entries[0];
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a repeated copy is rejected in the grouped order too");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.cost.texture_binds += 1;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a texture bind count that does not match the order is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.cost.visible_unplaced += 1;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a visible total that does not add up is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.order = nfsu::TrackDrawOrder::material_group;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a list relabelled as the other order is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.has_cull_provenance = false;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "provenance fields on a list that claims none are rejected");
    }
    {
        // The fourth provenance field, which the other three do not cover.
        nfsu::TrackDrawList bad = good;
        bad.has_cull_provenance = false;
        bad.policy_held = true;
        bad.sections_resident = 0;
        bad.sections_degraded = 0;
        bad.requested_policy = nfsu::TrackCullPolicy::draw_everything;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a policy on a list that carries no provenance is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.entries[0].draw =
            static_cast<std::uint32_t>(scene.region.draws.size());
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "an entry naming a group outside the region is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.cost.groups += 1;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a distinct-group count that does not match is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.cost.meshes += 1;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a distinct-mesh count that does not match is rejected");
    }
    {
        nfsu::TrackDrawList bad = good;
        bad.cost.triangles += 1;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "a triangle total that does not match is rejected");
    }
    {
        // instances_without_entries is a count, not a ceiling: the distinct
        // instances in the entries plus this number are exactly
        // visible_placed, so claiming one here while every placed instance
        // drew is a contradiction. Nothing else is touched, or the totals
        // would fail their own check instead of this one.
        nfsu::TrackDrawList bad = good;
        check(bad.cost.instances_without_entries == 0,
              "the reference list has every placed instance drawing");
        bad.cost.instances_without_entries = 1;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "an instances-without-entries count the entries contradict is"
              " rejected");
    }
    {
        // Nothing else bounds the visible totals from above.
        nfsu::TrackDrawList bad = good;
        bad.cost.visible += 700;
        bad.cost.visible_unplaced += 700;
        check(!nfsu::validate_track_draw_list(scene, bad, error),
              "more visible instances than the scene can hold is rejected");
    }
}

// The material group order is a stable reordering: the same multiset of
// entries, and inside one group the copies keep the visible order. Only a
// comparison that looks at both halves catches a sort that is merely
// ordered by group.
void test_material_group_order_is_stable() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    // A visible set that reaches instance 3 before instance 0, so the
    // visible order and the ascending one differ.
    std::vector<nfsu::TrackVisibleInstance> visible(2);
    visible[0].section_index = 0;
    visible[0].instance_index = 5;
    visible[1].section_index = 0;
    visible[1].instance_index = 0;

    nfsu::TrackDrawListOptions options;
    options.order = nfsu::TrackDrawOrder::material_group;
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, visible, options, list, error),
          "the grouped list is built: " + error);
    check_entries(list, {{3, 0}, {0, 0}, {3, 1}, {0, 1}},
                  "inside a group the copies keep the visible order");

    nfsu::TrackDrawList plain;
    check(nfsu::build_track_draw_list(scene, visible,
                                      nfsu::TrackDrawListOptions{}, plain, error),
          "the instance-order list is built: " + error);
    check_entries(plain, {{3, 0}, {3, 1}, {0, 0}, {0, 1}},
                  "the instance order keeps each instance's groups together");
    std::set<Pair> one;
    std::set<Pair> two;
    for (const nfsu::TrackDrawListEntry& entry : list.entries) {
        one.insert(Pair(entry.instance, entry.draw));
    }
    for (const nfsu::TrackDrawListEntry& entry : plain.entries) {
        two.insert(Pair(entry.instance, entry.draw));
    }
    check(one == two, "the two orders hold the same entries");
}

// A placed instance whose mesh has no drawable group contributes no entry,
// and the list counts it rather than dropping it silently. A region parsed
// from a file cannot produce such a mesh: src/track.cpp refuses an
// out-of-range texture slot outright and requires every group's index range
// to tile the index stream, so every group of every valid track mesh is
// drawable. The undrawable group below is made on the prepared region,
// which is the only way this layer will ever meet one.
void test_instance_without_a_drawable_group() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);
    check(scene.region.draws.size() == 4 && scene.region.draws[3].mesh == 2,
          "draw 3 is the LAMP group");
    scene.region.draws[3].drawable = false;

    std::vector<nfsu::TrackVisibleInstance> visible(2);
    visible[0].section_index = 0;
    visible[0].instance_index = 4;  // the LAMP instance
    visible[1].section_index = 0;
    visible[1].instance_index = 2;  // the unplaced SKYDOME instance
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, visible,
                                      nfsu::TrackDrawListOptions{}, list, error),
          "the list is built: " + error);
    check(list.entries.empty(), "neither visible instance draws anything");
    check_eq_u64(list.cost.visible, 2, "two visible instances");
    check_eq_u64(list.cost.visible_placed, 1, "one of them is placed");
    check_eq_u64(list.cost.visible_unplaced, 1, "the other places nothing");
    check_eq_u64(list.cost.instances_without_entries, 1,
                 "the placed one has no drawable group");
    check_eq_u64(list.cost.texture_binds, 0, "an empty list binds nothing");

    // A hand-built list that draws the undrawable group anyway. The entry
    // names the right mesh, the order holds and every statistic matches, so
    // the drawable flag is the only thing left to reject it.
    nfsu::TrackDrawList forged;
    forged.order = nfsu::TrackDrawOrder::visible_instance;
    const std::size_t lamp = nfsu::track_scene_instance_of(scene, 0, 4);
    check(lamp != nfsu::kNoHandle && scene.instances[lamp].mesh == 2,
          "the LAMP instance is placed on mesh 2");
    nfsu::TrackDrawListEntry entry;
    entry.instance = static_cast<std::uint32_t>(lamp);
    entry.draw = 3;
    forged.entries.push_back(entry);
    forged.cost.visible = 1;
    forged.cost.visible_placed = 1;
    forged.cost.entries = 1;
    forged.cost.triangles = 1;
    forged.cost.groups = 1;
    forged.cost.meshes = 1;
    forged.cost.texture_binds = 1;
    check(!nfsu::validate_track_draw_list(scene, forged, error),
          "an entry naming an undrawable group is rejected");
}

void test_empty_visible_set() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(
              scene, std::vector<nfsu::TrackVisibleInstance>{},
              nfsu::TrackDrawListOptions{}, list, error),
          "an empty visible set builds an empty list: " + error);
    check(list.entries.empty() && list.cost.entries == 0 &&
              list.cost.texture_binds == 0 && list.cost.visible == 0,
          "an empty list costs nothing");
}

// Both hashes speed.exe skips without a lookup, not just SKYDOME. A miss on
// either is a skipped hash, which is not the same as scenery this region
// cannot draw: unplaced_unresolved is the count that measures the hole a
// backend leaves, and a skipped hash does not belong in it.
void test_both_skipped_hashes() {
    SectionSpec section;
    section.number = 303;
    section.objects.resize(2);
    section.objects[0].mesh_hash[0] = nfsu::kSkydomeMeshHash;
    section.objects[1].mesh_hash[0] = nfsu::kReservedMeshHash;
    section.instances.push_back(row_instance(0, 0));
    section.instances.push_back(row_instance(1, 1));
    section.nodes.push_back(node_over(95, 205, {0, 1}));

    const Fixture fixture = make_fixture(town_meshes(), {section});
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the skipped-hash scene is built: " + error);
    check_eq_u64(scene.cost.placed_instances, 0, "neither instance is placed");
    check_eq_u64(scene.cost.unplaced_skipped_hash, 2,
                 "both are skipped hashes");
    check_eq_u64(scene.cost.unplaced_unresolved, 0,
                 "neither counts as scenery this region cannot draw");
    check(scene.unplaced_hashes.size() == 2 &&
              scene.unplaced_hashes[0] == nfsu::kReservedMeshHash &&
              scene.unplaced_hashes[1] == nfsu::kSkydomeMeshHash,
          "both hashes are listed, ascending");

    // The validator must keep the two reasons apart. Relabelling the
    // reserved hash as unresolved, with the counts moved to match, would
    // otherwise inflate the number a caller reads as a hole.
    nfsu::PreparedTrackScene bad = scene;
    bad.unplaced[0].reason = nfsu::TrackUnplacedReason::unresolved_hash;
    bad.cost.unplaced_skipped_hash = 1;
    bad.cost.unplaced_unresolved = 1;
    check(!nfsu::validate_prepared_track_scene(bad, error),
          "a hash the loader skips cannot be relabelled unresolved");
}

// An invalid section contributes nothing and does not renumber the ones
// around it. The middle section here holds an object with no mesh in slot 0,
// which src/track_objects.cpp refuses, so the two valid sections keep source
// positions 0 and 2.
void test_invalid_section_is_skipped() {
    std::vector<SectionSpec> sections = town_sections();
    SectionSpec broken;
    broken.number = 305;
    broken.objects.resize(1);  // every mesh slot zero: refused
    broken.instances.push_back(row_instance(0, 0));
    broken.nodes.push_back(node_over(95, 105, {0}));
    sections.insert(sections.begin() + 1, broken);

    const Fixture fixture = make_fixture(town_meshes(), sections);
    check_eq_u64(fixture.objects.sections.size(), 3, "three sections parse");
    check_eq_u64(fixture.objects.valid_section_count(), 2,
                 "one of them is invalid");

    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the scene with an invalid section is built: " + error);
    check_eq_u64(scene.cost.source_sections, 3, "all three are counted");
    check_eq_u64(scene.cost.valid_sections, 2, "two of them are valid");
    check_eq_u64(scene.cost.source_instances, 7,
                 "the invalid section's instance is not counted");
    check_eq_u64(scene.cost.placed_instances, 5, "the same five are placed");
    check(scene.sections.size() == 2 && scene.sections[0].section_index == 0 &&
              scene.sections[1].section_index == 2,
          "the valid sections keep their source positions");
    check(nfsu::track_scene_instance_of(scene, 2, 0) == 4,
          "a lookup by source position finds the second valid section");
    check(nfsu::track_scene_instance_of(scene, 1, 0) == nfsu::kNoHandle,
          "the invalid section answers no handle");
}

// The provenance a scene carries, read back. Four of these fields are
// documented and nothing else in this file looks at them.
void test_instance_provenance() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);
    if (scene.instances.size() != 5 || scene.unplaced.size() != 2 ||
        scene.sections.size() != 2) {
        check(false, "the town scene has the shape this test needs");
        return;
    }
    // row_instance writes 0x1000 + index into the record's flags word.
    check(scene.instances[0].instance_flags == 0x1000U &&
              scene.instances[3].instance_flags == 0x1005U &&
              scene.instances[4].instance_flags == 0x1006U,
          "each instance carries its own raw flags word");
    check(scene.instances[0].object_index == 0 &&
              scene.instances[1].object_index == 1 &&
              scene.instances[2].object_index == 4 &&
              scene.instances[3].object_index == 0,
          "each instance carries the object it names");
    check(scene.unplaced[0].object_index == 2 &&
              scene.unplaced[1].object_index == 3,
          "an unplaced instance carries its object too");
    check(scene.sections[0].number == 303 && scene.sections[0].letter == 'C' &&
              scene.sections[1].number == 410 &&
              scene.sections[1].letter == 'D' &&
              scene.sections[0].source_instances == 6 &&
              scene.sections[1].source_instances == 1,
          "each scene section carries its number, letter and instance count");
}

// A section whose bounding tree does not contain what it indexes degrades to
// a flat instance test, and the draw list carries that: policy_held false
// and one degraded section. Those two are among the three facts the header
// tells a caller to read.
void test_degraded_policy_reaches_the_draw_list() {
    Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);

    // The box is shrunk on the decoded report rather than written into the
    // fixture bytes, because src/track_objects.cpp refuses a tree whose node
    // does not contain what is below it: such a section is invalid at parse
    // time and never reaches the culler at all. The culler's own
    // verify_containment therefore only ever sees a report assembled some
    // other way, which is what this builds.
    check(!fixture.objects.sections.empty() &&
              fixture.objects.sections[0].nodes.size() == 2,
          "the town fixture's first section has the tree this test shrinks");
    fixture.objects.sections[0].nodes[0].bounds_max[0] = 205;

    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum,
                                nfsu::TrackCullOptions{});
    check(!cull.policy_held && cull.stats.sections_degraded == 1,
          "the culler degrades the section whose tree fails containment");

    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, cull, nfsu::TrackDrawListOptions{},
                                      list, error),
          "the draw list is built from the degraded report: " + error);
    check(list.has_cull_provenance && !list.policy_held &&
              list.sections_degraded == 1 && list.sections_resident == 2,
          "the draw list carries the degradation the culler reported");
}

// The grouped order is a stable reordering. Every fixture with a handful of
// entries is below the implementation's insertion-sort cutoff, where an
// unstable sort is stable by accident, so this one puts more than twenty
// copies of one group in a deliberately scrambled visible order.
void test_material_group_order_is_stable_at_scale() {
    MeshSpec mesh;
    mesh.name = "POST";
    mesh.hash = kMeshA;
    mesh.vertex_count = 3;
    mesh.face_count = 1;
    mesh.texture_hashes = {0xAAAA0001U};
    mesh.materials = {MaterialSpec{3, 0}};

    SectionSpec section;
    section.number = 303;
    section.objects.resize(1);
    section.objects[0].mesh_hash[0] = kMeshA;
    const int kCopies = 24;
    for (int i = 0; i < kCopies; ++i) {
        section.instances.push_back(row_instance(i, 0));
    }
    // A flat tree: five entries per node, so six nodes under a root of five
    // children plus its own entries. Simplest shape that covers 24 exactly:
    // the root names five children, each naming up to five instances.
    section.nodes.push_back(node_over(95, 2500, {-1, -2, -3, -4, -5}));
    for (int child = 0; child < 5; ++child) {
        std::vector<std::int16_t> entries;
        for (int k = 0; k < 5; ++k) {
            const int index = child * 5 + k;
            if (index < kCopies) {
                entries.push_back(static_cast<std::int16_t>(index));
            }
        }
        section.nodes.push_back(node_over(95, 2500, entries));
    }

    const Fixture fixture = make_fixture({mesh}, {section});
    check(fixture.objects.ok(), "the 24-copy fixture parses cleanly");
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the 24-copy scene is built: " + error);
    check_eq_u64(scene.instances.size(), kCopies, "every copy is placed");
    if (scene.instances.size() != static_cast<std::size_t>(kCopies)) {
        return;
    }

    // The visible set in reverse, which no ordering by instance or by group
    // reproduces by accident.
    std::vector<nfsu::TrackVisibleInstance> visible;
    for (int i = kCopies - 1; i >= 0; --i) {
        nfsu::TrackVisibleInstance item;
        item.section_index = 0;
        item.instance_index = static_cast<std::size_t>(i);
        visible.push_back(item);
    }
    nfsu::TrackDrawListOptions grouped;
    grouped.order = nfsu::TrackDrawOrder::material_group;
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, visible, grouped, list, error),
          "the grouped list of 24 copies is built: " + error);
    std::vector<Pair> want;
    for (int i = kCopies - 1; i >= 0; --i) {
        want.push_back(Pair(static_cast<std::uint32_t>(i), 0U));
    }
    check_entries(list, want,
                  "24 copies of one group keep the visible order exactly");
}

// The draw index an entry carries is the prepared position of the group,
// not its position in the per-mesh table. The two are the same number until
// a mesh has an undrawable group before a drawable one.
void test_entry_names_the_prepared_group() {
    const Fixture fixture = town();
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  scene, error),
          "the town scene is built: " + error);
    check(scene.region.draws.size() == 4 && scene.region.draws[0].mesh == 0 &&
              scene.region.draws[1].mesh == 0,
          "BARRIER_A owns prepared groups 0 and 1");
    // Mesh 0's first group stops being drawable, so its only entry is the
    // second one, at prepared position 1 and table position 0.
    scene.region.draws[0].drawable = false;

    std::vector<nfsu::TrackVisibleInstance> visible(1);
    visible[0].section_index = 0;
    visible[0].instance_index = 0;
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(scene, visible,
                                      nfsu::TrackDrawListOptions{}, list, error),
          "the list is built: " + error);
    check_entries(list, {{0, 1}},
                  "the entry names prepared group 1, not table position 0");
}

// Acceptance exactly at a limit, which the refusal cases cannot show. One
// section holding exactly max_instances, and a list drawing exactly
// max_triangles, are both legal.
void test_limits_accept_their_own_value() {
    const Fixture one_section = make_fixture(town_meshes(),
                                             {town_sections()[0]});
    check_eq_u64(one_section.objects.cost.instances, 6,
                 "the single-section fixture places six instances");
    nfsu::TrackSceneOptions options;
    options.placement.max_instances = 6;
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(one_section.geometry, one_section.objects,
                                  one_section.textures, options, scene, error),
          "one section of exactly six instances is accepted at six: " + error);
    options.placement.max_instances = 5;
    nfsu::PreparedTrackScene refused;
    check(!nfsu::build_track_scene(one_section.geometry, one_section.objects,
                                   one_section.textures, options, refused,
                                   error),
          "and refused at five");

    const Fixture fixture = town();
    nfsu::PreparedTrackScene full;
    check(nfsu::build_track_scene(fixture.geometry, fixture.objects,
                                  fixture.textures, nfsu::TrackSceneOptions{},
                                  full, error),
          "the town scene is built: " + error);
    nfsu::TrackFrustum frustum;
    check(nfsu::track_frustum_look_at(looking_down_the_row(1000.0F), frustum),
          "the reference camera builds a frustum");
    nfsu::TrackCullOptions cull_options;
    cull_options.policy = nfsu::TrackCullPolicy::draw_everything;
    const nfsu::TrackCullReport cull =
        nfsu::cull_track_region(fixture.objects, frustum, cull_options);
    nfsu::TrackDrawListOptions list_options;
    list_options.limits.max_triangles = 9;
    nfsu::TrackDrawList list;
    check(nfsu::build_track_draw_list(full, cull, list_options, list, error),
          "a list of exactly nine triangles is accepted at nine: " + error);
    check_eq_u64(list.cost.triangles, 9, "and it draws all nine");
}

void test_reason_names() {
    check(std::string(nfsu::track_unplaced_reason_name(
              nfsu::TrackUnplacedReason::empty_slot)) == "empty slot",
          "the empty slot reason names itself");
    check(std::string(nfsu::track_unplaced_reason_name(
              nfsu::TrackUnplacedReason::skipped_hash)) == "skipped hash",
          "the skipped hash reason names itself");
    check(std::string(nfsu::track_unplaced_reason_name(
              nfsu::TrackUnplacedReason::unresolved_hash)) == "unresolved hash",
          "the unresolved hash reason names itself");
    check(std::string(nfsu::track_draw_order_name(
              nfsu::TrackDrawOrder::visible_instance)) == "visible instance",
          "the visible instance order names itself");
}

} // namespace

int main() {
    test_fixture_parses();
    test_prepared_region_shape();
    test_placement_is_exact();
    test_placement_cost();
    test_unnamed_mesh_is_drawn_zero_times();
    test_instance_lookup();
    test_draw_everything_in_both_orders();
    test_frustum_selects_a_prefix();
    test_residency_narrows_the_list();
    test_lod_slot_zero();
    test_reflection_loader_uses_last_node();
    test_lod_slot_one_has_no_fallback();
    test_lod_slot_four_falls_back();
    test_lod_slot_five_falls_back_too();
    test_lod_chain_reaches_slot_three();
    test_placed_hashes_are_the_resolved_ones();
    test_lod_slot_must_exist();
    test_both_skipped_hashes();
    test_invalid_section_is_skipped();
    test_instance_provenance();
    test_degraded_policy_reaches_the_draw_list();
    test_material_group_order_is_stable_at_scale();
    test_entry_names_the_prepared_group();
    test_limits_accept_their_own_value();
    test_scene_outlives_its_inputs();
    test_build_is_deterministic();
    test_duplicate_hash_binds_the_lowest_mesh();
    test_draw_list_refuses_a_foreign_visible_set();
    test_explicit_visible_set_carries_no_provenance();
    test_draw_list_limits();
    test_placement_limits();
    test_validate_scene_rejects_tampering();
    test_validate_scene_rejects_the_wrong_duplicate();
    test_unresolved_group_binds_a_fallback();
    test_validate_draw_list_rejects_tampering();
    test_material_group_order_is_stable();
    test_instance_without_a_drawable_group();
    test_empty_visible_set();
    test_reason_names();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all track draw list tests passed\n";
    return 0;
}
