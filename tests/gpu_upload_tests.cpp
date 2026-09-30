#include "draw_command_checks.h"
#include "nfsu/track_render.h"
#include "nfsu/geometry.h"
#include "nfsu/gpu_upload.h"
#include "nfsu/memory_budget.h"
#include "nfsu/runtime_skin.h"
#include "nfsu/texture.h"
#include "nfsu/track.h"
#include "nfsu/track_objects.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

// Every test below names the defect it exists to catch, and every assertion
// is an exact value. The scenes are not hand-assembled: the car scene is
// prepared by prepare_car_scene, with its generated skin bound by
// bind_runtime_skin exactly as the viewer binds one, and the track scene is
// parsed out of byte images by the real parsers and placed by
// build_track_scene. A table that is right for a structure no preparation
// step can produce would prove nothing.

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void check_eq(std::uint64_t got, std::uint64_t want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

void check_has(const std::string& text, const std::string& part, const std::string& what) {
    check(text.find(part) != std::string::npos,
          what + ": '" + text + "' does not contain '" + part + "'");
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

void store_u32(unsigned char* out, std::uint32_t value) {
    out[0] = static_cast<unsigned char>(value);
    out[1] = static_cast<unsigned char>(value >> 8U);
    out[2] = static_cast<unsigned char>(value >> 16U);
    out[3] = static_cast<unsigned char>(value >> 24U);
}

void store_float(unsigned char* out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    store_u32(out, bits);
}

nfsu::RenderTexture make_texture(std::uint32_t hash, std::uint32_t width,
                                 std::uint32_t height, unsigned char fill) {
    nfsu::RenderTexture texture;
    texture.hash = hash;
    texture.claims[0] = hash;
    texture.claim_count = 1;
    texture.width = width;
    texture.height = height;
    texture.rgba.assign(static_cast<std::size_t>(width) * height * 4U, fill);
    texture.valid = true;
    return texture;
}

nfsu::MemoryArena make_arena(std::uint64_t capacity, std::uint64_t max_alignment,
                             std::uint32_t max_allocations = 256) {
    nfsu::ArenaConfig config;
    config.category = nfsu::MemoryCategory::gpu;
    config.capacity = capacity;
    config.max_alignment = max_alignment;
    config.max_allocations = max_allocations;
    nfsu::MemoryArena arena;
    std::string error;
    const bool gpu_ok_1 = arena.create(config, error);
    check(gpu_ok_1, "arena create: " + error);
    return arena;
}

bool same_contents(const nfsu::ArenaStats& a, const nfsu::ArenaStats& b) {
    return a.used_bytes == b.used_bytes && a.free_blocks == b.free_blocks &&
           a.largest_free_block == b.largest_free_block && a.extent == b.extent &&
           a.live_allocations == b.live_allocations;
}

// ---------------------------------------------------------------------------
// The car fixture
//
//   mesh 0  BODY   4 vertices, 2 triangles; group 0 on the skin hash, which
//                  the generated skin answers to, group 1 on PAINT
//   mesh 1  WHEEL  3 vertices, 1 triangle; one group on RIM
//
// Three textures of three different sizes, so that no byte count can be
// right by accident: the 8x4 generated skin, the 4x4 PAINT and the 2x3 RIM.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kSkinHash = 0x5C1A0001U;
constexpr std::uint32_t kPaintHash = 0x5C1A0002U;
constexpr std::uint32_t kRimHash = 0x5C1A0003U;
// Past the one real texture source, as bind_runtime_skin asks.
constexpr std::size_t kSkinSource = 7;

nfsu::MeshVertex car_vertex(float px, float py, float pz) {
    nfsu::MeshVertex out;
    out.px = px;
    out.py = py;
    out.pz = pz;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
    return out;
}

nfsu::MeshMaterial car_material(std::uint32_t first, std::uint32_t count,
                                std::uint32_t slot, unsigned char tag) {
    nfsu::MeshMaterial out;
    out.first_index = first;
    out.index_count = count;
    out.texture_slot = slot;
    out.material_key_slot = 0x100U + tag;
    out.flags = 0x5000U + tag;
    out.offset = 0x1000U + tag;
    for (int k = 0; k < 3; ++k) {
        out.bounds_min[k] = -1.0F - static_cast<float>(k);
        out.bounds_max[k] = 1.0F + static_cast<float>(k);
    }
    for (std::size_t k = 0; k < nfsu::kMaterialRecordSize; ++k) {
        out.raw[k] = static_cast<unsigned char>(tag * 7U + k);
    }
    for (std::size_t k = 0; k < 3; ++k) {
        store_float(out.raw + k * 4U, out.bounds_min[k]);
        store_float(out.raw + 16U + k * 4U, out.bounds_max[k]);
    }
    store_u32(out.raw + 12, out.index_count);
    store_u32(out.raw + 28, out.texture_slot);
    store_u32(out.raw + 32, out.material_key_slot);
    store_u32(out.raw + 52, out.first_index);
    store_u32(out.raw + 56, out.flags);
    return out;
}

nfsu::GeometryReport car_geometry() {
    nfsu::GeometryReport report;
    nfsu::Mesh body;
    body.index = 0;
    body.name = "BODY";
    body.valid = true;
    body.vertices = {car_vertex(0, 0, 0), car_vertex(0, 1, 0), car_vertex(0, 1, 1),
                     car_vertex(0, 0, 1)};
    body.triangles = {{0, 1, 2}, {0, 2, 3}};
    body.texture_hashes = {kSkinHash, kPaintHash};
    body.materials = {car_material(0, 3, 0, 1), car_material(3, 3, 1, 2)};
    report.meshes.push_back(body);

    nfsu::Mesh wheel;
    wheel.index = 1;
    wheel.name = "WHEEL";
    wheel.valid = true;
    wheel.vertices = {car_vertex(1, 0, 0), car_vertex(1, 1, 0), car_vertex(1, 1, 1)};
    wheel.triangles = {{0, 1, 2}};
    wheel.texture_hashes = {kRimHash};
    wheel.materials = {car_material(0, 3, 0, 3)};
    report.meshes.push_back(wheel);

    for (std::size_t i = 0; i < report.meshes.size(); ++i) {
        nfsu::Mesh& mesh = report.meshes[i];
        mesh.directory_hash = 0x01020304U + static_cast<std::uint32_t>(i);
        mesh.directory_offset = 0x200U + i * 0x100U;
        mesh.directory_size = 0x80U;
        mesh.has_directory_entry = true;
    }
    report.mesh_directory_valid = true;
    return report;
}

struct CarFixture {
    nfsu::GeometryReport geometry;
    nfsu::RenderTextureSet textures;
    nfsu::RuntimeSkinBinding binding;
    nfsu::PreparedCarScene scene;
};

// with_skin false leaves the skin hash unresolved, which is what a scene
// without a generated skin looks like.
CarFixture car_fixture(bool with_skin) {
    CarFixture fixture;
    fixture.geometry = car_geometry();
    fixture.textures.textures.push_back(make_texture(kPaintHash, 4, 4, 0x40));
    fixture.textures.textures.push_back(make_texture(kRimHash, 2, 3, 0x50));
    for (const nfsu::RenderTexture& texture : fixture.textures.textures) {
        fixture.textures.decoded_bytes += texture.rgba.size();
    }
    std::string error;
    if (with_skin) {
        nfsu::RuntimeSkin skin;
        skin.target_hash = kSkinHash;
        skin.width = 8;
        skin.height = 4;
        skin.rgba.assign(8U * 4U * 4U, 0x60);
        const bool gpu_ok_2 = nfsu::bind_runtime_skin(skin, {kSkinHash}, kSkinSource, fixture.textures,
                                      fixture.binding, error);
        check(gpu_ok_2, "bind the generated skin: " + error);
    }
    const bool gpu_ok_3 = nfsu::prepare_car_scene(fixture.geometry, fixture.textures, nfsu::MeshSelection{},
                                  nfsu::SceneLimits{}, fixture.scene, error);
    check(gpu_ok_3, "prepare the car scene: " + error);
    return fixture;
}

// ---------------------------------------------------------------------------
// The track fixture: the town of tests/track_draw_list_tests.cpp, reduced.
//
//   mesh 0  BARRIER_A  6 vertices, 3 triangles, groups on textures 1 and 2
//   mesh 1  BARRIER_B  3 vertices, 1 triangle, on texture 2
//   mesh 2  LAMP       3 vertices, 1 triangle, on texture 2
//
// Texture 1 is 4x2 and texture 2 is 3x5. One section places mesh 0, mesh 1,
// a hash no mesh has, mesh 2 and mesh 0 again.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kMeshA = 0x1001U;
constexpr std::uint32_t kMeshB = 0x1002U;
constexpr std::uint32_t kMeshLamp = 0x1003U;
constexpr std::uint32_t kTrackTexture1 = 0xAAAA0001U;
constexpr std::uint32_t kTrackTexture2 = 0xAAAA0002U;
constexpr std::uint64_t kGeometryBase = 0;
constexpr std::uint64_t kSceneryBase = 0x2000;

// Writes a chunk tree at absolute file offsets, so every alignment rule of
// docs/TRACK.md and docs/TRACK_OBJECTS.md applies as it would in a file.
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
        put_u32(bytes_, header + 4, static_cast<std::uint32_t>(bytes_.size() - header - 8));
    }

    void append(const Bytes& payload) { bytes_.insert(bytes_.end(), payload.begin(), payload.end()); }

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

struct MaterialSpec {
    std::uint32_t index_count = 3;
    std::uint32_t texture_slot = 0;
};

struct MeshSpec {
    std::string name;
    std::uint32_t hash = 0;
    std::uint32_t vertex_count = 3;
    std::uint32_t face_count = 1;
    std::vector<MaterialSpec> materials{MaterialSpec{}};
    std::vector<std::uint32_t> texture_hashes;
};

void write_mesh(Writer& writer, const MeshSpec& spec) {
    writer.begin(nfsu::kMeshContainerId);

    writer.begin(nfsu::kMeshMetadataId);
    writer.pad_to(nfsu::kMeshMetadataAlignment);
    Bytes record(nfsu::kMeshMetadataRecordSize, 0);
    put_u32(record, nfsu::kMeshMetadataHashOffset, spec.hash);
    record[nfsu::kMeshMetadataTextureCountOffset] =
        static_cast<unsigned char>(spec.texture_hashes.size());
    for (std::size_t i = 0; i < spec.name.size() && i < nfsu::kMeshNameSize; ++i) {
        record[nfsu::kMeshMetadataNameOffset + i] = static_cast<unsigned char>(spec.name[i]);
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
        const std::uint32_t count = spec.vertex_count;
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

Bytes build_geometry(const std::vector<MeshSpec>& meshes) {
    Writer writer(kGeometryBase);
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

// One section: five objects naming mesh 0, mesh 1, a hash no mesh has,
// mesh 2 and mesh 0, one instance of each on the world +X axis, and a
// two-node tree over them.
Bytes build_section() {
    Writer writer(kSceneryBase);
    writer.begin(nfsu::kScenerySectionId);

    writer.begin(nfsu::kSceneryHeaderId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes header(nfsu::kSceneryHeaderRecordSize, 0U);
    put_u32(header, nfsu::kScenerySectionNumberOffset, 303);
    writer.append(header);
    writer.end();

    const std::uint32_t object_meshes[5] = {kMeshA, kMeshB, 0x00009999U, kMeshLamp, kMeshA};
    writer.begin(nfsu::kSceneryInstanceListId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes instances;
    for (int i = 0; i < 5; ++i) {
        const std::int16_t centre = static_cast<std::int16_t>(100 * (i + 1));
        const std::int16_t low[3] = {static_cast<std::int16_t>(centre - 5), -5, -5};
        const std::int16_t high[3] = {static_cast<std::int16_t>(centre + 5), 5, 5};
        for (const std::int16_t value : low) {
            append_i16(instances, value);
        }
        for (const std::int16_t value : high) {
            append_i16(instances, value);
        }
        append_u16(instances, static_cast<std::uint16_t>(i));
        append_u16(instances, static_cast<std::uint16_t>(0x1000 + i));
        append_f32(instances, static_cast<float>(centre));
        append_f32(instances, 0.0F);
        append_f32(instances, 0.0F);
        const std::int16_t one = static_cast<std::int16_t>(nfsu::kSceneryMatrixOne);
        const std::int16_t identity[9] = {one, 0, 0, 0, one, 0, 0, 0, one};
        for (const std::int16_t word : identity) {
            append_i16(instances, word);
        }
        append_i16(instances, 0);
    }
    writer.append(instances);
    writer.end();

    Bytes objects;
    for (const std::uint32_t mesh : object_meshes) {
        const std::size_t start = objects.size();
        append_u32(objects, mesh);
        for (std::size_t slot = 1; slot < nfsu::kSceneryObjectMeshSlots; ++slot) {
            append_u32(objects, 0U);
        }
        objects.resize(start + nfsu::kSceneryObjectRadiusOffset, 0U);
        append_f32(objects, 5.0F);
        objects.resize(start + nfsu::kSceneryObjectRecordSize, 0U);
    }
    writer.leaf(nfsu::kSceneryObjectListId, objects);

    Bytes tree;
    const struct {
        std::int16_t low;
        std::int16_t high;
        std::vector<std::int16_t> entries;
    } nodes[2] = {{95, 505, {0, 1, 2, -1}}, {395, 505, {3, 4}}};
    for (const auto& node : nodes) {
        append_i16(tree, node.low);
        append_i16(tree, -5);
        append_i16(tree, -5);
        append_i16(tree, node.high);
        append_i16(tree, 5);
        append_i16(tree, 5);
        append_u16(tree, static_cast<std::uint16_t>(node.entries.size()));
        for (std::size_t slot = 0; slot < nfsu::kSceneryBoundNodeEntries; ++slot) {
            append_i16(tree, slot < node.entries.size() ? node.entries[slot] : 0);
        }
    }
    writer.leaf(nfsu::kSceneryBoundTreeId, tree);

    writer.end();
    return writer.bytes();
}

struct TrackFixture {
    Bytes geometry_bytes;
    Bytes scenery_bytes;
    nfsu::TrackRegionReport geometry;
    nfsu::TrackObjectReport objects;
    nfsu::RenderTextureSet textures;
    nfsu::PreparedTrackScene scene;
};

TrackFixture track_fixture() {
    MeshSpec a;
    a.name = "BARRIER_A";
    a.hash = kMeshA;
    a.vertex_count = 6;
    a.face_count = 3;
    a.texture_hashes = {kTrackTexture1, kTrackTexture2};
    a.materials = {MaterialSpec{3, 0}, MaterialSpec{6, 1}};
    MeshSpec b;
    b.name = "BARRIER_B";
    b.hash = kMeshB;
    b.texture_hashes = {kTrackTexture2};
    MeshSpec lamp;
    lamp.name = "LAMP";
    lamp.hash = kMeshLamp;
    lamp.texture_hashes = {kTrackTexture2};

    TrackFixture fixture;
    fixture.geometry_bytes = build_geometry({a, b, lamp});
    fixture.scenery_bytes = build_section();
    fixture.geometry = nfsu::parse_track_region(
        fixture.geometry_bytes.data(), fixture.geometry_bytes.size(), kGeometryBase);
    fixture.objects = nfsu::parse_track_objects(
        fixture.scenery_bytes.data(), fixture.scenery_bytes.size(), kSceneryBase);
    fixture.textures.textures.push_back(make_texture(kTrackTexture1, 4, 2, 0x11));
    fixture.textures.textures.push_back(make_texture(kTrackTexture2, 3, 5, 0x22));
    check(fixture.geometry.ok(), "the track geometry fixture parses");
    check(fixture.objects.ok(), "the track scenery fixture parses");
    std::string error;
    const bool gpu_ok_4 = nfsu::build_track_scene(fixture.geometry, fixture.objects, fixture.textures,
                                  nfsu::TrackSceneOptions{}, fixture.scene, error);
    check(gpu_ok_4, "build the track scene: " + error);
    return fixture;
}

struct Expected {
    nfsu::UploadResourceKind kind;
    nfsu::UploadLifetime lifetime;
    nfsu::UploadMemory memory;
    const char* name;
    std::size_t source;
    std::uint64_t bytes;
    std::uint64_t alignment;
    std::uint64_t element_bytes;
    std::uint64_t elements;
    std::uint32_t hash;
};

void check_table(const nfsu::UploadTable& table, const std::vector<Expected>& want,
                 const std::string& what) {
    check_eq(table.resources.size(), want.size(), what + ": resource count");
    for (std::size_t i = 0; i < want.size() && i < table.resources.size(); ++i) {
        const nfsu::UploadResource& got = table.resources[i];
        const Expected& w = want[i];
        const std::string at = what + " resource " + std::to_string(i);
        check(got.kind == w.kind, at + ": kind " + nfsu::upload_resource_kind_name(got.kind));
        check(got.lifetime == w.lifetime,
              at + ": lifetime " + nfsu::upload_lifetime_name(got.lifetime));
        check(got.memory == w.memory, at + ": memory " + nfsu::upload_memory_name(got.memory));
        check(got.name == w.name, at + ": name '" + got.name + "', want '" + w.name + "'");
        check_eq(got.source, w.source, at + ": source");
        check_eq(got.bytes, w.bytes, at + ": bytes");
        check_eq(got.alignment, w.alignment, at + ": alignment");
        check_eq(got.element_bytes, w.element_bytes, at + ": element bytes");
        check_eq(got.elements, w.elements, at + ": elements");
        check_eq(got.texture_hash, w.hash, at + ": texture hash");
        check_eq(got.bytes, got.element_bytes * got.elements, at + ": bytes are elements");
    }
}

// ---------------------------------------------------------------------------
// The format gate
// ---------------------------------------------------------------------------

const nfsu::TextureUploadFormat kPackFormats[] = {
    nfsu::TextureUploadFormat::dxt1, nfsu::TextureUploadFormat::dxt3,
    nfsu::TextureUploadFormat::a8r8g8b8, nfsu::TextureUploadFormat::p8,
    nfsu::TextureUploadFormat::unsupported};

// Defect: the wrong row pitch or byte count for the one accepted format,
// which on hardware copies the wrong number of bytes per row and skews or
// overruns the image.
void test_gate_accepts_rgba8() {
    const struct {
        std::uint32_t width;
        std::uint32_t height;
    } sizes[] = {{1, 1}, {3, 5}, {512, 256}, {4096, 1}};
    for (const auto& size : sizes) {
        const std::string what =
            "RGBA8 " + std::to_string(size.width) + "x" + std::to_string(size.height);
        nfsu::TextureUploadShape shape;
        std::string error;
        const std::uint64_t bytes = std::uint64_t{size.width} * size.height * 4U;
        const bool gpu_ok_5 = nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::rgba8, size.width,
                                                size.height, bytes, shape, error);
        check(gpu_ok_5, what + " is accepted: " + error);
        check(shape.format == nfsu::TextureUploadFormat::rgba8, what + ": format");
        check_eq(shape.width, size.width, what + ": width");
        check_eq(shape.height, size.height, what + ": height");
        check_eq(shape.texel_bytes, 4, what + ": texel bytes");
        check_eq(shape.row_bytes, std::uint64_t{size.width} * 4U, what + ": row bytes");
        check_eq(shape.bytes, bytes, what + ": bytes");
    }
    // The largest texture one memory block holds, and one texel more.
    nfsu::TextureUploadShape shape;
    std::string error;
    const std::uint32_t widest = static_cast<std::uint32_t>(nfsu::kMaxUploadResourceBytes / 4U);
    const bool gpu_ok_6 = nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::rgba8, widest, 1,
                                            nfsu::kMaxUploadResourceBytes, shape, error);
    check(gpu_ok_6, "the largest RGBA8 texture a block holds: " + error);
    check(!nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::rgba8, widest + 1U,
                                             1, nfsu::kMaxUploadResourceBytes + 4U, shape,
                                             error),
          "one texel past the largest block is refused");
    check_has(error, "one deko3d memory block can hold", "the block refusal says why");
    check_eq(shape.bytes, 0, "a refusal leaves the shape zeroed");
}

// Defect: a gate that decides by byte count instead of by format. A8R8G8B8
// has four bytes per texel exactly like RGBA8, so a size check alone
// accepts it and the backend uploads B, G, R, A as R, G, B, A: red and blue
// swapped on every such texture. Each pack format is offered with a byte
// count that would be right for RGBA8, and each must be refused by name.
void test_gate_refuses_invalid_native_payloads() {
    for (const nfsu::TextureUploadFormat format : kPackFormats) {
        const std::string name = nfsu::texture_upload_format_name(format);
        nfsu::TextureUploadShape shape;
        shape.bytes = 99;
        std::string error;
        check(!nfsu::check_texture_upload_format(format, 4, 4, 64, shape, error),
              name + " is refused");
        check_has(error, name, name);
        check_eq(shape.bytes, 0, name + ": the refusal zeroes the shape");
        check_eq(shape.width, 0, name + ": the refusal zeroes the width");
    }
    // Each says why, and the reason is the decode path's, not a size rule.
    std::string error;
    nfsu::TextureUploadShape shape;
    nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::p8, 4, 4, 16, shape, error);
    check_has(error, "256-entry palette", "the P8 refusal names the missing palette");
    nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::dxt1, 4, 4, 8, shape, error);
    check_eq(shape.bytes, 8, "native DXT1 top level is accepted");
    nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::unsupported, 4, 4, 64, shape,
                                      error);
    check_has(error, "pack parser rejects", "the unsupported refusal names the parser");
    // A value outside the enumeration is refused, not treated as RGBA8.
    nfsu::check_texture_upload_format(static_cast<nfsu::TextureUploadFormat>(77), 4, 4, 64,
                                      shape, error);
    check_has(error, "texture format unsupported is not uploadable", "an unknown format");
}

// Defect: an RGBA8 texture that is not what it says. Each byte count below
// belongs to some other shape - one byte short, one byte long, P8-sized,
// DXT1-sized - and the zero dimensions and the product that wraps 64 bits
// are the arithmetic a naive gate gets wrong: (2^32 - 1)^2 * 4 wraps to
// 2^64 - 2^35 + 4, and a gate that multiplies without checking accepts
// exactly that byte count. The largest product that does fit, 2^64 - 4, is
// the other side of that check, and must be refused for its size rather than
// reported as an overflow.
void test_gate_refuses_malformed_rgba8() {
    const std::uint64_t wrapped = 0ULL - (1ULL << 35U) + 4U;
    const struct {
        const char* what;
        std::uint32_t width;
        std::uint32_t height;
        std::uint64_t bytes;
        const char* message;
    } cases[] = {
        {"zero width", 0, 4, 0, "has a zero dimension"},
        {"zero height", 4, 0, 0, "has a zero dimension"},
        {"one byte short", 4, 4, 63, "needs exactly 64 bytes, got 63"},
        {"one byte long", 4, 4, 65, "needs exactly 64 bytes, got 65"},
        {"P8-sized", 4, 4, 16, "needs exactly 64 bytes, got 16"},
        {"DXT1-sized", 4, 4, 8, "needs exactly 64 bytes, got 8"},
        {"a product that wraps", 0xFFFFFFFFU, 0xFFFFFFFFU, wrapped, "larger than 2^64 - 1"},
        // (2^31 - 1)(2^31 + 1) * 4 is 2^64 - 4, the largest product that
        // fits: refused for its byte count, not as an overflow.
        {"the largest product that fits", 2147483647U, 2147483649U, 0,
         "needs exactly 18446744073709551612 bytes, got 0"},
    };
    for (const auto& c : cases) {
        nfsu::TextureUploadShape shape;
        std::string error;
        check(!nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::rgba8, c.width,
                                                 c.height, c.bytes, shape, error),
              std::string(c.what) + " is refused");
        check_has(error, c.message, c.what);
        check_eq(shape.bytes, 0, std::string(c.what) + ": shape zeroed");
    }
}

// Defect: the gate's names drifting from the pack parser's. Every report in
// this project names a pack format by pixel_format_name; a refusal that
// spelled one differently would name a format no other output mentions.
void test_gate_names_are_the_parser_names() {
    const struct {
        nfsu::TextureUploadFormat upload;
        nfsu::PixelFormat pack;
    } pairs[] = {{nfsu::TextureUploadFormat::dxt1, nfsu::PixelFormat::dxt1},
                 {nfsu::TextureUploadFormat::dxt3, nfsu::PixelFormat::dxt3},
                 {nfsu::TextureUploadFormat::a8r8g8b8, nfsu::PixelFormat::a8r8g8b8},
                 {nfsu::TextureUploadFormat::p8, nfsu::PixelFormat::p8},
                 {nfsu::TextureUploadFormat::unsupported, nfsu::PixelFormat::unsupported}};
    for (const auto& pair : pairs) {
        check(std::string(nfsu::texture_upload_format_name(pair.upload)) ==
                  nfsu::pixel_format_name(pair.pack),
              std::string("upload name ") + nfsu::texture_upload_format_name(pair.upload) +
                  " is the pack name " + nfsu::pixel_format_name(pair.pack));
    }
    check(std::string(nfsu::texture_upload_format_name(nfsu::TextureUploadFormat::rgba8)) ==
              "RGBA8",
          "the decoded format is RGBA8");
    check(std::string(nfsu::upload_resource_kind_name(nfsu::UploadResourceKind::vertex_buffer)) ==
                  "vertex buffer" &&
              std::string(nfsu::upload_resource_kind_name(
                  nfsu::UploadResourceKind::index_buffer)) == "index buffer" &&
              std::string(nfsu::upload_resource_kind_name(nfsu::UploadResourceKind::texture)) ==
                  "texture",
          "resource kind names");
    check(std::string(nfsu::upload_lifetime_name(nfsu::UploadLifetime::scene)) == "scene" &&
              std::string(nfsu::upload_lifetime_name(nfsu::UploadLifetime::generated)) ==
                  "generated",
          "lifetime names");
    check(std::string(nfsu::upload_memory_name(nfsu::UploadMemory::buffer)) == "buffer" &&
              std::string(nfsu::upload_memory_name(nfsu::UploadMemory::image)) == "image",
          "memory names");
}

// Defect: a gate that is not the decode path's. The list is supposed to be
// derived from decode_level, so this runs decode_level itself on a
// well-formed level of every pack format, 5x3 so that the DXT edge blocks
// are cropped: what it decodes must be exactly an RGBA8 image the gate
// accepts, and every stored payload, decodable or not, must be refused. If
// decode_level ever learns P8, or produces a size other than width * height
// * 4, this is where the gate has to follow.
void test_gate_follows_decode_level() {
    const struct {
        nfsu::PixelFormat pack;
        nfsu::TextureUploadFormat upload;
        bool decodes;
    } formats[] = {{nfsu::PixelFormat::dxt1, nfsu::TextureUploadFormat::dxt1, true},
                   {nfsu::PixelFormat::dxt3, nfsu::TextureUploadFormat::dxt3, true},
                   {nfsu::PixelFormat::a8r8g8b8, nfsu::TextureUploadFormat::a8r8g8b8, true},
                   {nfsu::PixelFormat::p8, nfsu::TextureUploadFormat::p8, false},
                   {nfsu::PixelFormat::unsupported, nfsu::TextureUploadFormat::unsupported,
                    false}};
    const std::uint32_t width = 5;
    const std::uint32_t height = 3;
    for (const auto& format : formats) {
        const std::string name = nfsu::pixel_format_name(format.pack);
        std::uint64_t size = 16;
        nfsu::level_size(format.pack, width, height, size);
        const Bytes level(static_cast<std::size_t>(size), 0x5AU);
        std::vector<unsigned char> rgba;
        std::string error;
        const bool decoded = nfsu::decode_level(format.pack, width, height, level.data(),
                                                level.size(), rgba, error);
        check(decoded == format.decodes, name + ": decode_level decodes it: " + error);
        nfsu::TextureUploadShape shape;
        std::string refusal;
        if (decoded) {
            check(nfsu::check_texture_upload_format(nfsu::TextureUploadFormat::rgba8, width,
                                                    height, rgba.size(), shape, refusal),
                  name + ": the gate accepts what decode_level produced: " + refusal);
        }
        const bool native = format.upload == nfsu::TextureUploadFormat::dxt1 ||
                            format.upload == nfsu::TextureUploadFormat::dxt3;
        check(nfsu::check_texture_upload_format(format.upload, width, height, level.size(),
                                                 shape, refusal) == native,
              name + ": only DXT native payloads are uploadable");
    }
}

// ---------------------------------------------------------------------------
// The tables
// ---------------------------------------------------------------------------

// Defect: a table that uploads the wrong bytes: the file's vertex stride
// where the struct's belongs, triangles counted as indices, a texture's
// texels counted without their four bytes, a mesh left out, a data pointer
// into the wrong vector, or the generated skin given a pack texture's
// lifetime - which would release the skin only with the whole car and make
// the garage re-upload every buffer to change a vinyl.
void test_car_table_is_exact() {
    CarFixture fixture = car_fixture(true);
    const nfsu::PreparedCarScene& scene = fixture.scene;
    check_eq(scene.meshes.size(), 2, "the car scene holds two meshes");
    check_eq(scene.textures.size(), 3, "the car scene holds three textures");
    check_eq(fixture.binding.source, kSkinSource, "the binding reports its source");
    if (scene.meshes.size() != 2 || scene.textures.size() != 3) {
        return;
    }
    // First use: the skin on BODY's group 0, PAINT on its group 1, RIM.
    check(scene.textures[0].hash == kSkinHash && scene.textures[0].source == kSkinSource,
          "texture 0 is the generated skin");
    check(scene.textures[1].hash == kPaintHash, "texture 1 is PAINT");
    check(scene.textures[2].hash == kRimHash, "texture 2 is RIM");

    nfsu::UploadTable table;
    std::string error;
    const bool gpu_ok_7 = nfsu::build_car_upload_table(scene, fixture.binding.source, table, error);
    check(gpu_ok_7, "car table: " + error);
    using Kind = nfsu::UploadResourceKind;
    using Life = nfsu::UploadLifetime;
    using Memory = nfsu::UploadMemory;
    check_table(table,
                {{Kind::vertex_buffer, Life::scene, Memory::buffer, "car mesh 0 BODY vertex buffer",
                  0, 144, 4, 36, 4, 0},
                 {Kind::index_buffer, Life::scene, Memory::buffer, "car mesh 0 BODY index buffer",
                  0, 12, 2, 2, 6, 0},
                 {Kind::vertex_buffer, Life::scene, Memory::buffer,
                  "car mesh 1 WHEEL vertex buffer", 1, 108, 4, 36, 3, 0},
                 {Kind::index_buffer, Life::scene, Memory::buffer, "car mesh 1 WHEEL index buffer",
                  1, 6, 2, 2, 3, 0},
                 {Kind::texture, Life::generated, Memory::image,
                  "car generated texture 0 hash 0x5C1A0001", 0, 128, 32, 4, 32, kSkinHash},
                 {Kind::texture, Life::scene, Memory::image, "car texture 1 hash 0x5C1A0002", 1,
                  64, 32, 4, 16, kPaintHash},
                 {Kind::texture, Life::scene, Memory::image, "car texture 2 hash 0x5C1A0003", 2,
                  24, 32, 4, 6, kRimHash}},
                "car table");
    if (table.resources.size() == 7) {
        check(table.resources[0].data ==
                  reinterpret_cast<const unsigned char*>(scene.meshes[0].vertices.data()),
              "the vertex buffer points at the scene's vertices");
        check(table.resources[1].data ==
                  reinterpret_cast<const unsigned char*>(scene.meshes[0].triangles.data()),
              "the index buffer points at the scene's triangles");
        check(table.resources[5].data == scene.textures[1].rgba.data(),
              "the texture points at the scene's texels");
        check(table.resources[4].texture.width == 8 && table.resources[4].texture.height == 4 &&
                  table.resources[4].texture.row_bytes == 32,
              "the skin's shape is the gate's");
    }
    const nfsu::UploadTotals& totals = table.totals;
    check_eq(totals.resources, 7, "totals: resources");
    check_eq(totals.vertex_buffers, 2, "totals: vertex buffers");
    check_eq(totals.index_buffers, 2, "totals: index buffers");
    check_eq(totals.textures, 3, "totals: textures");
    check_eq(totals.generated_textures, 1, "totals: generated textures");
    check_eq(totals.vertex_bytes, 252, "totals: vertex bytes");
    check_eq(totals.index_bytes, 18, "totals: index bytes");
    check_eq(totals.texture_bytes, 216, "totals: texture bytes");
    check_eq(totals.generated_texture_bytes, 128, "totals: generated texture bytes");
    check_eq(totals.largest_texture_bytes, 128, "totals: largest texture");
    check_eq(totals.empty_buffers, 0, "totals: empty buffers");
    // The same scene's textures, counted by the texture set it came from.
    check_eq(totals.texture_bytes, fixture.textures.decoded_bytes,
             "the table's texture bytes are the set's decoded bytes");
}

// Defect: the generated lifetime decided by anything but the provenance
// source bind_runtime_skin stamps. By hash, a pack texture that answers to
// the skin hash would be treated as regenerable; by position, whichever
// texture happens to come first. Here the same scene is read with the
// binding's source, with no source, with a source nothing carries, and with
// source 0, the real pack source: naming it marks both of its textures,
// because the rule is the caller's statement of provenance and nothing else,
// which is why bind_runtime_skin asks for an index past the real sources.
void test_generated_lifetime_follows_the_binding() {
    CarFixture fixture = car_fixture(true);
    nfsu::UploadTable table;
    std::string error;
    const struct {
        std::size_t source;
        std::uint64_t generated;
    } cases[] = {{kSkinSource, 1}, {nfsu::kNoHandle, 0}, {kSkinSource + 1U, 0}, {0, 2}};
    for (const auto& c : cases) {
        const bool gpu_ok_8 = nfsu::build_car_upload_table(fixture.scene, c.source, table, error);
        check(gpu_ok_8, "car table: " + error);
        check_eq(table.totals.generated_textures, c.generated,
                 "generated textures with source " + std::to_string(c.source));
        std::uint64_t named = 0;
        for (const nfsu::UploadResource& resource : table.resources) {
            if (resource.name.find("generated") != std::string::npos) {
                ++named;
                check(resource.lifetime == nfsu::UploadLifetime::generated,
                      resource.name + " is named generated and is");
            }
        }
        check_eq(named, c.generated, "generated names with source " + std::to_string(c.source));
    }
    // Without a skin, the skin hash resolves to nothing and no texture is
    // generated whatever source is named.
    CarFixture plain = car_fixture(false);
    const bool gpu_ok_9 = nfsu::build_car_upload_table(plain.scene, kSkinSource, table, error);
    check(gpu_ok_9, "plain car table: " + error);
    check_eq(table.totals.textures, 2, "without a skin: two pack textures");
    check_eq(table.totals.generated_textures, 0, "without a skin: nothing generated");
}

// Defect: a table built over a scene the backend would refuse. The builder
// runs the validator first, so a scene tampered into an inconsistent state
// is refused with the validator's message and leaves the table empty rather
// than half built.
void test_tables_refuse_an_invalid_scene() {
    CarFixture fixture = car_fixture(true);
    nfsu::UploadTable table;
    std::string error;
    const bool gpu_ok_10 = nfsu::build_car_upload_table(fixture.scene, kSkinSource, table, error);
    check(gpu_ok_10, "car table before tampering: " + error);
    fixture.scene.textures[1].rgba.pop_back();
    check(!nfsu::build_car_upload_table(fixture.scene, kSkinSource, table, error),
          "a car scene with a short texture is refused");
    check_has(error, "car scene refused: ", "the car refusal is the validator's");
    check_eq(table.resources.size(), 0, "a refused car table is empty");
    check_eq(table.totals.resources, 0, "a refused car table has no totals");

    TrackFixture track = track_fixture();
    const bool gpu_ok_11 = nfsu::build_track_upload_table(track.scene, table, error);
    check(gpu_ok_11, "track table before tampering: " + error);
    track.scene.region.meshes[1].vertices.pop_back();
    check(!nfsu::build_track_upload_table(track.scene, table, error),
          "a track scene with a vertex removed is refused");
    check_has(error, "track scene refused: ", "the track refusal is the validator's");
    check_eq(table.resources.size(), 0, "a refused track table is empty");
}

// Defect: the track table as a second accounting. TrackDrawCost already
// counts what a backend uploads; this table must be that upload broken down,
// so its totals equal the cost's to the byte, and a change to either side
// breaks this test until both agree again. The per-resource list is pinned
// as well, in order.
void test_track_table_is_the_region_cost() {
    TrackFixture fixture = track_fixture();
    const nfsu::PreparedTrackRegion& region = fixture.scene.region;
    check_eq(region.meshes.size(), 3, "the track region holds three meshes");
    check_eq(region.textures.size(), 2, "the track region holds two textures");
    nfsu::UploadTable table;
    std::string error;
    const bool gpu_ok_12 = nfsu::build_track_upload_table(fixture.scene, table, error);
    check(gpu_ok_12, "track table: " + error);
    using Kind = nfsu::UploadResourceKind;
    using Life = nfsu::UploadLifetime;
    using Memory = nfsu::UploadMemory;
    check_table(table,
                {{Kind::vertex_buffer, Life::scene, Memory::buffer,
                  "track mesh 0 BARRIER_A vertex buffer", 0, 144, 4, 24, 6, 0},
                 {Kind::index_buffer, Life::scene, Memory::buffer,
                  "track mesh 0 BARRIER_A index buffer", 0, 18, 2, 2, 9, 0},
                 {Kind::vertex_buffer, Life::scene, Memory::buffer,
                  "track mesh 1 BARRIER_B vertex buffer", 1, 72, 4, 24, 3, 0},
                 {Kind::index_buffer, Life::scene, Memory::buffer,
                  "track mesh 1 BARRIER_B index buffer", 1, 6, 2, 2, 3, 0},
                 {Kind::vertex_buffer, Life::scene, Memory::buffer,
                  "track mesh 2 LAMP vertex buffer", 2, 72, 4, 24, 3, 0},
                 {Kind::index_buffer, Life::scene, Memory::buffer,
                  "track mesh 2 LAMP index buffer", 2, 6, 2, 2, 3, 0},
                 {Kind::texture, Life::scene, Memory::image, "track texture 0 hash 0xAAAA0001",
                  0, 32, 32, 4, 8, kTrackTexture1},
                 {Kind::texture, Life::scene, Memory::image, "track texture 1 hash 0xAAAA0002",
                  1, 60, 32, 4, 15, kTrackTexture2}},
                "track table");
    check_eq(table.totals.vertex_bytes, region.cost.vertex_buffer_bytes,
             "vertex bytes are TrackDrawCost::vertex_buffer_bytes");
    check_eq(table.totals.index_bytes, region.cost.index_buffer_bytes,
             "index bytes are TrackDrawCost::index_buffer_bytes");
    check_eq(table.totals.texture_bytes, region.cost.texture_bytes,
             "texture bytes are TrackDrawCost::texture_bytes");
    check_eq(table.totals.vertex_bytes, 288, "vertex bytes");
    check_eq(table.totals.index_bytes, 30, "index bytes");
    check_eq(table.totals.texture_bytes, 92, "texture bytes");
    check_eq(table.totals.generated_textures, 0, "a track has no generated texture");
    if (table.resources.size() == 8) {
        check(table.resources[2].data ==
                  reinterpret_cast<const unsigned char*>(region.meshes[1].vertices.data()),
              "the track vertex buffer points at the region's vertices");
    }
}

// Defect: a table that lists a stream it cannot upload. An arena refuses a
// zero-byte request, so a table that listed an empty stream would make the
// whole scene's upload fail. The car below carries a third mesh with
// vertices and no triangles or materials - a mesh prepare_car_scene accepts,
// and one no group draws - so its index stream is empty and must be counted
// and left out, while its vertex buffer is still listed.
void test_empty_streams_are_counted_not_listed() {
    nfsu::GeometryReport geometry = car_geometry();
    nfsu::Mesh marker;
    marker.index = 2;
    marker.name = "MARKER";
    marker.valid = true;
    marker.vertices = {car_vertex(2, 0, 0), car_vertex(2, 1, 0)};
    marker.directory_hash = 0x01020306U;
    marker.directory_offset = 0x400U;
    marker.directory_size = 0x80U;
    marker.has_directory_entry = true;
    geometry.meshes.push_back(marker);
    nfsu::RenderTextureSet textures;
    textures.textures.push_back(make_texture(kPaintHash, 4, 4, 0x40));
    nfsu::PreparedCarScene scene;
    std::string error;
    const bool gpu_ok_13 = nfsu::prepare_car_scene(geometry, textures, nfsu::MeshSelection{},
                                  nfsu::SceneLimits{}, scene, error);
    check(gpu_ok_13, "a car with a mesh that draws nothing is prepared: " + error);
    check_eq(scene.meshes.size(), 3, "the empty mesh is prepared");
    nfsu::UploadTable table;
    const bool gpu_ok_14 = nfsu::build_car_upload_table(scene, nfsu::kNoHandle, table, error);
    check(gpu_ok_14, "the table over it: " + error);
    check_eq(table.totals.empty_buffers, 1, "one empty stream is counted");
    check_eq(table.totals.vertex_buffers, 3, "all three vertex buffers are listed");
    check_eq(table.totals.index_buffers, 2, "only the two drawn index buffers are listed");
    bool listed = false;
    for (const nfsu::UploadResource& resource : table.resources) {
        check(resource.bytes != 0, resource.name + " is not empty");
        listed = listed || resource.name == "car mesh 2 MARKER vertex buffer";
    }
    check(listed, "the empty mesh's vertex buffer is listed");
}

// Defect: resource names and hashes taken from the wrong place. A name is
// built from file bytes, so every byte outside 0x20..0x7E must become '?' -
// and every byte inside it, the space and the tilde at its two ends
// included, must be kept - and an unnamed mesh must not leave a double
// space. A texture's hash is RenderTexture::hash, which always counts as
// claimed, not the first claim, which a hand-built texture may leave empty.
// And kNoHandle means "no generated skin" even for a texture whose own
// source index is kNoHandle, as a hand-built one may be.
void test_names_hashes_and_sources() {
    nfsu::GeometryReport geometry = car_geometry();
    geometry.meshes[0].name = "A~ B";
    geometry.meshes[1].name = std::string("W\x01\xC3", 3);
    nfsu::Mesh unnamed;
    unnamed.index = 2;
    unnamed.valid = true;
    unnamed.vertices = {car_vertex(3, 0, 0)};
    unnamed.directory_hash = 0x01020307U;
    unnamed.directory_offset = 0x500U;
    unnamed.directory_size = 0x80U;
    unnamed.has_directory_entry = true;
    geometry.meshes.push_back(unnamed);

    nfsu::RenderTextureSet textures;
    nfsu::RenderTexture paint = make_texture(kPaintHash, 4, 4, 0x40);
    paint.claims = {};
    paint.claim_count = 0;
    paint.source = nfsu::kNoHandle;
    textures.textures.push_back(paint);
    nfsu::RenderTexture rim = make_texture(kRimHash, 2, 3, 0x50);
    rim.source = nfsu::kNoHandle;
    textures.textures.push_back(rim);

    nfsu::PreparedCarScene scene;
    std::string error;
    const bool gpu_ok_15 = nfsu::prepare_car_scene(geometry, textures, nfsu::MeshSelection{}, nfsu::SceneLimits{},
                                  scene, error);
    check(gpu_ok_15, "prepare the renamed car: " + error);
    nfsu::UploadTable table;
    const bool gpu_ok_16 = nfsu::build_car_upload_table(scene, nfsu::kNoHandle, table, error);
    check(gpu_ok_16, "its table: " + error);
    std::vector<std::string> names;
    for (const nfsu::UploadResource& resource : table.resources) {
        names.push_back(resource.name);
    }
    const std::vector<std::string> expected{
        "car mesh 0 A~ B vertex buffer",   "car mesh 0 A~ B index buffer",
        "car mesh 1 W?? vertex buffer",    "car mesh 1 W?? index buffer",
        "car mesh 2 vertex buffer",        "car texture 0 hash 0x5C1A0002",
        "car texture 1 hash 0x5C1A0003"};
    check(names == expected, "names keep printable bytes, replace the rest, skip an empty name");
    if (table.resources.size() == expected.size()) {
        check_eq(table.resources[5].texture_hash, kPaintHash,
                 "a texture with no claims answers to its own hash");
    }
    check_eq(table.totals.generated_textures, 0,
             "kNoHandle marks nothing generated, even a texture whose source is kNoHandle");
    check_eq(table.totals.empty_buffers, 1, "the unnamed mesh has no triangles");
}

// ---------------------------------------------------------------------------
// The ledger
// ---------------------------------------------------------------------------

struct Rig {
    nfsu::MemoryArena buffers;
    nfsu::MemoryArena images;
    nfsu::MemoryArena staging;
    nfsu::UploadLedger ledger;
};

void make_rig(Rig& rig, std::uint64_t buffer_bytes = 4096, std::uint64_t image_bytes = 4096,
              std::uint64_t staging_bytes = 1024) {
    rig.buffers = make_arena(buffer_bytes, 4096);
    rig.images = make_arena(image_bytes, 4096);
    rig.staging = make_arena(staging_bytes, 4096);
    std::string error;
    const bool gpu_ok_17 = rig.ledger.create(nfsu::UploadArenas{&rig.buffers, &rig.images, &rig.staging},
                            nfsu::linear_image_layout, 256, error);
    check(gpu_ok_17, "ledger create: " + error);
}

nfsu::UploadTable table_of(const nfsu::PreparedCarScene& scene) {
    nfsu::UploadTable table;
    std::string error;
    const bool gpu_ok_18 = nfsu::build_car_upload_table(scene, kSkinSource, table, error);
    check(gpu_ok_18, "car table: " + error);
    return table;
}

nfsu::UploadTable table_of(const nfsu::PreparedTrackScene& scene) {
    nfsu::UploadTable table;
    std::string error;
    const bool gpu_ok_19 = nfsu::build_track_upload_table(scene, table, error);
    check(gpu_ok_19, "track table: " + error);
    return table;
}

void test_native_track_section_lifetime() {
    for (const auto storage : {nfsu::RenderTextureStorage::dxt1, nfsu::RenderTextureStorage::dxt3}) {
        const bool dxt3 = storage == nfsu::RenderTextureStorage::dxt3;
        TrackFixture fixture = track_fixture();
        auto& scene = fixture.scene;
        scene.region.cost.texture_bytes = 0;
        for (auto& texture : scene.region.textures) {
            texture.storage = storage;
            texture.rgba.clear();
            texture.blocks.assign(((texture.width + 3) / 4) * ((texture.height + 3) / 4) * (dxt3 ? 16 : 8), 0x5A);
            scene.region.cost.texture_bytes += texture.blocks.size();
        }
        auto table = table_of(scene);
        check(table.totals.textures > 0, "native section has testable image resources");
        for (const auto& resource : table.resources) {
            if (resource.kind != nfsu::UploadResourceKind::texture) continue;
            check(resource.texture.format == (dxt3 ? nfsu::TextureUploadFormat::dxt3 :
                                                    nfsu::TextureUploadFormat::dxt1),
                  "native upload preserves BC1 versus BC2 format");
            check_eq(resource.texture.texel_bytes, dxt3 ? 16 : 8,
                     "native upload preserves block byte width");
        }
        nfsu::SceneMemoryRequirement need;
        std::string error;
        check(nfsu::upload_table_requirement(table, nfsu::bounded_image_layout, need, error),
              "native section bound: " + error);
        Rig rig;
        make_rig(rig, 4096, need.image_extent, 4096);
        check(rig.ledger.create({&rig.buffers, &rig.images, &rig.staging},
                                nfsu::bounded_image_layout, 256, error), "bounded native ledger");
        std::vector<nfsu::UploadHandle> handles;
        check(rig.ledger.upload_table(table, 10, handles, error), "native section upload: " + error);
        check(rig.staging.stats().used_bytes > 0, "native blocks staged before fence");
        rig.ledger.retire(9);
        check(rig.staging.stats().used_bytes > 0, "native staging survives incomplete upload");
        rig.ledger.retire(10);
        check_eq(rig.staging.stats().used_bytes, 0, "native staging released at upload fence");
        check(rig.ledger.release_all(handles, 20, error), "evict native section");
        rig.ledger.retire(19);
        check(rig.images.stats().used_bytes > 0, "native images survive last draw fence");
        rig.ledger.retire(20);
        check_eq(rig.images.stats().used_bytes, 0, "native section images released");
        check_eq(rig.buffers.stats().used_bytes, 0, "native section geometry released");
        Rig small;
        make_rig(small, 4096, need.image_extent - 1, 4096);
        check(small.ledger.create({&small.buffers, &small.images, &small.staging},
                                  nfsu::bounded_image_layout, 256, error), "small native ledger");
        check(!small.ledger.upload_table(table, 1, handles, error) && !error.empty(),
              "oversized native section returns a diagnostic");
        check_eq(small.images.stats().used_bytes, 0, "refused native section rolls back images");
        check_eq(small.buffers.stats().used_bytes, 0, "refused native section rolls back geometry");
        check_eq(small.staging.stats().used_bytes, 0, "refused native section rolls back staging");
    }
}

struct Placed {
    std::uint64_t offset;
    std::uint64_t size;
};

// Every placement of every handle, checked against the arena it must be in:
// aligned as the resource asked, inside the arena, and overlapping no other
// placement in the same arena.
std::vector<Placed> placements(const nfsu::UploadLedger& ledger,
                               const std::vector<nfsu::UploadHandle>& handles,
                               const nfsu::UploadTable& table, const std::string& what) {
    std::vector<Placed> out;
    std::map<std::uint64_t, std::uint64_t> buffers;
    std::map<std::uint64_t, std::uint64_t> images;
    std::map<std::uint64_t, std::uint64_t> staging;
    const auto claim = [&what](std::map<std::uint64_t, std::uint64_t>& taken,
                               const nfsu::ArenaAllocation& range, const std::string& name) {
        for (const auto& entry : taken) {
            const bool apart = range.offset + range.size <= entry.first ||
                               entry.first + entry.second <= range.offset;
            check(apart, what + ": " + name + " overlaps another placement");
        }
        taken[range.offset] = range.size;
    };
    for (std::size_t i = 0; i < handles.size(); ++i) {
        nfsu::UploadRecord record;
        if (!ledger.find(handles[i], record)) {
            check(false, what + ": handle " + std::to_string(i) + " is not live");
            continue;
        }
        const nfsu::UploadResource& resource = table.resources[i];
        check(record.name == resource.name, what + ": record " + std::to_string(i) + " name");
        check(record.placement.offset % resource.alignment == 0,
              what + ": " + record.name + " is aligned");
        check_eq(record.placement.size, resource.bytes, what + ": " + record.name + " size");
        claim(record.memory == nfsu::UploadMemory::image ? images : buffers, record.placement,
              record.name);
        if (record.staged) {
            check_eq(record.staging.size, resource.bytes, what + ": " + record.name + " staging");
            check(record.staging.offset % nfsu::kTextureStagingAlignment == 0,
                  what + ": " + record.name + " staging is aligned");
            claim(staging, record.staging, record.name + " staging");
        }
        out.push_back(Placed{record.placement.offset, record.placement.size});
    }
    return out;
}

// Defect: the lifetime rules broken in the direction that corrupts a frame.
// Staging reclaimed before its copy's fence lets the next upload overwrite
// bytes the copy engine has not read yet; a released buffer reclaimed before
// its last use lets a draw in flight read another resource's bytes. Each
// transition below happens at exactly one fence and not one earlier.
void test_ledger_fences() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    Rig rig;
    make_rig(rig);
    std::vector<nfsu::UploadHandle> handles;
    std::string error;
    const bool gpu_ok_20 = rig.ledger.upload_table(table, 10, handles, error);
    check(gpu_ok_20, "upload at fence 10: " + error);
    check_eq(handles.size(), table.resources.size(), "one handle per resource");
    placements(rig.ledger, handles, table, "fences");
    nfsu::UploadLedgerStats stats = rig.ledger.stats();
    check_eq(stats.resident, 7, "resident after upload");
    check_eq(stats.resident_bytes, 486, "resident bytes are the table's bytes");
    check_eq(stats.staged, 3, "every texture is staged");
    check_eq(stats.staged_bytes, 216, "staged bytes are the texture bytes");
    check_eq(rig.staging.stats().used_bytes, 216, "the staging arena holds them");

    rig.ledger.retire(9);
    check_eq(rig.ledger.stats().staged, 3, "staging survives fence 9");
    rig.ledger.retire(10);
    stats = rig.ledger.stats();
    check_eq(stats.staged, 0, "staging is reclaimed at fence 10");
    check_eq(stats.resident, 7, "the resources stay resident");
    check_eq(rig.staging.stats().used_bytes, 0, "the staging arena is empty");

    // A resource released before its upload fence waits for the copy too:
    // the texture uploaded at fence 20 and released for last use at 15
    // stays until 20.
    const nfsu::UploadResource& skin = table.resources[4];
    nfsu::UploadHandle late;
    const bool gpu_ok_21 = rig.ledger.upload(skin, 20, late, error);
    check(gpu_ok_21, "upload a skin at 20: " + error);
    const bool gpu_ok_22 = rig.ledger.release(late, 15, error);
    check(gpu_ok_22, "release it for last use at 15: " + error);
    rig.ledger.retire(15);
    check_eq(rig.ledger.stats().releasing, 1, "a copy in flight holds its image at 15");
    rig.ledger.retire(19);
    check_eq(rig.ledger.stats().releasing, 1, "and at 19");
    rig.ledger.retire(20);
    check_eq(rig.ledger.stats().releasing, 0, "and not at 20");

    // The converse: uploaded long ago, released for last use at 30.
    const bool gpu_ok_23 = rig.ledger.release_all(handles, 30, error);
    check(gpu_ok_23, "release the car at 30: " + error);
    stats = rig.ledger.stats();
    check_eq(stats.releasing, 7, "releasing after release_all");
    check_eq(stats.resident, 0, "nothing resident after release_all");
    rig.ledger.retire(29);
    check_eq(rig.ledger.stats().releasing, 7, "a draw in flight holds the car at 29");
    check_eq(rig.buffers.stats().used_bytes, 270, "the buffers are still placed at 29");
    rig.ledger.retire(30);
    check_eq(rig.ledger.stats().releasing, 0, "the car is reclaimed at 30");
    check_eq(rig.buffers.stats().used_bytes, 0, "the buffer arena is empty");
    check_eq(rig.images.stats().used_bytes, 0, "the image arena is empty");

    // Fences only move forward.
    rig.ledger.retire(3);
    check_eq(rig.ledger.stats().completed_fence, 30, "a smaller fence changes nothing");
    stats = rig.ledger.stats();
    check_eq(stats.uploads, 8, "uploads counted");
    check_eq(stats.releases, 8, "releases counted");
    check_eq(stats.reclaimed, 8, "reclaims counted");
}

// Defect: accounting that does not return to where it started. Issue #52
// asks for repeated scene load and unload cycles; without hardware this runs
// them against the ledger and its arenas. Each cycle uploads the car and the
// track, releases both, and retires; the arenas must be exactly empty, the
// ledger must hold nothing, and every placement of every cycle must be where
// the first cycle put it. Every handle's slot must stay below the 15
// resources a cycle holds, so a ledger that never reuses a slot is caught in
// the second cycle rather than after it has grown past its reservation. 500
// cycles, and a leak of one staging copy per cycle shows up long before the
// end.
void test_upload_release_upload_returns_to_a_known_state() {
    CarFixture car = car_fixture(true);
    TrackFixture track = track_fixture();
    const nfsu::UploadTable car_table = table_of(car.scene);
    const nfsu::UploadTable track_table = table_of(track.scene);
    Rig rig;
    make_rig(rig);
    const nfsu::ArenaStats empty_buffers = rig.buffers.stats();
    const nfsu::ArenaStats empty_images = rig.images.stats();
    const nfsu::ArenaStats empty_staging = rig.staging.stats();
    std::vector<Placed> first;
    std::uint64_t fence = 0;
    std::string error;
    for (int cycle = 0; cycle < 500; ++cycle) {
        const std::string what = "cycle " + std::to_string(cycle);
        std::vector<nfsu::UploadHandle> car_handles;
        std::vector<nfsu::UploadHandle> track_handles;
        ++fence;
        if (!rig.ledger.upload_table(car_table, fence, car_handles, error) ||
            !rig.ledger.upload_table(track_table, fence, track_handles, error)) {
            check(false, what + ": upload refused: " + error);
            return;
        }
        const std::size_t per_cycle = car_table.resources.size() + track_table.resources.size();
        for (const std::vector<nfsu::UploadHandle>* list : {&car_handles, &track_handles}) {
            for (const nfsu::UploadHandle& handle : *list) {
                if (handle.slot >= per_cycle) {
                    check(false, what + ": slot " + std::to_string(handle.slot) + " of " +
                                     std::to_string(per_cycle));
                    return;
                }
            }
        }
        std::vector<Placed> placed = placements(rig.ledger, car_handles, car_table, what);
        const std::vector<Placed> more = placements(rig.ledger, track_handles, track_table, what);
        placed.insert(placed.end(), more.begin(), more.end());
        if (cycle == 0) {
            first = placed;
        } else {
            bool same = placed.size() == first.size();
            for (std::size_t i = 0; same && i < placed.size(); ++i) {
                same = placed[i].offset == first[i].offset && placed[i].size == first[i].size;
            }
            if (!same) {
                check(false, what + ": placements moved from cycle 0");
                return;
            }
        }
        ++fence;
        if (!rig.ledger.release_all(track_handles, fence, error) ||
            !rig.ledger.release_all(car_handles, fence, error)) {
            check(false, what + ": release refused: " + error);
            return;
        }
        rig.ledger.retire(fence);
        const nfsu::UploadLedgerStats stats = rig.ledger.stats();
        if (stats.resident != 0 || stats.releasing != 0 || stats.staged != 0 ||
            !rig.ledger.unreleased().empty() || !rig.ledger.releasing().empty() ||
            !same_contents(rig.buffers.stats(), empty_buffers) ||
            !same_contents(rig.images.stats(), empty_images) ||
            !same_contents(rig.staging.stats(), empty_staging)) {
            check(false, what + ": the accounting did not return to empty");
            return;
        }
    }
    const nfsu::UploadLedgerStats stats = rig.ledger.stats();
    const std::uint64_t per_cycle = car_table.resources.size() + track_table.resources.size();
    check_eq(stats.uploads, 500U * per_cycle, "uploads over every cycle");
    check_eq(stats.reclaimed, 500U * per_cycle, "reclaims over every cycle");
    check_eq(stats.refused_uploads + stats.refused_releases + stats.lost_placements, 0,
             "no refusal and no lost placement");
    check_eq(rig.buffers.stats().high_water_bytes,
             car_table.totals.vertex_bytes + car_table.totals.index_bytes +
                 track_table.totals.vertex_bytes + track_table.totals.index_bytes,
             "the buffer peak is both scenes' buffers, and did not creep");
}

// Defect: a leak report that misses a leak, names the wrong resource, or
// counts a released resource the GPU has not finished with as leaked. Two
// resources are left unreleased on purpose and must be reported by name, in
// slot order, and nothing else, both while six others are still releasing
// and after they are reclaimed.
void test_leaks_are_reported_by_name() {
    TrackFixture track = track_fixture();
    const nfsu::UploadTable table = table_of(track.scene);
    Rig rig;
    make_rig(rig);
    std::vector<nfsu::UploadHandle> handles;
    std::string error;
    const bool gpu_ok_24 = rig.ledger.upload_table(table, 1, handles, error);
    check(gpu_ok_24, "upload: " + error);
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (i != 3 && i != 6) {
            const bool gpu_ok_25 = rig.ledger.release(handles[i], 2, error);
            check(gpu_ok_25, "release: " + error);
        }
    }
    check(rig.ledger.releasing().size() == 6, "six are releasing before the fence");
    // A released resource is not a leak even while the GPU may still read
    // it, so the report is the same before the fence as after it.
    const std::vector<std::string> expected{"track mesh 1 BARRIER_B index buffer",
                                            "track texture 0 hash 0xAAAA0001"};
    check(rig.ledger.unreleased() == expected,
          "before the fence only the two unreleased resources are reported");
    rig.ledger.retire(2);
    check(rig.ledger.unreleased() == expected,
          "the two unreleased resources are reported by name");
    check(rig.ledger.releasing().empty(), "nothing is left releasing");
    check_eq(rig.ledger.stats().resident, 2, "two stay resident");
    check_eq(rig.buffers.stats().used_bytes, 6, "the leaked index buffer is still placed");
    check_eq(rig.images.stats().used_bytes, 32, "the leaked texture is still placed");
}

// Defect: a double release that is not refused. A second release before the
// reclaim would overwrite the first release's fence, possibly with an
// earlier one, and reclaim memory a draw still reads; one after the reclaim,
// or through a handle whose slot a new upload now holds, would free the new
// resource. Each is refused by name and changes nothing.
void test_double_release_is_refused_by_name() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    Rig rig;
    make_rig(rig);
    const nfsu::UploadResource& paint = table.resources[5];
    nfsu::UploadHandle handle;
    std::string error;
    const bool gpu_ok_26 = rig.ledger.upload(paint, 1, handle, error);
    check(gpu_ok_26, "upload: " + error);
    const bool gpu_ok_27 = rig.ledger.release(handle, 8, error);
    check(gpu_ok_27, "release at 8: " + error);
    check(!rig.ledger.release(handle, 2, error), "a second release before the reclaim");
    check_has(error, "double release of car texture 1 hash 0x5C1A0002: already released at fence 8",
              "the second release names the resource and keeps the first fence");
    rig.ledger.retire(2);
    check_eq(rig.ledger.stats().releasing, 1, "the earlier fence did not reclaim it");
    rig.ledger.retire(8);
    check_eq(rig.ledger.stats().releasing, 0, "fence 8 reclaims it");
    check(!rig.ledger.release(handle, 9, error), "a release after the reclaim");
    check_has(error, "double release of car texture 1 hash 0x5C1A0002: it was released",
              "after the reclaim it is still named");

    nfsu::UploadHandle next;
    const bool gpu_ok_28 = rig.ledger.upload(table.resources[0], 9, next, error);
    check(gpu_ok_28, "a new upload: " + error);
    check_eq(next.slot, handle.slot, "the new upload takes the freed slot");
    check(!rig.ledger.release(handle, 10, error), "a release through the old handle");
    check_has(error, "double release of an upload reclaimed earlier", "a reused slot");
    nfsu::UploadRecord record;
    check(rig.ledger.find(next, record) && !record.released,
          "the new upload is untouched by the old handle");

    nfsu::UploadHandle forged = next;
    forged.generation += 5U;
    check(!rig.ledger.release(forged, 10, error), "a generation never issued");
    check_has(error, "never issued", "a forged handle");
    check(!rig.ledger.release(nfsu::UploadHandle{}, 10, error), "a default handle");
    check_has(error, "never issued", "a default handle");
    check_eq(rig.ledger.stats().refused_releases, 5, "five refused releases");
    check_eq(rig.ledger.stats().resident, 1, "the new upload is still resident");
}

// Defect: release_all that does not stop at the first refusal, or that
// reports success despite one. A caller batches a frame's releases and
// trusts a true return to mean every handle in the list is now releasing;
// a regressed release_all that returns true after a refusal would leave the
// handles after the refused one silently unreleased and never retried - the
// generated skin case issue #137 names, but exercised here on plain buffers
// so no staging arena is involved.
void test_release_all_stops_at_first_refusal() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    Rig rig;
    make_rig(rig);
    std::vector<nfsu::UploadHandle> handles;
    std::string error;
    const bool gpu_ok_29 = rig.ledger.upload_table(table, 1, handles, error);
    check(gpu_ok_29, "upload: " + error);
    check_eq(handles.size(), 7, "one handle per car table resource");

    // handles[1] ("car mesh 0 BODY index buffer") is released ahead of time,
    // so the batch below re-releases it and is refused on that entry.
    const bool gpu_ok_30 = rig.ledger.release(handles[1], 5, error);
    check(gpu_ok_30, "release ahead of time: " + error);
    check_eq(rig.ledger.stats().releasing, 1, "one handle releasing before the batch");

    const std::vector<nfsu::UploadHandle> batch{handles[0], handles[1], handles[2]};
    const bool released_all = rig.ledger.release_all(batch, 9, error);
    check(!released_all, "a batch with an already-released handle is refused");
    check_has(error,
              "double release of car mesh 0 BODY index buffer: already released at fence 5",
              "the refusal names the double release, not a made-up reason");

    nfsu::UploadRecord record;
    check(rig.ledger.find(handles[0], record) && record.released,
          "the handle ahead of the refused one was released before the batch stopped");
    check(rig.ledger.find(handles[2], record) && !record.released,
          "the handle after the refused one was never reached, so it is still resident");
    check_eq(rig.ledger.stats().releasing, 2,
             "only the two handles up to and including the refused one are releasing");
    check_eq(rig.ledger.stats().resident, 5,
             "the handle after the refused one, and the three never in the batch, stay resident");
}

// Defect: a handle the ledger never issued accepted as one it did. Two such
// handles are easy to come by: the slots of a refused table, which were
// taken and returned without a handle ever leaving the call, and the
// current generation of a slot whose resource was reclaimed. Releasing
// either would mark a dead slot released and hand its stale placements back
// to the arena a second time. find() must not see them either. And a double
// release names its resource only while that resource's record is still the
// slot's: once a newer upload has come and gone, the name there is the
// newer one's, and naming it would blame the wrong resource.
void test_handles_the_ledger_never_issued() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    Rig rig;
    make_rig(rig, 200, 4096, 1024);
    std::vector<nfsu::UploadHandle> handles;
    std::string error;
    check(!rig.ledger.upload_table(table, 1, handles, error), "a table that does not fit");
    nfsu::UploadRecord record;
    for (std::uint32_t slot = 0; slot < 2; ++slot) {
        const nfsu::UploadHandle rolled_back{slot, 1};
        check(!rig.ledger.release(rolled_back, 2, error),
              "the slot of a refused table, slot " + std::to_string(slot));
        check_has(error, "never issued", "a rolled-back slot was never issued");
        check(!rig.ledger.find(rolled_back, record), "find() does not see a rolled-back slot");
    }

    Rig fresh;
    make_rig(fresh);
    nfsu::UploadHandle first;
    const bool gpu_ok_31 = fresh.ledger.upload(table.resources[0], 1, first, error);
    check(gpu_ok_31, "first: " + error);
    const bool gpu_ok_32 = fresh.ledger.release(first, 1, error);
    check(gpu_ok_32, "release first: " + error);
    fresh.ledger.retire(1);
    const nfsu::UploadHandle next_generation{first.slot, first.generation + 1U};
    check(!fresh.ledger.release(next_generation, 2, error),
          "the current generation of a reclaimed slot");
    check_has(error, "never issued", "a reclaimed slot's next generation was never issued");
    check(!fresh.ledger.find(next_generation, record), "find() does not see it");
    check(!fresh.ledger.find(first, record), "find() does not see the reclaimed handle");

    // The slot is reused by a second upload, which is itself reclaimed; the
    // name left in the slot is the second one's now.
    nfsu::UploadHandle second;
    const bool gpu_ok_33 = fresh.ledger.upload(table.resources[2], 2, second, error);
    check(gpu_ok_33, "second: " + error);
    check_eq(second.slot, first.slot, "the second upload reuses the slot");
    const bool gpu_ok_34 = fresh.ledger.release(second, 2, error);
    check(gpu_ok_34, "release second: " + error);
    fresh.ledger.retire(2);
    check(!fresh.ledger.release(first, 3, error), "the first handle after two reclaims");
    check_has(error, "its slot 0 has been reused",
              "the refusal does not name the second resource as the first");
    check(error.find("WHEEL") == std::string::npos, "it does not blame the second resource");
    check_eq(fresh.ledger.stats().lost_placements, 0, "no placement was released twice");
}

// Defect: a partial upload left behind. When the arena cannot take the
// whole table, every placement the call made must go back, nothing may stay
// recorded, and the error must name the resource that did not fit and the
// arena's reason, so the caller knows what to unload. The slots go back too,
// newest first, so that a retry on the same ledger is given exactly the slots
// the refused call took and its reports stay in table order.
void test_upload_table_is_all_or_nothing() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    const struct {
        const char* what;
        std::uint64_t buffers;
        std::uint64_t images;
        std::uint64_t staging;
        const char* message;
    } cases[] = {
        {"buffers too small", 200, 4096, 1024,
         "cannot upload car mesh 1 WHEEL vertex buffer: the gpu arena for buffer memory "
         "refused 108 bytes at alignment 4: over budget"},
        {"images too small", 4096, 150, 1024,
         "cannot upload car texture 1 hash 0x5C1A0002: the gpu arena for image memory "
         "refused 64 bytes at alignment 32: over budget"},
        {"staging too small", 4096, 4096, 100,
         "cannot upload car generated texture 0 hash 0x5C1A0001: the gpu arena for staging "
         "refused 128 bytes at alignment 32: over budget"},
    };
    for (const auto& c : cases) {
        Rig rig;
        make_rig(rig, c.buffers, c.images, c.staging);
        const nfsu::ArenaStats buffers = rig.buffers.stats();
        const nfsu::ArenaStats images = rig.images.stats();
        const nfsu::ArenaStats staging = rig.staging.stats();
        std::vector<nfsu::UploadHandle> handles(3);
        std::string error;
        check(!rig.ledger.upload_table(table, 1, handles, error),
              std::string(c.what) + ": the table is refused");
        check(error == c.message, std::string(c.what) + ": '" + error + "'");
        check(handles.empty(), std::string(c.what) + ": no handles");
        check(same_contents(rig.buffers.stats(), buffers),
              std::string(c.what) + ": the buffer arena is as it was");
        check(same_contents(rig.images.stats(), images),
              std::string(c.what) + ": the image arena is as it was");
        check(same_contents(rig.staging.stats(), staging),
              std::string(c.what) + ": the staging arena is as it was");
        const nfsu::UploadLedgerStats stats = rig.ledger.stats();
        check_eq(stats.resident + stats.releasing + stats.staged, 0,
                 std::string(c.what) + ": nothing recorded");
        check_eq(stats.uploads, 0, std::string(c.what) + ": no upload counted");
        check_eq(stats.reclaimed, 0, std::string(c.what) + ": no reclaim counted");
        check_eq(stats.refused_uploads, 1, std::string(c.what) + ": one refusal counted");
        check(rig.ledger.unreleased().empty(), std::string(c.what) + ": no leak reported");
        // The same ledger takes the table once there is room, and gives the
        // retry the slots the refused call took, in the same order, so the
        // reports list the retried table in table order.
        nfsu::ArenaConfig roomy = rig.buffers.config();
        roomy.capacity = 4096;
        const bool gpu_ok_35 = rig.buffers.create(roomy, error);
        check(gpu_ok_35, "a roomier buffer arena: " + error);
        roomy = rig.images.config();
        roomy.capacity = 4096;
        const bool gpu_ok_36 = rig.images.create(roomy, error);
        check(gpu_ok_36, "a roomier image arena: " + error);
        roomy = rig.staging.config();
        roomy.capacity = 1024;
        const bool gpu_ok_37 = rig.staging.create(roomy, error);
        check(gpu_ok_37, "a roomier staging arena: " + error);
        const bool gpu_ok_38 = rig.ledger.upload_table(table, 1, handles, error);
        check(gpu_ok_38, std::string(c.what) + ": fits in room: " + error);
        std::vector<std::string> names;
        for (const nfsu::UploadResource& resource : table.resources) {
            names.push_back(resource.name);
        }
        check(rig.ledger.unreleased() == names,
              std::string(c.what) + ": the retry is listed in table order");
    }
}

// Defect: the generated skin tied to the scene's other resources. The garage
// recomposes the skin every time a vinyl changes, with frames in flight; each
// recomposition releases the old skin for its last use and uploads a new one,
// and neither may disturb a single other placement of the car. The ledger's
// own record must say which resource is the skin, because that record is what
// a backend reads to decide what a vinyl change drops. With two
// frames in flight, at most three skins exist at once, so the image arena's
// peak is fixed after the first frames and must not creep over a thousand
// recompositions.
void test_generated_skin_is_replaced_on_its_own() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    Rig rig;
    make_rig(rig);
    std::vector<nfsu::UploadHandle> handles;
    std::string error;
    const bool gpu_ok_39 = rig.ledger.upload_table(table, 0, handles, error);
    check(gpu_ok_39, "upload the car: " + error);
    rig.ledger.retire(0);
    const std::vector<Placed> car_before = placements(rig.ledger, handles, table, "car");
    // What a backend reads on a vinyl change to decide what to drop is the
    // ledger's record, not the table, so the record must carry the lifetime.
    nfsu::UploadRecord skin_record;
    check(rig.ledger.find(handles[4], skin_record) &&
              skin_record.lifetime == nfsu::UploadLifetime::generated &&
              skin_record.kind == nfsu::UploadResourceKind::texture &&
              skin_record.memory == nfsu::UploadMemory::image,
          "the ledger records the skin as a generated image texture");
    nfsu::UploadRecord paint_record;
    check(rig.ledger.find(handles[5], paint_record) &&
              paint_record.lifetime == nfsu::UploadLifetime::scene,
          "the ledger records a pack texture with the scene lifetime");
    const std::size_t skin_index = 4;
    const nfsu::UploadResource skin = table.resources[skin_index];
    check(skin.lifetime == nfsu::UploadLifetime::generated, "resource 4 is the skin");
    nfsu::UploadHandle current = handles[skin_index];
    std::uint64_t settled_peak = 0;
    for (std::uint64_t frame = 1; frame <= 1000; ++frame) {
        const std::string what = "frame " + std::to_string(frame);
        // The old skin was last drawn by the frame before this one.
        if (!rig.ledger.release(current, frame - 1U, error) ||
            !rig.ledger.upload(skin, frame, current, error)) {
            check(false, what + ": " + error);
            return;
        }
        // Two frames in flight: the GPU has finished the one before last.
        if (frame >= 2) {
            rig.ledger.retire(frame - 2U);
        }
        const nfsu::UploadLedgerStats stats = rig.ledger.stats();
        if (stats.resident != 7 || stats.releasing > 2) {
            check(false, what + ": resident " + std::to_string(stats.resident) + ", releasing " +
                             std::to_string(stats.releasing));
            return;
        }
        const nfsu::ArenaStats images = rig.images.stats();
        if (frame == 10) {
            settled_peak = images.high_water_bytes;
        } else if (frame > 10 && images.high_water_bytes != settled_peak) {
            check(false, what + ": the image peak moved to " +
                             std::to_string(images.high_water_bytes));
            return;
        }
    }
    check_eq(settled_peak, 88 + 3U * 128U, "the peak is the two pack textures and three skins");
    std::vector<nfsu::UploadHandle> others = handles;
    others.erase(others.begin() + static_cast<std::ptrdiff_t>(skin_index));
    nfsu::UploadTable others_table = table;
    others_table.resources.erase(others_table.resources.begin() +
                                 static_cast<std::ptrdiff_t>(skin_index));
    std::vector<Placed> car_before_others = car_before;
    car_before_others.erase(car_before_others.begin() + static_cast<std::ptrdiff_t>(skin_index));
    const std::vector<Placed> car_after = placements(rig.ledger, others, others_table, "car");
    bool same = car_after.size() == car_before_others.size();
    for (std::size_t i = 0; same && i < car_after.size(); ++i) {
        same = car_after[i].offset == car_before_others[i].offset;
    }
    check(same, "no other placement of the car moved in a thousand recompositions");
    rig.ledger.retire(1000);
    check_eq(rig.ledger.stats().releasing, 0, "every old skin is reclaimed at the last fence");
    check_eq(rig.images.stats().used_bytes, 88 + 128, "the pack textures and one skin remain");
}

// Defect: an arena and a ledger that no longer describe the same memory,
// unnoticed. Resetting the buffer arena behind the ledger's back, as a
// per-frame reset of the wrong arena would, invalidates every placement the
// ledger holds there; the reclaim that follows must count each one as lost
// rather than succeed silently.
void test_lost_placements_are_counted() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    Rig rig;
    make_rig(rig);
    std::vector<nfsu::UploadHandle> handles;
    std::string error;
    const bool gpu_ok_40 = rig.ledger.upload_table(table, 1, handles, error);
    check(gpu_ok_40, "upload: " + error);
    rig.buffers.reset();
    const bool gpu_ok_41 = rig.ledger.release_all(handles, 2, error);
    check(gpu_ok_41, "release: " + error);
    rig.ledger.retire(2);
    check_eq(rig.ledger.stats().lost_placements, 4, "the four buffer placements are lost");
    check_eq(rig.images.stats().used_bytes, 0, "the image placements were reclaimed");
}

// Defect: a ledger created over nothing - no buffer arena, no image arena,
// no layout query - or one re-created while it still tracks placements,
// which would forget them while the arenas keep them; a resource cap that is
// documented but not enforced, so a ledger reserves bookkeeping for any
// count it is asked for; a block limit that refuses the largest block; and a
// re-created ledger that keeps the old completed fence, which reclaims new
// staging before its copy has run.
void test_ledger_creation_is_checked() {
    nfsu::MemoryArena arena = make_arena(4096, 4096);
    nfsu::UploadLedger ledger;
    std::string error;
    nfsu::UploadResource resource;
    resource.name = "a buffer";
    resource.bytes = 16;
    resource.alignment = 4;
    nfsu::UploadHandle handle;
    check(!ledger.upload(resource, 0, handle, error), "an uncreated ledger uploads nothing");
    check_has(error, "was not created", "an uncreated ledger says so");
    check(!ledger.create(nfsu::UploadArenas{nullptr, &arena, &arena}, nfsu::linear_image_layout,
                         4, error),
          "no buffer arena");
    check(!ledger.create(nfsu::UploadArenas{&arena, nullptr, &arena}, nfsu::linear_image_layout,
                         4, error),
          "no image arena");
    check_has(error, "needs a buffer arena and an image arena", "a missing arena is named");
    check(!ledger.create(nfsu::UploadArenas{&arena, &arena, &arena}, nfsu::ImageLayoutQuery{}, 4,
                         error),
          "no layout query");
    check(!ledger.create(nfsu::UploadArenas{&arena, &arena, &arena}, nfsu::linear_image_layout,
                         0, error),
          "no resources");
    check(!ledger.create(nfsu::UploadArenas{&arena, &arena, &arena}, nfsu::linear_image_layout,
                         nfsu::kMaxArenaAllocations + 1U, error),
          "one resource more than the cap");
    check_has(error, "max resources 4194305 is outside 1..4194304", "the resource cap is named");
    const bool gpu_ok_42 = ledger.create(nfsu::UploadArenas{&arena, &arena, nullptr}, nfsu::linear_image_layout,
                        1, error);
    check(gpu_ok_42, "one arena for buffers and images, no staging: " + error);
    const bool gpu_ok_43 = ledger.upload(resource, 0, handle, error);
    check(gpu_ok_43, "upload a buffer: " + error);
    nfsu::UploadHandle second;
    check(!ledger.upload(resource, 0, second, error), "the second of one");
    check_has(error, "already tracks 1 resources", "the resource limit says so");
    check(!ledger.create(nfsu::UploadArenas{&arena, &arena, &arena}, nfsu::linear_image_layout,
                         4, error),
          "re-creating a ledger that tracks a resource");
    check_has(error, "still tracks 1 resources", "the refused re-create says why");
    nfsu::UploadResource texture = resource;
    texture.kind = nfsu::UploadResourceKind::texture;
    texture.memory = nfsu::UploadMemory::image;
    const bool gpu_ok_44 = ledger.release(handle, 0, error);
    check(gpu_ok_44, "release: " + error);
    ledger.retire(0);
    check(!ledger.upload(texture, 0, handle, error), "a texture with no staging arena");
    check_has(error, "has no staging arena", "the missing staging arena is named");
    texture.memory = nfsu::UploadMemory::buffer;
    check(!ledger.upload(texture, 0, handle, error), "a texture in buffer memory");
    check_has(error, "a texture does not belong in buffer memory", "the wrong memory is named");
    resource.bytes = nfsu::kMaxUploadResourceBytes + 1U;
    check(!ledger.upload(resource, 0, handle, error), "a resource larger than a block");
    check_has(error, "is outside 1..4294963200", "the block limit is named");
    check_eq(arena.stats().used_bytes, 0, "no refusal placed anything");

    // The block limit is inclusive: exactly the largest block is accepted.
    nfsu::MemoryArena wide = make_arena(1ULL << 33U, 4096);
    nfsu::UploadLedger large;
    const bool gpu_ok_45 = large.create(nfsu::UploadArenas{&wide, &wide, &wide}, nfsu::linear_image_layout, 1,
                       error);
    check(gpu_ok_45, "create over a wide arena: " + error);
    resource.bytes = nfsu::kMaxUploadResourceBytes;
    const bool gpu_ok_46 = large.upload(resource, 0, handle, error);
    check(gpu_ok_46, "exactly the largest block: " + error);

    // create() starts the fence count over. A ledger that kept the old
    // completed fence would, once its new uploads are numbered from 1 again,
    // reclaim their staging before the copies that read it had run.
    nfsu::MemoryArena reused = make_arena(4096, 4096);
    nfsu::UploadLedger restarted;
    const nfsu::UploadArenas both{&reused, &reused, &reused};
    const bool gpu_ok_47 = restarted.create(both, nfsu::linear_image_layout, 4, error);
    check(gpu_ok_47, "create: " + error);
    restarted.retire(30);
    check_eq(restarted.stats().completed_fence, 30, "the first run reached fence 30");
    const bool gpu_ok_48 = restarted.create(both, nfsu::linear_image_layout, 4, error);
    check(gpu_ok_48, "create again: " + error);
    check_eq(restarted.stats().completed_fence, 0, "create() starts the fences over");
    texture.memory = nfsu::UploadMemory::image;
    nfsu::UploadHandle staged;
    const bool gpu_ok_49 = restarted.upload(texture, 1, staged, error);
    check(gpu_ok_49, "a texture at fence 1: " + error);
    restarted.retire(0);
    check_eq(restarted.stats().staged, 1, "its staging waits for fence 1");
    restarted.retire(1);
    check_eq(restarted.stats().staged, 0, "and is reclaimed at fence 1");
}

// Defect: a double release that re-creating the ledger forgives. create()
// starts the ledger over and a new upload takes the old slot number; if
// generations started over too, a handle from before create() would name the
// new upload and release it.
void test_create_again_keeps_old_handles_stale() {
    static_assert(!std::is_copy_constructible<nfsu::UploadLedger>::value &&
                      !std::is_copy_assignable<nfsu::UploadLedger>::value,
                  "two copies of one ledger would reclaim the same placements");
    nfsu::MemoryArena arena = make_arena(4096, 4096);
    nfsu::UploadLedger ledger;
    std::string error;
    const nfsu::UploadArenas arenas{&arena, &arena, &arena};
    const bool gpu_ok_50 = ledger.create(arenas, nfsu::linear_image_layout, 4, error);
    check(gpu_ok_50, "create: " + error);
    nfsu::UploadResource resource;
    resource.name = "a buffer";
    resource.bytes = 16;
    resource.alignment = 4;
    nfsu::UploadHandle old;
    const bool gpu_ok_51 = ledger.upload(resource, 1, old, error);
    check(gpu_ok_51, "upload: " + error);
    const bool gpu_ok_52 = ledger.release(old, 1, error);
    check(gpu_ok_52, "release: " + error);
    ledger.retire(1);
    const bool gpu_ok_53 = ledger.create(arenas, nfsu::linear_image_layout, 4, error);
    check(gpu_ok_53, "create again: " + error);
    nfsu::UploadHandle fresh;
    const bool gpu_ok_54 = ledger.upload(resource, 2, fresh, error);
    check(gpu_ok_54, "upload again: " + error);
    check_eq(fresh.slot, old.slot, "the slot number is reused");
    check(!ledger.release(old, 2, error), "a handle from before create() is refused");
    check_has(error, "double release of an upload reclaimed earlier", "the refusal");
    nfsu::UploadRecord record;
    check(ledger.find(fresh, record) && !record.released, "the new upload is untouched");
    check(!ledger.find(old, record), "the old handle finds nothing");
}

// Defect: a layout query that is not honoured. On deko3d the image of a
// texture is as big and as aligned as its layout says, which is at least its
// texels; the ledger must place exactly that, refuse a layout smaller than
// the texels, and refuse the texture when the query does.
void test_image_layout_query_is_honoured() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    const nfsu::UploadResource& paint = table.resources[5];
    nfsu::MemoryArena buffers = make_arena(1U << 20, 4096);
    nfsu::MemoryArena images = make_arena(1U << 20, 4096);
    nfsu::MemoryArena staging = make_arena(1U << 20, 4096);
    std::uint64_t layout_bytes = 512;
    std::uint64_t layout_alignment = 512;
    bool refuse = false;
    const nfsu::ImageLayoutQuery query = [&](const nfsu::UploadResource&, std::uint64_t& bytes,
                                             std::uint64_t& alignment, std::string& error) {
        if (refuse) {
            error = "no such layout";
            return false;
        }
        bytes = layout_bytes;
        alignment = layout_alignment;
        return true;
    };
    nfsu::UploadLedger ledger;
    std::string error;
    const bool gpu_ok_55 = ledger.create(nfsu::UploadArenas{&buffers, &images, &staging}, query, 16, error);
    check(gpu_ok_55, "create: " + error);
    nfsu::UploadHandle first;
    nfsu::UploadHandle second;
    const bool gpu_ok_56 = ledger.upload(paint, 0, first, error) && ledger.upload(paint, 0, second, error);
    check(gpu_ok_56, "two tiled textures: " + error);
    nfsu::UploadRecord record;
    check(ledger.find(second, record), "the second is live");
    check_eq(record.placement.offset, 512, "the second image is placed after the first's layout");
    check_eq(record.placement.size, 512, "the image takes the layout's bytes");
    check_eq(record.staging.size, 64, "the staging copy takes the texels only");
    check_eq(images.stats().used_bytes, 1024, "two layouts of 512");
    check_eq(ledger.stats().staged, 2, "two staging copies");
    check_eq(ledger.stats().staged_bytes, 128, "staged bytes are texels, not layouts");
    check_eq(ledger.stats().resident_bytes, 1024, "resident bytes are layouts");

    // releasing_bytes must sum the same field as resident_bytes: the image's
    // placement, which the layout query pads to 512, not its staging copy,
    // which stays at the 64-byte texel size. The two differ here, so summing
    // the wrong one is caught.
    const bool gpu_ok_57 = ledger.release(second, 7, error);
    check(gpu_ok_57, "release the second tiled image: " + error);
    const nfsu::UploadLedgerStats released = ledger.stats();
    check_eq(released.releasing, 1, "one image releasing");
    check_eq(released.releasing_bytes, 512, "releasing bytes are the layout size, not the texel size");
    check_eq(released.staged_bytes, 128, "staged bytes are unchanged by the release");
    check_eq(images.stats().used_bytes, 1024, "the release alone does not reclaim the placement");

    layout_bytes = 63;
    check(!ledger.upload(paint, 0, first, error), "a layout smaller than the texels");
    check_has(error, "the image layout needs 63 bytes for 64 bytes of texels", "the small layout");
    layout_bytes = 512;
    layout_alignment = 8192;
    check(!ledger.upload(paint, 0, first, error), "a layout aligned beyond the arena");
    check_has(error, "refused 512 bytes at alignment 8192: bad alignment", "the alignment");
    refuse = true;
    check(!ledger.upload(paint, 0, first, error), "a layout the query refuses");
    check_has(error, "image layout refused: no such layout", "the query's own refusal");
    check_eq(images.stats().used_bytes, 1024, "no refused layout placed anything");
    check_eq(staging.stats().used_bytes, 128, "and no staging either");

    // The desktop stand-in answers for textures only.
    std::uint64_t bytes = 0;
    std::uint64_t alignment = 0;
    check(!nfsu::linear_image_layout(table.resources[0], bytes, alignment, error),
          "linear_image_layout refuses a vertex buffer");
    check_has(error, "car mesh 0 BODY vertex buffer is not a texture", "and names it");
}

// ---------------------------------------------------------------------------
// Budgets
// ---------------------------------------------------------------------------

// Defect: a ceiling computed with the wrong element size, or one that wraps.
// These are the most a preparation layer can hand an upload, so a ceiling
// that comes out small for a hostile set of limits would make an unbounded
// scene look affordable. Each figure is the product the header states.
void test_derived_ceilings() {
    const nfsu::SceneLimits car_limits;
    check_eq(nfsu::car_scene_upload_ceiling(car_limits),
             (1ULL << 22U) * 36U + (1ULL << 22U) * 6U + 128ULL * 1024U * 1024U,
             "the car ceiling under the default limits");
    const nfsu::TrackSceneLimits track_limits;
    check_eq(nfsu::track_region_upload_ceiling(track_limits),
             (1ULL << 22U) * 24U + (1ULL << 22U) * 6U + 128ULL * 1024U * 1024U,
             "the track region ceiling under the default limits");
    const nfsu::TrackDrawListLimits list_limits;
    check_eq(nfsu::track_draw_list_frame_ceiling(list_limits), (1ULL << 22U) * 8U,
             "the draw list ceiling under the default limits");

    nfsu::SceneLimits hostile;
    hostile.max_vertices = UINT64_MAX / 2U;
    check_eq(nfsu::car_scene_upload_ceiling(hostile), UINT64_MAX, "a car ceiling saturates");
    nfsu::TrackSceneLimits hostile_track;
    hostile_track.max_texture_bytes = UINT64_MAX - 5U;
    check_eq(nfsu::track_region_upload_ceiling(hostile_track), UINT64_MAX,
             "a track ceiling saturates on the sum");
    nfsu::TrackDrawListLimits hostile_list;
    hostile_list.max_entries = UINT64_MAX / 4U;
    check_eq(nfsu::track_draw_list_frame_ceiling(hostile_list), UINT64_MAX,
             "a draw list ceiling saturates");
    // The largest product that fits is exact, not saturated one step early.
    nfsu::TrackDrawListLimits largest_list;
    largest_list.max_entries = UINT64_MAX / 8U;
    check_eq(nfsu::track_draw_list_frame_ceiling(largest_list), 18446744073709551608ULL,
             "the largest draw list ceiling that fits is exact");
}

// Defect: a Switch budget that does not build, one that is not assembled
// from the declared placeholders - the streaming budget for one region
// instead of kResidentRegionsPlaceholder of them - or one whose alignments
// are not the derived ones: a GPU arena that cannot honour a memory block's
// alignment, or CPU arenas aligned below what the allocator guarantees. The
// placeholder values themselves are free to change; that is what a
// placeholder is.
void test_switch_budget() {
    const nfsu::MemoryBudget budget = nfsu::switch_memory_budget();
    const auto at = [](nfsu::MemoryCategory category) {
        return static_cast<std::size_t>(category);
    };
    check_eq(budget.capacity[at(nfsu::MemoryCategory::persistent)],
             nfsu::kPersistentBudgetPlaceholder, "persistent capacity");
    check_eq(budget.capacity[at(nfsu::MemoryCategory::streaming)],
             nfsu::kResidentRegionsPlaceholder * nfsu::kStreamingRegionBudgetPlaceholder,
             "streaming capacity");
    check_eq(budget.capacity[at(nfsu::MemoryCategory::transient)],
             nfsu::kTransientBudgetPlaceholder, "transient capacity");
    check_eq(budget.capacity[at(nfsu::MemoryCategory::gpu)], nfsu::kGpuBudgetPlaceholder,
             "gpu capacity");
    for (std::size_t i = 0; i < 3; ++i) {
        check_eq(budget.max_alignment[i], alignof(std::max_align_t), "CPU alignment");
    }
    check_eq(budget.max_alignment[at(nfsu::MemoryCategory::gpu)], nfsu::kDekoMemBlockAlignment,
             "GPU alignment");
    nfsu::MemoryPlan plan;
    std::string error;
    const bool gpu_ok_58 = plan.create(budget, error);
    check(gpu_ok_58, "the Switch budget builds a plan: " + error);
    check_eq(plan.cpu_bytes(),
             nfsu::kPersistentBudgetPlaceholder +
                 nfsu::kResidentRegionsPlaceholder * nfsu::kStreamingRegionBudgetPlaceholder +
                 nfsu::kTransientBudgetPlaceholder,
             "the CPU block is the three CPU budgets end to end");
    check(nfsu::kTransientBudgetPlaceholder <
              nfsu::track_draw_list_frame_ceiling(nfsu::TrackDrawListLimits{}),
          "the frame placeholder is below the derived ceiling, as documented");
}

// Defect: a scene requirement that ignores what placement costs. The track
// table's buffers in table order leave two 2-byte gaps - each 4-aligned
// vertex buffer after a 6-byte index buffer - so the extent is 322 for 318
// bytes; and a layout query that pads images must show in the image extent.
void test_scene_requirement_places_the_table() {
    TrackFixture track = track_fixture();
    const nfsu::UploadTable table = table_of(track.scene);
    nfsu::SceneMemoryRequirement requirement;
    std::string error;
    const bool gpu_ok_59 = nfsu::upload_table_requirement(table, nfsu::linear_image_layout, requirement, error);
    check(gpu_ok_59, "requirement: " + error);
    check_eq(requirement.buffer_bytes, 318, "buffer bytes");
    check_eq(requirement.buffer_extent, 322, "buffer extent with the alignment gaps");
    check_eq(requirement.image_bytes, 92, "image bytes at the linear lower bound");
    check_eq(requirement.image_extent, 92, "image extent at the linear lower bound");
    check_eq(requirement.staging_batch_bytes, 92, "staging for every texture at once");
    check_eq(requirement.staging_largest_bytes, 60, "staging for one texture at a time");

    const nfsu::ImageLayoutQuery tiled = [](const nfsu::UploadResource& texture,
                                            std::uint64_t& bytes, std::uint64_t& alignment,
                                            std::string&) {
        bytes = (texture.bytes + 511U) / 512U * 512U + 512U;
        alignment = 1024;
        return true;
    };
    const bool gpu_ok_60 = nfsu::upload_table_requirement(table, tiled, requirement, error);
    check(gpu_ok_60, "tiled requirement: " + error);
    check_eq(requirement.image_bytes, 2048, "two images of 1024 bytes");
    check_eq(requirement.image_extent, 2048, "placed back to back at 1024");
    check_eq(requirement.staging_batch_bytes, 92, "staging does not depend on the layout");

    const nfsu::ImageLayoutQuery huge = [](const nfsu::UploadResource&, std::uint64_t& bytes,
                                           std::uint64_t& alignment, std::string&) {
        bytes = 64;
        alignment = nfsu::kMaxArenaAlignment * 2U;
        return true;
    };
    check(!nfsu::upload_table_requirement(table, huge, requirement, error),
          "an alignment above what an arena can model");
    check_has(error, "track texture 0 hash 0xAAAA0001: a request of 64 bytes at alignment 2097152",
              "the refusal names the texture whose layout asked for it");
    check_eq(requirement.image_extent, 0, "a refused requirement is zeroed");
}

// Defect: a requirement that is right only for the track table's shape.
//
//   * Its largest texture is also its last (32 then 60), so "the largest"
//     and "the last" cannot be told apart there. The car table's is first:
//     the 8x4 skin at 128 bytes, then 64, then 24. A backend sizing a
//     one-at-a-time staging arena from a "last" figure would get 24, and
//     every larger texture would then be refused as over budget at load.
//   * Its images pack with no gaps at either alignment the other test uses,
//     so the image extent and the image bytes coincide. At alignment 256 the
//     car's three images leave gaps: 216 bytes reach 536.
//   * A table with no texture at all has an image role with nothing in it;
//     that must be a zero requirement, not an arena of zero bytes refused.
//   * A layout query that answers differently when the table is placed than
//     when it was sized - a backend caching layouts under a key it later
//     changes - must be refused, not reported with an extent taken from an
//     arena that never placed that image.
//   * The simulated arena's capacity bound is inclusive: one buffer whose
//     worst case is exactly kMaxArenaCapacity is a valid arena.
void test_scene_requirement_reads_the_whole_table() {
    CarFixture car = car_fixture(true);
    const nfsu::UploadTable table = table_of(car.scene);
    nfsu::SceneMemoryRequirement requirement;
    std::string error;
    const bool gpu_ok_61 = nfsu::upload_table_requirement(table, nfsu::linear_image_layout, requirement, error);
    check(gpu_ok_61, "car requirement: " + error);
    check_eq(requirement.staging_largest_bytes, 128, "the largest texture is the first one");
    check_eq(requirement.staging_batch_bytes, 216, "car staging for every texture at once");
    check_eq(requirement.buffer_bytes, 270, "car buffer bytes");
    check_eq(requirement.buffer_extent, 270, "car buffers pack with no gap");

    const nfsu::ImageLayoutQuery spaced = [](const nfsu::UploadResource& texture,
                                             std::uint64_t& bytes, std::uint64_t& alignment,
                                             std::string&) {
        bytes = texture.bytes;
        alignment = 256;
        return true;
    };
    const bool gpu_ok_62 = nfsu::upload_table_requirement(table, spaced, requirement, error);
    check(gpu_ok_62, "spaced requirement: " + error);
    check_eq(requirement.image_bytes, 216, "spaced: the image bytes");
    check_eq(requirement.image_extent, 536, "spaced: 128 at 0, 64 at 256, 24 at 512");

    nfsu::UploadTable buffers_only = table;
    buffers_only.resources.erase(
        std::remove_if(buffers_only.resources.begin(), buffers_only.resources.end(),
                       [](const nfsu::UploadResource& resource) {
                           return resource.kind == nfsu::UploadResourceKind::texture;
                       }),
        buffers_only.resources.end());
    const bool gpu_ok_63 = nfsu::upload_table_requirement(buffers_only, nfsu::linear_image_layout, requirement,
                                         error);
    check(gpu_ok_63, "a table with no texture: " + error);
    check_eq(requirement.buffer_extent, 270, "no texture: the buffers");
    check_eq(requirement.image_bytes + requirement.image_extent + requirement.staging_batch_bytes +
                 requirement.staging_largest_bytes,
             0, "no texture: no image and no staging");

    // Sized at 64 bytes, placed at 4096: the first answer per texture is
    // the small one.
    std::map<std::string, int> asked;
    const nfsu::ImageLayoutQuery fickle = [&asked](const nfsu::UploadResource& texture,
                                                   std::uint64_t& bytes, std::uint64_t& alignment,
                                                   std::string&) {
        bytes = asked[texture.name]++ == 0 ? 64U : 4096U;
        alignment = 32;
        return true;
    };
    TrackFixture track = track_fixture();
    const nfsu::UploadTable track_table = table_of(track.scene);
    check(!nfsu::upload_table_requirement(track_table, fickle, requirement, error),
          "a layout query that answers differently the second time is refused");
    check_has(error,
              "track texture 0 hash 0xAAAA0001: over budget; the layout query answered "
              "differently the second time",
              "the refusal names the texture and the reason");
    check_eq(requirement.image_extent, 0, "a refused requirement reports no extent");

    nfsu::UploadTable single;
    nfsu::UploadResource wide;
    wide.name = "a buffer as wide as an arena";
    wide.bytes = nfsu::kMaxArenaCapacity - 3U;
    wide.alignment = 4;
    single.resources.push_back(wide);
    const bool gpu_ok_64 = nfsu::upload_table_requirement(single, nfsu::linear_image_layout, requirement, error);
    check(gpu_ok_64, "a worst case of exactly kMaxArenaCapacity: " + error);
    check_eq(requirement.buffer_extent, nfsu::kMaxArenaCapacity - 3U,
             "the widest single buffer an arena can model");
}

// Defect: a frame ceiling that is not the largest list the scene can
// produce. Every instance of the fixture visible gives the largest draw list
// the scene has, and its bytes must be exactly the ceiling; a transient arena
// of exactly that size must take the list, and one byte less must refuse it
// as over budget, which is the frame budget failing before an overflow.
void test_frame_ceiling_is_the_full_draw_list() {
    TrackFixture track = track_fixture();
    std::vector<nfsu::TrackVisibleInstance> visible;
    for (std::size_t i = 0; i < 5; ++i) {
        nfsu::TrackVisibleInstance item;
        item.section_index = 0;
        item.instance_index = i;
        visible.push_back(item);
    }
    nfsu::TrackDrawList list;
    std::string error;
    const bool gpu_ok_65 = nfsu::build_track_draw_list(track.scene, visible, nfsu::TrackDrawListOptions{}, list,
                                      error);
    check(gpu_ok_65, "the full draw list: " + error);
    const std::uint64_t bytes = list.entries.size() * sizeof(nfsu::TrackDrawListEntry);
    check_eq(list.entries.size(), 6, "four placed instances draw six groups");
    check_eq(nfsu::track_scene_frame_ceiling(track.scene), bytes,
             "the frame ceiling is the full list");
    for (const std::uint64_t capacity : {bytes, bytes - 1U}) {
        nfsu::ArenaConfig config;
        config.category = nfsu::MemoryCategory::transient;
        config.capacity = capacity;
        nfsu::MemoryArena frame;
        const bool gpu_ok_66 = frame.create(config, error);
        check(gpu_ok_66, "frame arena: " + error);
        nfsu::ArenaAllocation out;
        const nfsu::ArenaRefusal got =
            frame.allocate(bytes, alignof(nfsu::TrackDrawListEntry), out);
        check(got == (capacity == bytes ? nfsu::ArenaRefusal::none
                                        : nfsu::ArenaRefusal::over_budget),
              "a frame of " + std::to_string(capacity) + " bytes: " +
                  nfsu::arena_refusal_name(got));
    }
}


void test_draw_commands_match_cpu() {
    namespace sm = nfsu::shader_manifest;
    std::string error;
    nfsu::RenderOptions options;
    options.width = 48;
    options.height = 32;
    options.flip_v = true;
    options.vertex_color = true;
    options.alpha_cutoff = 17;
    options.background[3] = 19;
    options.coefficients.alpha_min = 0.3;
    options.coefficients.alpha_range = 0.6;
    options.coefficients.environment_min[0] = 0.25;
    options.coefficients.environment_color[0] = 64;
    options.coefficients.environment_power = 3;
    // Every decoder outcome, including nonstandard alpha/blend inputs, with
    // fallback and generated-skin scenes and all cull modes.
    for (bool skin : {false, true}) {
        auto fixture = car_fixture(skin);
        for (auto& texture : fixture.scene.textures) {
            texture.layer_group = 1;
            texture.layer_order = 3;
        }
        if (!fixture.scene.textures.empty()) fixture.scene.textures.back().layer_order = 0;
        for (auto mode : {nfsu::MaterialStateMode::off, nfsu::MaterialStateMode::retail}) {
            options.material_state = mode;
            for (std::uint8_t alpha : {0, 1, 2, 3, 255}) {
                for (std::uint8_t blend : {0, 1, 2, 3, 4, 255}) {
                    for (auto cull : {nfsu::CullMode::none, nfsu::CullMode::back, nfsu::CullMode::front}) {
                        options.cull = cull;
                        for (auto& texture : fixture.scene.textures) {
                            texture.alpha_test_mode = alpha;
                            texture.alpha_blend_mode = blend;
                        }
                        nfsu::RecordedCommands commands;
                        if (!nfsu::record_car_commands(fixture.scene, options, {}, kSkinSource, commands, error)) {
                            check(false, "record car: " + error); return;
                        }
                        nfsu::Frame frame;
                        nfsu::RenderStats stats;
                        std::vector<nfsu::ReferenceDraw> trace;
                        if (!nfsu::render_scene(fixture.scene, options, {}, frame, stats, error,
                            [&](const auto& draw) { trace.push_back(draw); })) {
                            check(false, "CPU car: " + error); return;
                        }
                        const auto mismatch = compare_draws(commands, trace, false);
                        check(mismatch.empty(), "car sequence: " + mismatch);
                        check_eq(commands.owning_draws().size(), 3, "car nonempty groups");
                        check_eq(commands.frame.options[0], 1, "flip v uniform");
                        check_eq(commands.frame.options[1], 1, "vertex colour uniform");
                        check_eq(commands.frame.options[3], 17, "alpha cutoff uniform");
                        check_eq(commands.width, 48, "viewport width");
                        check_eq(commands.height, 32, "viewport height");
                        check_eq(commands.clear_color[3], 19, "clear alpha preserves background alpha");
                        const auto rgba = frame.rgba();
                        check(!rgba.empty() && rgba[3] == commands.clear_color[3], "CPU and recorded clear alpha agree");
                        for (const auto& d : commands.owning_draws()) {
                            const auto& source = fixture.scene.draws[d.group];
                            const bool resolved = source.binding == nfsu::BindingKind::resolved;
                            const auto gates = nfsu::material_coefficient_gates(source.flags, true);
                            const float expected = mode == nfsu::MaterialStateMode::retail && resolved
                                ? (gates.diffuse ? 0.3F : 0.0F) : 1.0F;
                            check(d.material.alpha[0] == expected, d.name + ": coefficient alpha");
                            check(d.placement.rows[0] == 1 && d.placement.rows[15] == 1,
                                  d.name + ": authored identity placement");
                        }
                        // Prove the comparator reports the specific draw and field.
                        if (!commands.owning_draws().empty() && alpha == 0 && blend == 0) {
                            const auto name = commands.owning_draws()[0].name;
                            commands.owning_draws()[0].pipeline.depth_write = false;
                            check(compare_draws(commands, trace, false) == name + ": depth write",
                                  "differential comparator catches a named depth-write mutation");
                            commands.owning_draws()[0].pipeline.depth_write = true;
                            commands.owning_draws()[0].pipeline.alpha_test = sm::CompareOp::never;
                            check(compare_draws(commands, trace, false) == name + ": alpha test",
                                  "differential comparator catches a named alpha mutation");
                        }
                    }
                }
            }
        }
    }
}

void test_draw_commands_resources_and_refusals() {
    auto fixture = car_fixture(true);
    nfsu::RecordedCommands commands;
    std::string error;
    check(nfsu::record_car_commands(fixture.scene, {}, {}, kSkinSource, commands, error), "record ledger car");
    const auto expected = table_of(fixture.scene);
    check_eq(commands.owning_uploads().resources.size(), expected.resources.size(), "same upload table");
    for (std::size_t i = 0; i < commands.owning_uploads().resources.size(); ++i) {
        check(commands.owning_uploads().resources[i].data == expected.resources[i].data, "borrowed scene bytes");
    }
    Rig rig;
    make_rig(rig);
    std::vector<nfsu::UploadHandle> handles;
    check(rig.ledger.upload_table(commands.owning_uploads(), 4, handles, error), "recorded uploads enter existing ledger");
    placements(rig.ledger, handles, commands.owning_uploads(), "recorded resources");
    check(rig.ledger.release_all(handles, 8, error), "release recorded resources");
    rig.ledger.retire(8);
    check_eq(rig.buffers.stats().used_bytes, 0, "recorded buffers retired");
    auto arena = make_arena(4096, 256);
    nfsu::ArenaAllocation allocation;
    check(nfsu::reserve_draw_uniforms(commands, arena, allocation, error), "uniform arena placement");
    check_eq(allocation.size, 7 * 256, "one frame and two blocks per draw");
    check_eq(allocation.offset % 256, 0, "uniform alignment");
    check(arena.release(allocation.handle) == nfsu::ArenaRefusal::none, "release uniform placement");
    auto small = make_arena(256, 256);
    const auto previous = allocation.handle.generation;
    check(!nfsu::reserve_draw_uniforms(commands, small, allocation, error), "uniform budget refusal");
    check_eq(small.stats().used_bytes, 0, "uniform refusal leaves arena unchanged");
    check_eq(allocation.handle.generation, previous, "uniform refusal leaves output unchanged");
    auto limits = nfsu::RenderLimits{};
    limits.max_submitted_triangles = 0;
    check(!nfsu::record_car_commands(fixture.scene, {}, limits, kSkinSource, commands, error), "triangle budget refusal");
    check(commands.owning_draws().empty() && commands.owning_uploads().resources.empty(), "refusal clears prior commands and resources");
    auto options = nfsu::RenderOptions{};
    options.width = 0;
    check(!nfsu::record_car_commands(fixture.scene, options, {}, kSkinSource, commands, error), "zero viewport refused");
    fixture.scene.draws[0].mesh = nfsu::kNoHandle;
    check(!nfsu::record_car_commands(fixture.scene, {}, {}, kSkinSource, commands, error), "invalid scene refused");
    fixture = car_fixture(true);
    fixture.scene.meshes[0].triangles[0].a = 65535;
    check(!nfsu::record_car_commands(fixture.scene, {}, {}, kSkinSource, commands, error), "bad GPU vertex index refused");
    check_has(error, "car group 0", "bad index names draw");
}

void test_track_commands_match_cpu() {
    for (bool textured : {false, true}) {
        auto fixture = track_fixture();
        std::string error;
        if (!textured) {
            check(nfsu::build_track_scene(fixture.geometry, fixture.objects, {}, {}, fixture.scene, error),
                  "prepare fallback track");
        }
        std::vector<nfsu::TrackVisibleInstance> visible;
        for (std::size_t i : {4U, 0U, 3U, 1U, 2U}) {
            nfsu::TrackVisibleInstance v;
            v.section_index = 0; v.instance_index = i; visible.push_back(v);
        }
        for (auto order : {nfsu::TrackDrawOrder::visible_instance, nfsu::TrackDrawOrder::material_group}) {
            nfsu::TrackDrawList list;
            nfsu::TrackDrawListOptions selection;
            selection.order = order;
            check(nfsu::build_track_draw_list(fixture.scene, visible, selection, list, error), "build ordered track list");
            nfsu::RenderOptions options;
            options.width = 48; options.height = 32; options.cull = nfsu::CullMode::front;
            nfsu::RecordedCommands commands;
            if (!nfsu::record_track_commands(fixture.scene, list, options, {}, commands, error)) {
                check(false, "record track: " + error); return;
            }
            nfsu::Frame frame;
            nfsu::RenderStats stats;
            nfsu::TrackRenderStats track;
            std::vector<nfsu::ReferenceDraw> trace;
            if (!nfsu::render_track_draw_list(fixture.scene, list, options, {}, frame, stats, track, error,
                [&](const auto& d) { trace.push_back(d); })) {
                check(false, "CPU track: " + error); return;
            }
            const auto mismatch = compare_draws(commands, trace, true);
            check(mismatch.empty(), "track sequence: " + mismatch);
            check_eq(commands.owning_draws().size(), 6, "six track instance groups");
            for (const auto& d : commands.owning_draws()) {
                const auto& t = fixture.scene.instances[d.instance].transform;
                for (std::size_t i = 0; i < 3; ++i) {
                    for (std::size_t j = 0; j < 3; ++j)
                        check(d.placement.rows[i*4+j] == t.linear[i*3+j], d.name + ": track linear transform");
                    check(d.placement.rows[12+i] == t.translation[i], d.name + ": track translation");
                }
            }
            options.material_state = nfsu::MaterialStateMode::retail;
            check(nfsu::record_track_commands(fixture.scene, list, options, {}, commands, error), "track retail recorded: " + error);
            check_eq(commands.owning_draws().size(), 6, "retail records six track instance groups");
            options.material_state = nfsu::MaterialStateMode::off;
            list.entries[0].instance = 0xFFFFFFFFU;
            check(!nfsu::record_track_commands(fixture.scene, list, options, {}, commands, error), "invalid track list refused");
            check(commands.owning_draws().empty() && commands.owning_uploads().resources.empty(), "track refusal is atomic");
        }
    }
}

void test_track_wet_receiver_mask() {
    auto fixture = track_fixture();
    fixture.scene.region.meshes[0].vertex_format_flags = 0x40;
    fixture.scene.region.meshes[1].vertex_format_flags = 0x80;
    fixture.scene.region.meshes[2].vertex_format_flags = 0x1;
    std::vector<nfsu::TrackVisibleInstance> visible;
    for (std::size_t i = 0; i < 5; ++i) {
        nfsu::TrackVisibleInstance v;
        v.section_index = 0;
        v.instance_index = i;
        visible.push_back(v);
    }
    std::string error;
    nfsu::TrackDrawList list;
    if (!nfsu::build_track_draw_list(fixture.scene, visible, {}, list, error)) {
        check(false, "build wet receiver list: " + error);
        return;
    }
    nfsu::RenderOptions options;
    options.wet_road_reflections = true;
    nfsu::RecordedCommands commands;
    if (!nfsu::record_track_commands(fixture.scene, list, options, {}, commands, error)) {
        check(false, "record wet receivers: " + error);
        return;
    }
    bool seen[3]{};
    for (const auto& draw : commands.owning_draws()) {
        if (draw.mesh >= 3) continue;
        seen[draw.mesh] = true;
        check(draw.material.environment_min[3] == (draw.mesh == 0 ? 1.0F : 0.0F),
              draw.name + ": only 0x40 track surfaces receive Switch wet reflections");
    }
    check(seen[0] && seen[1] && seen[2],
          "the wet receiver fixture submits 0x40, 0x80 and 0x1 surfaces");
}

void test_track_material_states_match_cpu() {
    const std::pair<std::uint8_t, std::uint8_t> states[] = {
        {0, 0}, {1, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 2}};
    for (const auto& [alpha, blend] : states) {
        auto fixture = track_fixture();
        for (auto& texture : fixture.scene.region.textures) {
            texture.alpha_test_mode = alpha;
            texture.alpha_blend_mode = blend;
        }
        fixture.scene.region.textures[0].layer_order = 5;
        fixture.scene.region.textures[1].layer_order = 0;
        std::vector<nfsu::TrackVisibleInstance> visible;
        for (std::size_t i : {4U, 0U, 3U, 1U, 2U}) {
            nfsu::TrackVisibleInstance v;
            v.section_index = 0;
            v.instance_index = i;
            visible.push_back(v);
        }
        nfsu::TrackDrawListOptions selection;
        selection.order = nfsu::TrackDrawOrder::retail_bucket;
        nfsu::TrackDrawList list;
        std::string error;
        if (!nfsu::build_track_draw_list(fixture.scene, visible, selection, list, error)) {
            check(false, "build retail bucket: " + error);
            return;
        }
        check(!list.entries.empty() &&
              fixture.scene.region.draws[list.entries.front().draw].texture == 1,
              "retail bucket submits row zero before row five");
        nfsu::RenderOptions options;
        options.width = 48;
        options.height = 32;
        options.material_state = nfsu::MaterialStateMode::retail;
        nfsu::RecordedCommands commands;
        if (!nfsu::record_track_commands(fixture.scene, list, options, {}, commands, error)) {
            check(false, "record track material state: " + error);
            return;
        }
        nfsu::Frame frame;
        nfsu::RenderStats stats;
        nfsu::TrackRenderStats track;
        std::vector<nfsu::ReferenceDraw> trace;
        if (!nfsu::render_track_draw_list(fixture.scene, list, options, {},
                                          frame, stats, track, error,
                                          [&](const auto& draw) { trace.push_back(draw); })) {
            check(false, "render track material state: " + error);
            return;
        }
        const auto mismatch = compare_draws(commands, trace, true);
        check(mismatch.empty(), "track retail material CPU/GPU: " + mismatch);
        check_eq(stats.groups_alpha_tested, alpha ? 6 : 0,
                 "track retail alpha-test draw count");
        check_eq(stats.groups_blended, blend ? 6 : 0,
                 "track retail blend draw count");
        for (const auto& draw : commands.owning_draws()) {
            check(draw.pipeline.depth_write == (blend == 0),
                  "track world blend depth-write state");
            check(draw.material.alpha[0] == 1.0F,
                  "track world neutral diffuse alpha coefficient");
        }
    }
}


void test_car_instances_and_uniform_layout() {
    auto fixture = car_fixture(true);
    auto& scene = fixture.scene;
    scene.profile = nfsu::SelectionProfile::stock_car;
    for (std::size_t mesh : {1U, 0U, 1U, 0U}) {
        nfsu::PreparedInstance instance;
        instance.mesh = mesh;
        instance.lamp_mode = static_cast<std::uint8_t>(scene.instances.size());
        instance.transform[0] = -1;
        instance.transform[12] = static_cast<float>(scene.instances.size());
        instance.transform[13] = 2;
        instance.transform[14] = 3;
        scene.instances.push_back(instance);
    }
    nfsu::RenderOptions options;
    options.width = 32; options.height = 24;
    options.material_state = nfsu::MaterialStateMode::retail;
    // The last material sorts ahead of the first, so an unchanged source
    // order cannot accidentally pass the retail-order comparison.
    for (auto& t : scene.textures) { t.layer_group = 1; t.layer_order = 3; }
    scene.textures.back().layer_order = 0;
    nfsu::RecordedCommands commands;
    std::string error;
    if (!nfsu::record_car_commands(scene, options, {}, kSkinSource, commands, error)) {
        check(false, "instanced car: " + error); return;
    }
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    std::vector<nfsu::ReferenceDraw> trace;
    check(nfsu::render_scene(scene, options, {}, frame, stats, error,
        [&](const auto& d) { trace.push_back(d); }), "instanced CPU render");
    const auto mismatch = compare_draws(commands, trace, false);
    check(mismatch.empty(), "instanced sequence: " + mismatch);
    check_eq(commands.owning_draws().size(), 6, "three groups each have two copies");
    check(commands.owning_draws().front().group != 0, "retail fixture actually changes order");
    for (const auto& d : commands.owning_draws()) {
        for (std::size_t i = 0; i < 16; ++i)
            check(d.placement.rows[i] == scene.instances[d.instance].transform[i], d.name + ": car placement");
        check(d.material.alpha[3] == static_cast<float>(scene.instances[d.instance].lamp_mode),
              d.name + ": lamp mode uniform");
    }
    nfsu::CameraFrame camera;
    check(nfsu::make_camera_frame(options.camera, options.width, options.height, camera, error), "camera frame");
    check(commands.frame.eye[0] == static_cast<float>(camera.eye.x), "camera eye uniform");
    check(commands.frame.projection[0] == static_cast<float>(camera.focal_x), "camera projection uniform");
    // Concrete std140 member offsets and sizes, checked against every
    // generated program rather than a second hand-written manifest.
    const std::map<std::string, std::size_t> offsets = {
        {"Frame.eye", offsetof(nfsu::DrawFrameUniform, eye)},
        {"Frame.right", offsetof(nfsu::DrawFrameUniform, right)},
        {"Frame.up", offsetof(nfsu::DrawFrameUniform, up)},
        {"Frame.forward", offsetof(nfsu::DrawFrameUniform, forward)},
        {"Frame.projection", offsetof(nfsu::DrawFrameUniform, projection)},
        {"Frame.light", offsetof(nfsu::DrawFrameUniform, light)},
        {"Frame.options", offsetof(nfsu::DrawFrameUniform, options)},
        {"Instance.rows", offsetof(nfsu::DrawInstanceUniform, rows)},
        {"Material.fallback", offsetof(nfsu::DrawMaterialUniform, fallback)},
        {"Material.alpha", offsetof(nfsu::DrawMaterialUniform, alpha)},
        {"Material.environment_min", offsetof(nfsu::DrawMaterialUniform, environment_min)},
        {"Material.environment_range", offsetof(nfsu::DrawMaterialUniform, environment_range)},
        {"Material.environment_color", offsetof(nfsu::DrawMaterialUniform, environment_color)},
        {"Material.surface_diffuse_min", offsetof(nfsu::DrawMaterialUniform, surface)+offsetof(nfsu::CarShadingMaterial, diffuse_min)},
        {"Material.surface_diffuse_range", offsetof(nfsu::DrawMaterialUniform, surface)+offsetof(nfsu::CarShadingMaterial, diffuse_range)},
        {"Material.surface_specular_min", offsetof(nfsu::DrawMaterialUniform, surface)+offsetof(nfsu::CarShadingMaterial, specular_min)},
        {"Material.surface_specular_range", offsetof(nfsu::DrawMaterialUniform, surface)+offsetof(nfsu::CarShadingMaterial, specular_range)},
        {"Material.surface_environment_min", offsetof(nfsu::DrawMaterialUniform, surface)+offsetof(nfsu::CarShadingMaterial, environment_min)},
        {"Material.surface_environment_range", offsetof(nfsu::DrawMaterialUniform, surface)+offsetof(nfsu::CarShadingMaterial, environment_range)},
        {"Material.surface_gates", offsetof(nfsu::DrawMaterialUniform, surface)+offsetof(nfsu::CarShadingMaterial, gates)}};
    for (const auto& program : nfsu::shader_manifest::kPrograms) {
        const bool shaded=std::string(program.name)=="car_shaded_textured";
        check_eq(program.block_count, 3, "three uniform blocks");
        for (std::size_t i = 0; i < program.block_count; ++i) {
            const auto& b = program.blocks[i];
            const std::string name = b.name;
            const auto size = name == "Frame" ? sizeof(nfsu::DrawFrameUniform) :
                name == "Instance" ? sizeof(nfsu::DrawInstanceUniform) :
                shaded ? sizeof(nfsu::DrawMaterialUniform) : offsetof(nfsu::DrawMaterialUniform,surface);
            check_eq(b.size, size, name + ": std140 size");
            check_eq(b.binding, name == "Frame" ? 0 : name == "Instance" ? 1 : 2, name + ": uniform binding");
        }
        check_eq(program.member_count, shaded ? offsets.size() : offsets.size()-7, "all uniform members checked");
        for (std::size_t i = 0; i < program.member_count; ++i) {
            const auto& m = program.members[i];
            const auto key = std::string(m.block) + "." + m.name;
            const auto found = offsets.find(key);
            check(found != offsets.end(), key + ": known uniform member");
            if (found != offsets.end()) check_eq(m.offset, found->second, key + ": std140 offset");
        }
    }
}


void test_command_material_gates_and_empty_ranges() {
    auto fixture = car_fixture(true);
    nfsu::RenderOptions options;
    options.material_state = nfsu::MaterialStateMode::retail;
    options.coefficients.alpha_min = 0.25;
    options.coefficients.alpha_range = 0.5;
    options.coefficients.environment_min[0] = 0.75;
    options.coefficients.environment_range[1] = 0.125;
    options.coefficients.environment_color[2] = 128;
    options.coefficients.environment_power = 4;
    std::string error;
    nfsu::RecordedCommands commands;
    for (bool enabled : {false, true}) {
        options.coefficients.environment_enabled = enabled;
        for (std::uint32_t flags : {0U, 0x80U, 0x100U, 0x180U}) {
            for (auto& d : fixture.scene.draws) {
                d.flags = flags; store_u32(d.raw.data() + 56, flags);
            }
            if (!nfsu::record_car_commands(fixture.scene, options, {}, kSkinSource, commands, error)) {
                check(false, "material gates: " + error); return;
            }
            const bool zero = enabled && flags == 0x100U;
            const bool environment = enabled && flags == 0x180U;
            for (const auto& d : commands.owning_draws()) {
                check(d.material.alpha[0] == (zero ? 0.0F : 0.25F), d.name + ": diffuse alpha min gate");
                check(d.material.alpha[1] == (zero ? 0.0F : 0.5F), d.name + ": diffuse alpha range gate");
                check(d.material.alpha[2] == (zero ? 1.0F : 0.0F), d.name + ": diffuse colour gate");
                check(d.material.environment_min[0] == (environment ? 0.75F : 0.0F), d.name + ": environment min gate");
                check(d.material.environment_range[1] == (environment ? 0.125F : 0.0F), d.name + ": environment range gate");
                check(d.material.environment_color[2] == (environment ? 128.0F : 0.0F), d.name + ": environment colour gate");
                check(d.material.environment_color[3] == (environment ? 4.0F : 1.0F), d.name + ": environment power");
                unsigned char color[4];
                nfsu::fallback_color(fixture.scene.draws[d.group].texture_hash, color);
                for (std::size_t i = 0; i < 3; ++i)
                    check(d.material.fallback[i] == color[i], d.name + ": fallback colour");
                check(d.material.fallback[3] == 1, d.name + ": fallback checker");
            }
        }
    }
    // A valid zero range contributes no GPU call or CPU submission.
    fixture.scene.draws[0].index_count = 0;
    store_u32(fixture.scene.draws[0].raw.data() + 12, 0);
    options.width = 32; options.height = 24;
    if (!nfsu::record_car_commands(fixture.scene, options, {}, kSkinSource, commands, error)) {
        check(false, "empty range: " + error); return;
    }
    nfsu::Frame frame;
    nfsu::RenderStats stats;
    std::vector<nfsu::ReferenceDraw> trace;
    check(nfsu::render_scene(fixture.scene, options, {}, frame, stats, error,
        [&](const auto& d) { trace.push_back(d); }), "CPU empty range");
    const auto mismatch = compare_draws(commands, trace, false);
    check(mismatch.empty(), "empty range sequence: " + mismatch);
    check_eq(commands.owning_draws().size(), 2, "zero range emits no command");
    // This group still has a valid nonempty range, but is explicitly not
    // drawable. Both submission loops skip it independently of zero ranges.
    auto& invalid = fixture.geometry.meshes[0].materials[1];
    invalid.texture_slot = 99;
    store_u32(invalid.raw + 28, 99);
    check(nfsu::prepare_car_scene(fixture.geometry, fixture.textures, {}, {}, fixture.scene, error),
          "prepare invalid-slot fixture: " + error);
    check(!fixture.scene.draws[1].drawable && fixture.scene.draws[1].index_count != 0,
          "invalid-slot fixture holds a non-drawable nonempty group");
    trace.clear();
    check(nfsu::record_car_commands(fixture.scene, options, {}, kSkinSource, commands, error),
          "non-drawable group record: " + error);
    check(nfsu::render_scene(fixture.scene, options, {}, frame, stats, error,
        [&](const auto& d) { trace.push_back(d); }), "CPU non-drawable group");
    check(compare_draws(commands, trace, false).empty(), "non-drawable group sequence");
    check_eq(commands.owning_draws().size(), 2, "non-drawable nonempty group emits no command");
    options.coefficients.alpha_min = -1;
    check(!nfsu::record_car_commands(fixture.scene, options, {}, kSkinSource, commands, error),
          "invalid retail coefficients refused");
}

void test_car_shading_records_and_rendering() {
    Bytes bytes(168,0);
    put_u32(bytes,0,0x00135200U); put_u32(bytes,4,160); put_u32(bytes,20,0x12345678U);
    auto f=[&](std::size_t off,float value){ store_float(bytes.data()+8+off,value); };
    f(0x30,0.5F); f(0x40,0.75F); f(0x50,1); f(0x54,0.4F);
    f(0x58,4); f(0x5c,0.5F); f(0x6c,0.25F); f(0x7c,2);
    f(0x80,0.8F); f(0x90,0.2F);
    for (std::size_t i=0;i<3;++i) {
        f(0x34+4*i,1); f(0x44+4*i,1); f(0x60+4*i,1); f(0x70+4*i,1);
        f(0x84+4*i,1); f(0x94+4*i,1);
    }
    std::vector<nfsu::CarMaterialRecord> records; std::string error;
    check(nfsu::parse_car_materials(bytes.data(),bytes.size(),records,error),"decode material record: "+error);
    if (records.size()!=1) { check(false,"one decoded material"); return; }
    const auto& c=records[0].values;
    check(c.diffuse_min[0]==0.5F && c.diffuse_range[0]==0.25F,
          "diffuse target subtracts minimum");
    check(c.specular_min[3]==4.4F, "specular power uses traced initial global");
    check(c.environment_min[0]==0.4F && c.environment_range[0]==-0.3F,
          "environment half scale and signed range");
    for (std::size_t size: {std::size_t(7),std::size_t(167)})
        check(!nfsu::parse_car_materials(bytes.data(),size,records,error) && records.empty(),
              "truncated material refuses transactionally");
    auto duplicate=bytes; duplicate.insert(duplicate.end(),bytes.begin(),bytes.end());
    check(!nfsu::parse_car_materials(duplicate.data(),duplicate.size(),records,error),"duplicate material hash refused");
    auto bad=bytes; store_float(bad.data()+8+0x58,std::numeric_limits<float>::quiet_NaN());
    check(!nfsu::parse_car_materials(bad.data(),bad.size(),records,error),"nonfinite material refused");
    auto paint_bytes=bytes;
    put_u32(paint_bytes,20,0x827D95A1U); // REGPAINTBLUE
    auto paint_fixture=car_fixture(true);
    for (auto& d:paint_fixture.scene.draws) {
        d.material_key_hash=0xD6D6080AU; // CARSKIN
        d.flags=0x180U; store_u32(d.raw.data()+56,d.flags);
    }
    check(nfsu::apply_car_materials(paint_fixture.scene,paint_bytes.data(),
                                    paint_bytes.size(),0x827D95A1U,error),
          "join blue CARSKIN to REGPAINTBLUE: "+error);
    check(!paint_fixture.scene.draws.empty() &&
          paint_fixture.scene.draws[0].shading.gates[0]==1.0F &&
          paint_fixture.scene.draws[0].shading.specular_min[3]==4.4F,
          "blue CARSKIN receives REGPAINTBLUE coefficients");
    auto unmatched_fixture=car_fixture(true);
    for (auto& d:unmatched_fixture.scene.draws) d.material_key_hash=0xD6D6080AU;
    auto default_bytes=paint_bytes;
    put_u32(default_bytes,20,0x2C420D64U); // DEFAULT
    check(nfsu::apply_car_materials(unmatched_fixture.scene,default_bytes.data(),
                                   default_bytes.size(),0xD6640DFFU,error),
          "unmatched paint attribute falls back to DEFAULT: "+error);
    check(!unmatched_fixture.scene.draws.empty() &&
          unmatched_fixture.scene.draws[0].shading.gates[0]==1.0F &&
          unmatched_fixture.scene.draws[0].shading.specular_min[3]==4.4F,
          "unmatched CARSKIN receives DEFAULT coefficients");
    auto fixture=car_fixture(true);
    auto& scene=fixture.scene;
    for (auto& d:scene.draws) {
        d.material_key_hash=0x12345678U; d.flags=0x180U; store_u32(d.raw.data()+56,d.flags);
    }
    check(nfsu::apply_car_materials(scene,bytes.data(),bytes.size(),0,error),"join material keys: "+error);
    for (auto& m:scene.meshes) for (auto& v:m.vertices) { v.nx=0; v.ny=1; v.nz=0; }
    nfsu::RenderOptions options; options.width=160; options.height=120;
    nfsu::Frame off,on,turned,off_again; nfsu::RenderStats stats;
    check(nfsu::render_scene(scene,options,{},off,stats,error),"shade-off fixture renders: "+error);
    options.car_shading=true;
    std::vector<nfsu::ReferenceDraw> trace;
    check(nfsu::render_scene(scene,options,{},on,stats,error,
            [&](const nfsu::ReferenceDraw& d){ trace.push_back(d); }),"shaded fixture renders: "+error);
    check(stats.pixels_drawn>0 && on.rgba()!=off.rgba(),"shading affects real covered pixels");
    nfsu::RecordedCommands commands;
    check(nfsu::record_car_commands(scene,options,{},kSkinSource,commands,error),"shaded command recording: "+error);
    check(compare_draws(commands,trace,false).empty(),"shaded CPU/recorder draw interface agrees");
    bool checked=false;
    for (const auto& d:commands.owning_draws()) if (d.texture_resource!=nfsu::kNoHandle) {
        check(std::memcmp(&d.material.surface,&scene.draws[d.group].shading,sizeof d.material.surface)==0,
              "shaded shader gets decoded per-group coefficients"); checked=true;
    }
    check(checked,"fixture exercises a shaded texture");
    for (auto& m:scene.meshes) for (auto& v:m.vertices) { v.nx=1; v.ny=0; }
    check(nfsu::render_scene(scene,options,{},turned,stats,error),"turned normals render: "+error);
    check(turned.rgba()!=on.rgba(),"stored smooth normals affect lighting/reflection");
    options.car_shading=false;
    check(nfsu::render_scene(scene,options,{},off_again,stats,error) && off.rgba()==off_again.rgba(),
          "disabled effect ignores stored normals and preserves legacy pixels");
    scene.draws[0].shading.environment_min[0]=std::numeric_limits<float>::infinity();
    check(!nfsu::render_scene(scene,options,{},off_again,stats,error),"mutated nonfinite coefficient is rejected");
}

struct TestCase {
    const char* name;
    void (*run)();
};

const TestCase kTests[] = {
    {"car_shading_records_and_rendering", test_car_shading_records_and_rendering},
    {"native_track_section_lifetime", test_native_track_section_lifetime},
    {"command_material_gates_and_empty_ranges", test_command_material_gates_and_empty_ranges},
    {"car_instances_and_uniform_layout", test_car_instances_and_uniform_layout},
    {"draw_commands_match_cpu", test_draw_commands_match_cpu},
    {"draw_commands_resources_and_refusals", test_draw_commands_resources_and_refusals},
    {"track_commands_match_cpu", test_track_commands_match_cpu},
    {"track_wet_receiver_mask", test_track_wet_receiver_mask},
    {"track_material_states_match_cpu", test_track_material_states_match_cpu},
    {"gate_accepts_rgba8", test_gate_accepts_rgba8},
    {"gate_refuses_invalid_native_payloads", test_gate_refuses_invalid_native_payloads},
    {"gate_refuses_malformed_rgba8", test_gate_refuses_malformed_rgba8},
    {"gate_names_are_the_parser_names", test_gate_names_are_the_parser_names},
    {"gate_follows_decode_level", test_gate_follows_decode_level},
    {"car_table_is_exact", test_car_table_is_exact},
    {"generated_lifetime_follows_the_binding", test_generated_lifetime_follows_the_binding},
    {"tables_refuse_an_invalid_scene", test_tables_refuse_an_invalid_scene},
    {"track_table_is_the_region_cost", test_track_table_is_the_region_cost},
    {"empty_streams_are_counted_not_listed", test_empty_streams_are_counted_not_listed},
    {"names_hashes_and_sources", test_names_hashes_and_sources},
    {"ledger_fences", test_ledger_fences},
    {"upload_release_upload_returns_to_a_known_state", test_upload_release_upload_returns_to_a_known_state},
    {"leaks_are_reported_by_name", test_leaks_are_reported_by_name},
    {"double_release_is_refused_by_name", test_double_release_is_refused_by_name},
    {"release_all_stops_at_first_refusal", test_release_all_stops_at_first_refusal},
    {"handles_the_ledger_never_issued", test_handles_the_ledger_never_issued},
    {"upload_table_is_all_or_nothing", test_upload_table_is_all_or_nothing},
    {"generated_skin_is_replaced_on_its_own", test_generated_skin_is_replaced_on_its_own},
    {"lost_placements_are_counted", test_lost_placements_are_counted},
    {"ledger_creation_is_checked", test_ledger_creation_is_checked},
    {"create_again_keeps_old_handles_stale", test_create_again_keeps_old_handles_stale},
    {"image_layout_query_is_honoured", test_image_layout_query_is_honoured},
    {"derived_ceilings", test_derived_ceilings},
    {"switch_budget", test_switch_budget},
    {"scene_requirement_places_the_table", test_scene_requirement_places_the_table},
    {"scene_requirement_reads_the_whole_table", test_scene_requirement_reads_the_whole_table},
    {"frame_ceiling_is_the_full_draw_list", test_frame_ceiling_is_the_full_draw_list},
};

} // namespace

// With no argument every test runs, which is how CTest runs this program.
// With one, only the test of that name runs - the name without its "test_"
// prefix - so that a failure can be attributed to the test that made it. A
// name no test has is itself a failure: a misspelt filter must not pass by
// running nothing.
int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    bool ran = false;
    for (const TestCase& test : kTests) {
        if (only.empty() || only == test.name) {
            test.run();
            ran = true;
        }
    }
    if (!ran) {
        std::cerr << "FAIL: no test is named " << only << '\n';
        ++failures;
    }

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all gpu upload tests passed\n";
    return 0;
}
