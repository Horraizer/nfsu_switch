#include "nfsu/track_stream.h"

#include "nfsu/geometry.h"
#include "nfsu/track_budget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void check_eq_u64(std::uint64_t got, std::uint64_t want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

bool has_issue(const std::vector<nfsu::TrackIssue>& issues, std::uint64_t offset,
               const std::string& text) {
    for (const nfsu::TrackIssue& issue : issues) {
        if (issue.offset == offset && issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A fixture name no other process shares, as tests/track_zone_tests.cpp
// builds one since issue #124.
std::string unique_fixture(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return "nfsu_track_stream_" + label + "_" + std::to_string(pid);
}

// The format's own numbers as literals, tied to the header in one place
// (test_constants) so that a mutated constant cannot move a fixture and its
// expectation together.
constexpr std::uint32_t kTableChunk = 0x00034107U;
constexpr std::uint32_t kSizesChunk = 0x00034108U;
constexpr std::size_t kRecord = 56;
constexpr std::uint32_t kZoneContainer = 0x80034150U;
constexpr std::uint32_t kPolygonChunk = 0x00034151U;
constexpr std::uint32_t kZoneChunk = 0x00034152U;
constexpr std::uint32_t kTrackListChunk = 0x00034153U;
constexpr std::uint32_t kExtraListChunk = 0x00034154U;

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

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<std::size_t>(i)] =
            static_cast<unsigned char>((value >> (8 * i)) & 0xFFU);
    }
}

Bytes chunk(std::uint32_t id, const Bytes& payload) {
    Bytes out;
    append_u32(out, id);
    append_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

void append(Bytes& into, const Bytes& more) {
    into.insert(into.end(), more.begin(), more.end());
}

// Writes a chunk tree at absolute file offsets, so every alignment rule of
// docs/TRACK.md, docs/TRACK_OBJECTS.md and the texture pack applies exactly
// as it would in a retail file.
class Writer {
public:
    explicit Writer(std::uint64_t base) : base_(base) {}

    std::uint64_t at() const { return base_ + bytes_.size(); }
    Bytes& bytes() { return bytes_; }
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

    void append_bytes(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append_bytes(payload);
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
// Geometry, scenery and texture pack writers, as the draw-list, track and
// probe suites build them.
// ---------------------------------------------------------------------------

struct MeshSpec {
    std::string name = "MESH";
    std::uint32_t hash = 0x1001U;
    std::uint32_t vertex_count = 3;
    std::uint32_t face_count = 1;
    std::uint32_t vertex_format = 0;
    std::uint32_t texture_hash = 0xAAAA0001U;
};

void write_mesh(Writer& writer, const MeshSpec& spec) {
    writer.begin(nfsu::kMeshContainerId);

    writer.begin(nfsu::kMeshMetadataId);
    writer.pad_to(nfsu::kMeshMetadataAlignment);
    Bytes record(nfsu::kMeshMetadataRecordSize, 0);
    put_u32(record, nfsu::kMeshMetadataHashOffset, spec.hash);
    record[nfsu::kMeshMetadataTextureCountOffset] = 1;
    for (std::size_t i = 0; i < spec.name.size() && i < nfsu::kMeshNameSize; ++i) {
        record[nfsu::kMeshMetadataNameOffset + i] =
            static_cast<unsigned char>(spec.name[i]);
    }
    writer.append_bytes(record);
    writer.end();

    Bytes textures;
    append_u32(textures, spec.texture_hash);
    append_u32(textures, 0U);
    writer.leaf(nfsu::kTextureListId, textures);

    writer.begin(nfsu::kGeometryContainerId);

    writer.begin(nfsu::kGeometryMetadataId);
    writer.pad_to(nfsu::kGeometryMetadataAlignment);
    Bytes metadata(nfsu::kGeometryMetadataSize, 0);
    put_u32(metadata, nfsu::kVertexFormatOffset, spec.vertex_format);
    put_u32(metadata, nfsu::kMaterialCountOffset, 1U);
    put_u32(metadata, nfsu::kFaceCountOffset, spec.face_count);
    put_u32(metadata, nfsu::kVertexCountOffset, spec.vertex_count);
    writer.append_bytes(metadata);
    writer.end();

    writer.begin(nfsu::kVertexBufferId);
    writer.pad_to(nfsu::kVertexDataAlignment);
    Bytes vertices;
    for (std::uint32_t i = 0; i < spec.vertex_count; ++i) {
        append_f32(vertices, static_cast<float>(i));
        append_f32(vertices, static_cast<float>(i) + 0.5F);
        append_f32(vertices, static_cast<float>(i) + 0.25F);
        vertices.push_back(0x40U);
        vertices.push_back(0x50U);
        vertices.push_back(0x60U);
        vertices.push_back(0xFFU);
        append_f32(vertices, static_cast<float>(i) * 0.125F);
        append_f32(vertices, static_cast<float>(i) * 0.0625F);
    }
    writer.append_bytes(vertices);
    writer.end();

    writer.begin(nfsu::kMaterialBufferId);
    writer.pad_to(nfsu::kMaterialDataAlignment);
    Bytes material;
    append_f32(material, -1.0F);
    append_f32(material, -2.0F);
    append_f32(material, -3.0F);
    append_u32(material, spec.face_count * 3U);
    append_f32(material, 1.0F);
    append_f32(material, 2.0F);
    append_f32(material, 3.0F);
    append_u32(material, 0U);
    append_u32(material, 0xFFU);
    material.resize(52, 0);
    append_u32(material, 0U);
    append_u32(material, 0U);
    writer.append_bytes(material);
    writer.end();

    writer.begin(nfsu::kFaceBufferId);
    writer.pad_to(nfsu::kFaceDataAlignment);
    for (std::uint32_t i = 0; i < spec.face_count; ++i) {
        const std::uint32_t count = spec.vertex_count == 0 ? 1 : spec.vertex_count;
        Bytes triangle;
        append_u16(triangle, static_cast<std::uint16_t>((3 * i) % count));
        append_u16(triangle, static_cast<std::uint16_t>((3 * i + 1) % count));
        append_u16(triangle, static_cast<std::uint16_t>((3 * i + 2) % count));
        writer.append_bytes(triangle);
    }
    writer.zero_to(nfsu::kFaceTailAlignment);
    writer.end();

    writer.end();  // 0x80134100
    writer.end();  // 0x80134010
}

void write_geometry(Writer& writer, const std::vector<MeshSpec>& meshes) {
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
    writer.end();
    for (const MeshSpec& mesh : meshes) {
        write_mesh(writer, mesh);
    }
    writer.end();
}

// One scenery section: one object per placed hash, one instance of each,
// all under a single root node. Instance k sits at raw x = 10 * (k + 1).
void write_scenery(Writer& writer, std::uint32_t number,
                   const std::vector<std::uint32_t>& hashes) {
    writer.begin(nfsu::kScenerySectionId);

    writer.begin(nfsu::kSceneryHeaderId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes header(nfsu::kSceneryHeaderRecordSize, 0U);
    put_u32(header, nfsu::kScenerySectionNumberOffset, number);
    writer.append_bytes(header);
    writer.end();

    writer.begin(nfsu::kSceneryInstanceListId);
    writer.pad_to(nfsu::kSceneryRecordAlignment);
    Bytes instances;
    for (std::size_t k = 0; k < hashes.size(); ++k) {
        const std::int16_t centre = static_cast<std::int16_t>(10 * (k + 1));
        append_i16(instances, static_cast<std::int16_t>(centre - 4));
        append_i16(instances, -4);
        append_i16(instances, -4);
        append_i16(instances, static_cast<std::int16_t>(centre + 4));
        append_i16(instances, 4);
        append_i16(instances, 4);
        append_u16(instances, static_cast<std::uint16_t>(k));
        append_u16(instances, static_cast<std::uint16_t>(0x0100U + k));
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
    writer.append_bytes(instances);
    writer.end();

    Bytes objects;
    for (const std::uint32_t hash : hashes) {
        const std::size_t start = objects.size();
        append_u32(objects, hash);
        objects.resize(start + nfsu::kSceneryObjectRadiusOffset, 0U);
        append_f32(objects, 5.0F);
        objects.resize(start + nfsu::kSceneryObjectRecordSize, 0U);
    }
    writer.leaf(nfsu::kSceneryObjectListId, objects);

    Bytes tree;
    append_i16(tree, 0);
    append_i16(tree, -4);
    append_i16(tree, -4);
    append_i16(tree, static_cast<std::int16_t>(10 * (hashes.size() + 1)));
    append_i16(tree, 4);
    append_i16(tree, 4);
    append_u16(tree, static_cast<std::uint16_t>(hashes.size()));
    for (std::size_t slot = 0; slot < nfsu::kSceneryBoundNodeEntries; ++slot) {
        append_i16(tree, slot < hashes.size() ? static_cast<std::int16_t>(slot) : 0);
    }
    writer.leaf(nfsu::kSceneryBoundTreeId, tree);

    writer.end();
}

// A raw (BUN-style) texture pack of 2x2 A8R8G8B8 textures, as
// tests/track_tests.cpp writes one.
void write_pack(Writer& w,
                const std::vector<std::pair<std::uint32_t, unsigned char>>& textures) {
    w.begin(nfsu::kTexturePackRootId);
    w.begin(nfsu::kTextureInfoContainerId);

    const std::string path = "Location1\\Test.tpk";
    Bytes header(nfsu::kTexturePackHeaderSize, 0);
    put_u32(header, 0, 4);
    std::memcpy(&header[4], "TRACK", 5);
    std::memcpy(&header[32], path.data(), path.size());
    put_u32(header, 96, nfsu::bin_hash(path));
    w.leaf(nfsu::kTexturePackHeaderId, header);

    Bytes hash_table;
    for (const auto& texture : textures) {
        append_u32(hash_table, texture.first);
        append_u32(hash_table, 0);
    }
    w.leaf(nfsu::kTextureHashTableId, hash_table);

    constexpr std::uint32_t kBytes = 2U * 2U * 4U;
    Bytes info_table;
    for (std::size_t i = 0; i < textures.size(); ++i) {
        Bytes record(nfsu::kTextureInfoRecordSize, 0);
        const std::string name = "T" + std::to_string(i);
        std::memcpy(&record[nfsu::kTextureNameOffset], name.data(), name.size());
        put_u32(record, 36, textures[i].first);
        put_u32(record, 48, static_cast<std::uint32_t>(i) * 128U);
        put_u32(record, 56, kBytes);
        put_u32(record, 64, kBytes);
        put_u16(record, 68, 2);
        put_u16(record, 70, 2);
        record[72] = 1;
        record[73] = 1;
        record[74] = 0x20;  // A8R8G8B8
        record[78] = 1;     // one level
        info_table.insert(info_table.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureInfoTableId, info_table);

    Bytes format_table;
    for (std::size_t i = 0; i < textures.size(); ++i) {
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
    const std::size_t start = w.bytes().size();
    w.append_bytes(Bytes(textures.size() * 128U, 0));
    for (std::size_t i = 0; i < textures.size(); ++i) {
        std::fill_n(w.bytes().begin() + static_cast<std::ptrdiff_t>(start + i * 128U),
                    kBytes, textures[i].second);
    }
    w.end();  // data chunk
    w.end();  // data container
    w.end();  // root
}

// ---------------------------------------------------------------------------
// The fixture: a stream of eight sections and the TRACKB image that
// describes it.
//
//   record 0  '--'   number    0  file 0                     never requested
//   record 1  'Y0'   number 2500  texture pack (T1, T2)        fixed
//   record 2  'X0'   number 2400  geometry ROAD, LAMP          fixed
//   record 3  'Z0'   number 2600  scenery 2600 + geometry SIGN fixed
//   record 4  'X99'  number 2499  scenery 1011 only            fixed
//   record 5  'Z3'   number 2603  scenery 2603 + geometry CONE extra list
//   record 6  'J3'   number 1003  scenery 1003 + geometry BENCH track byte
//   record 7  'J9'   number 1009  scenery 1009 + geometry TREE zones only
//   record 8  'W1'   number 2301  a 60-byte-stride mesh        stream only
//
// Z0, X99, Z3 and J3 place ROAD and LAMP, which only X0 holds, so a placed
// scene needs several sections at once. Every section starts at a multiple
// of 2048, as every retail section does, and the gaps between them are
// filled with 0xEE, which is not a chunk: a loader that read a byte outside
// a selected record would refuse it.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTrack = 1003;
constexpr std::uint32_t kRoad = 0x2001U;
constexpr std::uint32_t kLamp = 0x2002U;
constexpr std::uint32_t kSign = 0x2003U;
constexpr std::uint32_t kCone = 0x2004U;
constexpr std::uint32_t kBench = 0x2005U;
constexpr std::uint32_t kTree = 0x2006U;
constexpr std::uint32_t kCrowd = 0x2007U;
constexpr std::uint32_t kMissingMesh = 0x2999U;
constexpr std::uint32_t kT1 = 0xAAAA0001U;
constexpr std::uint32_t kT2 = 0xAAAA0002U;
// Only the separate texture file carries it.
constexpr std::uint32_t kT3 = 0xAAAA0003U;
constexpr std::uint64_t kSectionStep = 2048;

struct SectionSpec {
    std::string name;
    std::int16_t number = 0;
    std::uint8_t track_flag = 0;
    std::uint32_t file = 1;
    Bytes bytes;
    std::uint32_t offset = 0;
    std::uint32_t resident = 0;
};

struct Fixture {
    Bytes stream;
    Bytes trackb;
    Bytes texture_file;
    std::vector<SectionSpec> sections;  // in table order, record 0 first
};

MeshSpec mesh(const std::string& name, std::uint32_t hash, std::uint32_t texture,
              std::uint32_t vertices = 3, std::uint32_t faces = 1) {
    MeshSpec spec;
    spec.name = name;
    spec.hash = hash;
    spec.texture_hash = texture;
    spec.vertex_count = vertices;
    spec.face_count = faces;
    return spec;
}

Bytes section_bytes(std::uint64_t offset, int kind) {
    Writer w(offset);
    switch (kind) {
        case 1:  // Y0
            write_pack(w, {{kT1, 0x31}, {kT2, 0x32}});
            break;
        case 2:  // X0
            write_geometry(w, {mesh("ROAD", kRoad, kT1, 6, 2), mesh("LAMP", kLamp, kT2)});
            break;
        case 3:  // Z0
            write_scenery(w, 2600, {kRoad});
            w.zero_to(16);
            write_geometry(w, {mesh("SIGN", kSign, kT3)});
            break;
        case 4:  // X99
            write_scenery(w, 1011, {kLamp});
            break;
        case 5:  // Z3
            write_scenery(w, 2603, {kCone, kLamp});
            w.zero_to(16);
            write_geometry(w, {mesh("CONE", kCone, kT2)});
            break;
        case 6:  // J3
            write_scenery(w, 1003, {kRoad, kLamp, kBench, kMissingMesh});
            w.zero_to(16);
            write_geometry(w, {mesh("BENCH", kBench, kT1)});
            break;
        case 7:  // J9
            write_scenery(w, 1009, {kTree});
            w.zero_to(16);
            write_geometry(w, {mesh("TREE", kTree, kT2)});
            break;
        case 8: {  // W1: a crowd mesh this decoder refuses
            MeshSpec crowd = mesh("CROWD", kCrowd, kT1);
            crowd.vertex_format = 0x00004081U;
            write_geometry(w, {crowd, mesh("POLE", 0x2008U, kT1)});
            break;
        }
        default:
            break;
    }
    // A padding chunk, as retail sections end, so the next one can start at
    // its own boundary with gap bytes in between.
    w.leaf(0x00000000U, Bytes(8, 0));
    return w.bytes();
}

Bytes stream_record(const SectionSpec& spec) {
    Bytes record(kRecord, 0);
    for (std::size_t i = 0; i < spec.name.size(); ++i) {
        record[i] = static_cast<unsigned char>(spec.name[i]);
    }
    put_u16(record, 8, static_cast<std::uint16_t>(spec.number));
    record[10] = spec.track_flag;
    put_u32(record, 16, spec.file);
    put_u32(record, 20, spec.offset);
    put_u32(record, 24, static_cast<std::uint32_t>(spec.bytes.size()));
    put_u32(record, 28, spec.resident);
    put_u32(record, 32, 0x12345678U);
    return record;
}

Bytes polygon_record(std::int16_t key, float x0, float y0, float x1, float y1) {
    Bytes out;
    append_u32(out, 11);
    append_u32(out, 11);
    append_i16(out, key);
    append_i16(out, 4);
    const float corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (std::size_t v = 0; v < 16; ++v) {
        append_f32(out, v < 4 ? corners[v][0] : 0.0F);
        append_f32(out, v < 4 ? corners[v][1] : 0.0F);
    }
    return out;
}

Bytes zone_record(std::int16_t key, const std::vector<std::int16_t>& sections) {
    Bytes out;
    append_u32(out, 11);
    append_u32(out, 11);
    append_i16(out, key);
    append_i16(out, 0);
    append_u32(out, 0);
    append_u32(out, static_cast<std::uint32_t>(sections.size()));
    for (std::size_t s = 0; s < 64; ++s) {
        append_i16(out, s < sections.size() ? sections[s] : 0);
    }
    return out;
}

// `capacity_tail` is written into the slot just past the count: a reader
// that walked the record's capacity rather than its count would pick it up.
Bytes list_record(std::size_t size, std::uint32_t key,
                  const std::vector<std::int16_t>& sections,
                  std::int16_t capacity_tail) {
    Bytes out;
    append_u32(out, 11);
    append_u32(out, 11);
    append_u32(out, key);
    append_u32(out, static_cast<std::uint32_t>(sections.size()));
    const std::size_t capacity = (size - 16) / 2;
    for (std::size_t s = 0; s < capacity; ++s) {
        std::int16_t value = 0;
        if (s < sections.size()) {
            value = sections[s];
        } else if (s == sections.size()) {
            value = capacity_tail;
        }
        append_i16(out, value);
    }
    return out;
}

Bytes zone_container() {
    Bytes polygons;
    append(polygons, polygon_record(1003, 0.0F, 0.0F, 100.0F, 100.0F));
    append(polygons, polygon_record(1009, 100.0F, 0.0F, 200.0F, 100.0F));
    Bytes zones;
    // Zone 1003 is the track's own: it names 1003 and 1011, which X99
    // carries as scenery with no record of its own, and 110, which nothing
    // holds. Zone 1009 is a neighbour's.
    append(zones, zone_record(1003, {1003, 1011, 110}));
    append(zones, zone_record(1009, {1003, 1009}));
    Bytes track_lists;
    append(track_lists, list_record(272, 1004, {1009}, 0));
    append(track_lists, list_record(272, kTrack, {1003}, 1009));
    Bytes extra_lists;
    // The first record keyed on the track wins; the second is never read.
    append(extra_lists, list_record(32, 1004, {2604}, 0));
    append(extra_lists, list_record(32, kTrack, {2603}, 2301));
    append(extra_lists, list_record(32, kTrack, {1009}, 0));
    Bytes children;
    append(children, chunk(kPolygonChunk, polygons));
    append(children, chunk(kZoneChunk, zones));
    append(children, chunk(kTrackListChunk, track_lists));
    append(children, chunk(kExtraListChunk, extra_lists));
    return chunk(kZoneContainer, children);
}

Fixture make_fixture() {
    Fixture fixture;
    const char* names[] = {"--", "Y0", "X0", "Z0", "X99", "Z3", "J3", "J9", "W1"};
    const std::int16_t numbers[] = {0, 2500, 2400, 2600, 2499, 2603, 1003, 1009, 2301};
    for (int k = 0; k < 9; ++k) {
        SectionSpec spec;
        spec.name = names[k];
        spec.number = numbers[k];
        spec.track_flag = k == 6 ? 1 : 0;
        if (k == 0) {
            spec.file = 0;
            spec.bytes = Bytes(64, 0x5A);
            spec.offset = 0;
            spec.resident = 64;
            fixture.sections.push_back(spec);
            continue;
        }
        const std::uint64_t offset = fixture.stream.size();
        spec.offset = static_cast<std::uint32_t>(offset);
        spec.bytes = section_bytes(offset, k);
        // The texture section keeps a fraction of what it reads, as a
        // retail one does once its pixels have gone to video memory.
        spec.resident = k == 1 ? 40U : static_cast<std::uint32_t>(spec.bytes.size());
        fixture.stream.insert(fixture.stream.end(), spec.bytes.begin(), spec.bytes.end());
        while (fixture.stream.size() % kSectionStep != 0) {
            fixture.stream.push_back(0xEEU);
        }
        fixture.sections.push_back(spec);
    }

    Bytes table;
    for (const SectionSpec& spec : fixture.sections) {
        append(table, stream_record(spec));
    }
    Bytes sizes;
    append_u32(sizes, 64);
    append_u32(sizes, static_cast<std::uint32_t>(fixture.stream.size()));
    append_u32(sizes, 0);

    Bytes& trackb = fixture.trackb;
    append(trackb, chunk(0x00000000U, Bytes(24, 0)));
    append(trackb, chunk(kTableChunk, table));
    append(trackb, chunk(kSizesChunk, sizes));
    append(trackb, zone_container());

    Writer texture(0);
    write_pack(texture, {{kT3, 0x33}, {kT1, 0x99}});
    fixture.texture_file = texture.bytes();
    return fixture;
}

// ---------------------------------------------------------------------------
// A second, minimal fixture for the F4 share guarantee (review comment 3689
// on issue #157, mutants 53, 54 and 56 of that review's sweep): two sections
// that carry their own texture pack for the SAME hash, so the candidate
// scene's real decode shadows one of them while an isolated, decode-alone
// share must still charge each one the full cost. It is unrelated to
// make_fixture(), on purpose: touching that shared fixture would re-pin
// every hash in tests/track_stream_path_guards.cmake for a gap this issue
// calls non-blocking.
//
//   record 0  'SA'  number 2500  pack (SHARE) + a mesh naming it    fixed
//   record 1  'SB'  number    1  pack (SHARE) + a mesh naming it too  track byte
//
// SA is one of the four fixed numbers, so it is requested, and its pack
// decoded, before SB's track-flagged request. In the real candidate scene
// SB's claim on SHARE is shadowed by SA's and retains nothing; a section's
// prepared share is supposed to be an upper bound on what it could add on
// its own, so SB's share must still be the full texture even though the
// full-candidate build charges it nothing.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kShareTexture = 0xBBBB0001U;
constexpr std::uint32_t kShareMesh = 0x6001U;
constexpr std::uint32_t kShareMeshB = 0x6002U;
// 32x32 A8R8G8B8: 4096 retained bytes, well past the retail placement slack
// (0x400) and block header (0x58) that a tiny 2x2 texture would be lost
// under: place_pass never admits anything within about 1112 bytes of the
// budget, so a texture has to be bigger than that margin before an
// undercounted share (mutant 56) can admit something whose real bytes then
// read back over the same budget.
constexpr std::uint32_t kShareTextureSide = 32;

// A one-texture pack of a chosen side length, otherwise laid out exactly as
// write_pack() lays out its fixed 2x2 textures.
void write_sized_pack(Writer& w, std::uint32_t hash, std::uint32_t side,
                      unsigned char fill) {
    w.begin(nfsu::kTexturePackRootId);
    w.begin(nfsu::kTextureInfoContainerId);

    const std::string path = "Location1\\Share.tpk";
    Bytes header(nfsu::kTexturePackHeaderSize, 0);
    put_u32(header, 0, 4);
    std::memcpy(&header[4], "TRACK", 5);
    std::memcpy(&header[32], path.data(), path.size());
    put_u32(header, 96, nfsu::bin_hash(path));
    w.leaf(nfsu::kTexturePackHeaderId, header);

    Bytes hash_table;
    append_u32(hash_table, hash);
    append_u32(hash_table, 0);
    w.leaf(nfsu::kTextureHashTableId, hash_table);

    const std::uint32_t bytes = side * side * 4U;
    Bytes info_table(nfsu::kTextureInfoRecordSize, 0);
    std::memcpy(&info_table[nfsu::kTextureNameOffset], "T0", 2);
    put_u32(info_table, 36, hash);
    put_u32(info_table, 48, 0);
    put_u32(info_table, 56, bytes);
    put_u32(info_table, 64, bytes);
    put_u16(info_table, 68, static_cast<std::uint16_t>(side));
    put_u16(info_table, 70, static_cast<std::uint16_t>(side));
    // check_dimensions (src/texture.cpp) requires 1 << log2 == side.
    unsigned char side_log2 = 0;
    while ((1U << side_log2) < side) {
        ++side_log2;
    }
    info_table[72] = side_log2;
    info_table[73] = side_log2;
    info_table[74] = 0x20;  // A8R8G8B8
    info_table[78] = 1;     // one level
    w.leaf(nfsu::kTextureInfoTableId, info_table);

    Bytes format_table(nfsu::kTextureFormatRecordSize, 0);
    put_u32(format_table, 20, 21);  // D3DFMT_A8R8G8B8
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
    w.append_bytes(Bytes(bytes, fill));
    w.end();  // data chunk
    w.end();  // data container
    w.end();  // root
}

Bytes shares_section_bytes(std::uint64_t offset, int kind) {
    Writer w(offset);
    switch (kind) {
        case 1:  // SA: the pack plus a mesh that wants it
            write_sized_pack(w, kShareTexture, kShareTextureSide, 0x41);
            write_geometry(w, {mesh("SHAREA", kShareMesh, kShareTexture, 3, 1)});
            break;
        case 2:
            // SB: the pack plus its own mesh naming it too. Whichever
            // resident set is actually built recomputes "wanted" from only
            // that set's own geometry (main.cpp's build_stream_path_scene),
            // unlike the once-off "shares" pass, which reads it from the
            // whole candidate; SB needs its own claim so that a resident set
            // holding SB alone still decodes SHARE for real.
            write_sized_pack(w, kShareTexture, kShareTextureSide, 0x42);
            write_geometry(w, {mesh("SHAREB", kShareMeshB, kShareTexture, 3, 1)});
            break;
        default:
            break;
    }
    w.leaf(0x00000000U, Bytes(8, 0));
    return w.bytes();
}

Fixture make_shares_fixture() {
    Fixture fixture;
    const char* names[] = {"SA", "SB"};
    const std::int16_t numbers[] = {2500, 1};
    for (int k = 0; k < 2; ++k) {
        SectionSpec spec;
        spec.name = names[k];
        spec.number = numbers[k];
        spec.track_flag = k == 1 ? 1 : 0;
        const std::uint64_t offset = fixture.stream.size();
        spec.offset = static_cast<std::uint32_t>(offset);
        spec.bytes = shares_section_bytes(offset, k + 1);
        spec.resident = static_cast<std::uint32_t>(spec.bytes.size());
        fixture.stream.insert(fixture.stream.end(), spec.bytes.begin(), spec.bytes.end());
        while (fixture.stream.size() % kSectionStep != 0) {
            fixture.stream.push_back(0xEEU);
        }
        fixture.sections.push_back(spec);
    }

    Bytes table;
    for (const SectionSpec& spec : fixture.sections) {
        append(table, stream_record(spec));
    }
    Bytes sizes;
    append_u32(sizes, 64);
    append_u32(sizes, static_cast<std::uint32_t>(fixture.stream.size()));
    append_u32(sizes, 0);

    Bytes& trackb = fixture.trackb;
    append(trackb, chunk(0x00000000U, Bytes(24, 0)));
    append(trackb, chunk(kTableChunk, table));
    append(trackb, chunk(kSizesChunk, sizes));
    // The zone container is unrelated to SA and SB (fixed and track-flag
    // records need no zone at all); make_fixture()'s own container is a
    // proven-valid one, reused as inert filler so parse_track_zones succeeds.
    append(trackb, zone_container());
    return fixture;
}

nfsu::TrackStreamTable parse_table(const Bytes& image) {
    return nfsu::parse_track_stream_table(image.data(), image.size());
}

nfsu::TrackZoneReport parse_zones(const Bytes& image) {
    return nfsu::parse_track_zones(image.data(), image.size(), 0);
}

nfsu::TrackStreamSelection select(const Fixture& fixture, nfsu::TrackStreamSet set,
                                  std::uint32_t track = kTrack) {
    const nfsu::TrackStreamTable table = parse_table(fixture.trackb);
    const nfsu::TrackZoneReport zones = parse_zones(fixture.trackb);
    nfsu::TrackStreamSelection selection;
    std::string error;
    check(nfsu::select_track_stream_sections(table, zones, track, set, selection, error),
          "the fixture selects: " + error);
    return selection;
}

std::string names_of(const nfsu::TrackStreamTable& table,
                     const std::vector<std::size_t>& records) {
    std::string text;
    for (const std::size_t record : records) {
        text += text.empty() ? "" : " ";
        text += record < table.records.size() ? table.records[record].name : "?";
    }
    return text;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_constants() {
    check_eq_u64(nfsu::kTrackStreamTableId, kTableChunk, "table chunk id");
    check_eq_u64(nfsu::kTrackStreamFileSizesId, kSizesChunk, "file size chunk id");
    check_eq_u64(nfsu::kTrackStreamRecordSize, kRecord, "record size");
    check_eq_u64(nfsu::kTrackStreamNumberOffset, 8, "number offset");
    check_eq_u64(nfsu::kTrackStreamTrackFlagOffset, 10, "track byte offset");
    check_eq_u64(nfsu::kTrackStreamRequestedOffset, 11, "requested byte offset");
    check_eq_u64(nfsu::kTrackStreamStateOffset, 12, "state offset");
    check_eq_u64(nfsu::kTrackStreamFileIndexOffset, 16, "file index offset");
    check_eq_u64(nfsu::kTrackStreamDataOffsetOffset, 20, "data offset offset");
    check_eq_u64(nfsu::kTrackStreamSizeOffset, 24, "size offset");
    check_eq_u64(nfsu::kTrackStreamResidentSizeOffset, 28, "resident size offset");
    check_eq_u64(nfsu::kTrackStreamUnknownOffset, 32, "unknown word offset");
    check_eq_u64(nfsu::kTrackStreamRuntimeOffset, 36, "runtime bytes offset");
    check_eq_u64(nfsu::kTrackStreamPoolHeadroom, 0x200000, "pool headroom");
    check_eq_u64(nfsu::kTrackStreamRequestLimit, 256, "request limit");
    check_eq_u64(nfsu::kTrackStreamSectionAlignment, 128, "section alignment");
    const std::int16_t fixed[4] = {2500, 2400, 2600, 2499};
    for (std::size_t i = 0; i < 4; ++i) {
        check_eq_u64(static_cast<std::uint64_t>(nfsu::kTrackStreamFixedSections[i]),
                     static_cast<std::uint64_t>(fixed[i]),
                     "fixed request " + std::to_string(i));
    }
    check(std::string(nfsu::track_stream_set_name(nfsu::TrackStreamSet::permanent)) ==
                  "permanent" &&
              std::string(nfsu::track_stream_set_name(nfsu::TrackStreamSet::zones)) ==
                  "zones" &&
              std::string(nfsu::track_stream_set_name(nfsu::TrackStreamSet::stream)) ==
                  "stream",
          "set names");
    check(std::string(nfsu::track_stream_reason_name(nfsu::TrackStreamReason::fixed)) ==
                  "fixed" &&
              std::string(nfsu::track_stream_reason_name(
                  nfsu::TrackStreamReason::extra_list)) == "extra-list" &&
              std::string(nfsu::track_stream_reason_name(
                  nfsu::TrackStreamReason::track_flag)) == "track-flag" &&
              std::string(nfsu::track_stream_reason_name(
                  nfsu::TrackStreamReason::zone_list)) == "zone-list" &&
              std::string(nfsu::track_stream_reason_name(
                  nfsu::TrackStreamReason::stream_bound)) == "stream-bound",
          "reason names");
}

void test_table_parses() {
    const Fixture fixture = make_fixture();
    const nfsu::TrackStreamTable table = parse_table(fixture.trackb);
    check(table.valid && table.ok(), "the fixture table is valid");
    check_eq_u64(table.records.size(), 9, "records");
    check_eq_u64(table.table_offset, 32 + 8, "table payload offset");
    check_eq_u64(table.table_size, 9 * kRecord, "table payload size");
    check(table.has_file_sizes, "file sizes present");
    check_eq_u64(table.file_sizes[0], 64, "file 0 size");
    check_eq_u64(table.file_sizes[1], fixture.stream.size(), "stream size");
    check_eq_u64(table.file_sizes[2], 0, "pack size");
    check_eq_u64(table.file_sizes_offset, 40 + 9 * kRecord + 8, "file sizes offset");
    if (table.records.size() == 9) {
        for (std::size_t k = 0; k < 9; ++k) {
            const nfsu::TrackStreamRecord& record = table.records[k];
            const SectionSpec& spec = fixture.sections[k];
            check(record.name == spec.name, "record " + std::to_string(k) + " name");
            check_eq_u64(record.index, k, "record index");
            check_eq_u64(record.offset, 40 + k * kRecord, "record offset");
            check(record.number == spec.number, "record number");
            check_eq_u64(record.track_flag, spec.track_flag, "record track byte");
            check_eq_u64(record.file_index, spec.file, "record file index");
            check_eq_u64(record.data_offset, spec.offset, "record data offset");
            check_eq_u64(record.size, spec.bytes.size(), "record size");
            check_eq_u64(record.resident_size, spec.resident, "record resident size");
            check_eq_u64(record.unknown, 0x12345678U, "record unknown word");
        }
    }
    check_eq_u64(nfsu::find_track_stream_record(table, 1003), 6, "find 1003");
    check_eq_u64(nfsu::find_track_stream_record(table, 0), 0, "find 0");
    check(nfsu::find_track_stream_record(table, 1011) == nfsu::kTrackStreamNoRecord,
          "a number with no record");
}

void test_table_refusals() {
    const Fixture fixture = make_fixture();
    const std::size_t payload = 40;  // the first record
    const auto refused = [&](const Bytes& image, const std::string& label,
                             std::uint64_t offset, const std::string& text) {
        const nfsu::TrackStreamTable table = parse_table(image);
        check(!table.valid && has_issue(table.issues, offset, text),
              label + " is refused at " + std::to_string(offset) + " with '" + text +
                  "'");
        return table;
    };
    const auto with_record = [&](std::size_t record, std::size_t at, unsigned char value) {
        Bytes image = fixture.trackb;
        image[payload + record * kRecord + at] = value;
        return image;
    };

    {
        Bytes truncated(fixture.trackb.begin() + 32,
                        fixture.trackb.begin() + 32 + 8 + kRecord - 1);
        refused(truncated, "a truncated table chunk", 0, "remain");
        Bytes image;
        append(image, chunk(0x00000000U, Bytes(8, 0)));
        refused(image, "no table", 0, "expected exactly one chunk 0x00034107, found 0");
        Bytes two = fixture.trackb;
        append(two, chunk(kTableChunk, Bytes(kRecord, 0)));
        refused(two, "two tables", fixture.trackb.size(),
                "expected exactly one chunk 0x00034107, found 2");
        Bytes trailing = fixture.trackb;
        trailing.push_back(0);
        refused(trailing, "a trailing byte", fixture.trackb.size(), "trailing bytes");
    }
    {
        Bytes image;
        append(image, chunk(kTableChunk, Bytes(kRecord + 1, 0)));
        refused(image, "a remainder", 0, "not a non-zero whole number");
        Bytes empty;
        append(empty, chunk(kTableChunk, Bytes{}));
        refused(empty, "an empty table", 0, "not a non-zero whole number");
    }
    {
        Bytes sizes = fixture.trackb;
        append(sizes, chunk(kSizesChunk, Bytes(12, 0)));
        refused(sizes, "two size chunks", fixture.trackb.size(),
                "expected at most one chunk 0x00034108, found 2");
        Bytes image;
        append(image, chunk(kTableChunk, stream_record(fixture.sections[6])));
        append(image, chunk(kSizesChunk, Bytes(16, 0)));
        refused(image, "a size chunk of 16 bytes", 8 + kRecord, "holds 16 bytes, not 12");
        Bytes no_sizes;
        append(no_sizes, chunk(kTableChunk, stream_record(fixture.sections[6])));
        const nfsu::TrackStreamTable plain = parse_table(no_sizes);
        check(plain.valid && !plain.has_file_sizes, "a table with no size chunk is valid");
    }
    // Names.
    refused(with_record(6, 0, 0), "an empty name", payload + 6 * kRecord, "name");
    refused(with_record(6, 1, 0x07), "a control byte in a name", payload + 6 * kRecord,
            "printable");
    refused(with_record(6, 1, 0x7F), "a DEL in a name", payload + 6 * kRecord,
            "printable");
    check(parse_table(with_record(6, 1, 0x7E)).valid, "a tilde is printable");
    {
        Bytes image = fixture.trackb;
        for (std::size_t i = 0; i < 8; ++i) {
            image[payload + 6 * kRecord + i] = 'A';
        }
        refused(image, "a name with no terminator", payload + 6 * kRecord,
                "NUL-terminated");
    }
    refused(with_record(6, 7, 'Q'), "bytes after the terminator", payload + 6 * kRecord,
            "NUL-terminated");
    // Loader-written bytes, each end of each run.
    for (const std::size_t at : {std::size_t{11}, std::size_t{12}, std::size_t{15},
                                 std::size_t{36}, std::size_t{55}}) {
        refused(with_record(6, at, 1), "a loader byte at +" + std::to_string(at),
                payload + 6 * kRecord, "where the loader writes");
    }
    // A file index outside the three names, at the boundary.
    refused(with_record(6, 16, 3), "file index 3", payload + 6 * kRecord + 16,
            "file index 3");
    {
        const nfsu::TrackStreamTable two = parse_table(with_record(6, 16, 2));
        // Index 2 is a real file; the fixture declares it as 0 bytes, so the
        // record is refused for its range instead.
        check(has_issue(two.issues, payload + 6 * kRecord + 20, "reaches byte"),
              "file index 2 is accepted and bounded by its declared size");
    }
    // Resident size one past the byte count, and exactly equal to it.
    {
        Bytes image = fixture.trackb;
        put_u32(image, payload + 6 * kRecord + 28,
                static_cast<std::uint32_t>(fixture.sections[6].bytes.size() + 1));
        refused(image, "a resident size above the size", payload + 6 * kRecord + 28,
                "keeps");
        put_u32(image, payload + 6 * kRecord + 28,
                static_cast<std::uint32_t>(fixture.sections[6].bytes.size()));
        check(parse_table(image).valid, "a resident size equal to the size is kept");
    }
    // The declared file size: one byte short of the last record's end, and
    // exactly at it.
    {
        Bytes image = fixture.trackb;
        const std::size_t sizes = 40 + 9 * kRecord + 8;
        const std::uint32_t end =
            fixture.sections[8].offset +
            static_cast<std::uint32_t>(fixture.sections[8].bytes.size());
        put_u32(image, sizes + 4, end - 1);
        refused(image, "a record past its file", payload + 8 * kRecord + 20,
                "reaches byte");
        put_u32(image, sizes + 4, end);
        check(parse_table(image).valid, "a record ending at its file's size is kept");
    }
    // A repeated number: the second record is refused, the first kept.
    {
        Bytes image = fixture.trackb;
        put_u16(image, payload + 7 * kRecord + 8, 1003);
        const nfsu::TrackStreamTable table =
            refused(image, "a repeated number", payload + 7 * kRecord + 8,
                    "repeats section number 1003");
        check_eq_u64(table.records.size(), 8, "the repeat is dropped, the rest kept");
    }
    // Limits.
    {
        nfsu::TrackStreamLimits limits;
        limits.max_records = 8;
        const nfsu::TrackStreamTable table = nfsu::parse_track_stream_table(
            fixture.trackb.data(), fixture.trackb.size(), limits);
        check(!table.valid && has_issue(table.issues, 32, "9 records, over the limit of 8"),
              "the record limit");
        limits.max_records = 9;
        check(nfsu::parse_track_stream_table(fixture.trackb.data(), fixture.trackb.size(),
                                             limits)
                  .valid,
              "the record limit is inclusive");
        nfsu::TrackStreamLimits image_limit;
        image_limit.max_image_bytes = fixture.trackb.size() - 1;
        check(!nfsu::parse_track_stream_table(fixture.trackb.data(), fixture.trackb.size(),
                                              image_limit)
                   .valid,
              "the image limit");
        image_limit.max_image_bytes = fixture.trackb.size();
        check(nfsu::parse_track_stream_table(fixture.trackb.data(), fixture.trackb.size(),
                                             image_limit)
                  .valid,
              "the image limit is inclusive");
        nfsu::TrackStreamLimits chunk_limit;
        chunk_limit.max_chunks = 3;
        check(!nfsu::parse_track_stream_table(fixture.trackb.data(), fixture.trackb.size(),
                                              chunk_limit)
                   .valid,
              "the chunk limit");
        chunk_limit.max_chunks = 4;
        check(nfsu::parse_track_stream_table(fixture.trackb.data(), fixture.trackb.size(),
                                             chunk_limit)
                  .valid,
              "the chunk limit is inclusive");
    }
}

nfsu::TrackStreamRecord pool_record(std::int16_t number, std::uint8_t flag,
                                    std::uint32_t size, std::uint32_t resident) {
    nfsu::TrackStreamRecord record;
    record.number = number;
    record.track_flag = flag;
    record.size = size;
    record.resident_size = resident;
    record.file_index = 1;
    return record;
}

void test_pool_bytes() {
    nfsu::TrackStreamTable table;
    // Counted: two flagged records and the four fixed numbers whether or
    // not their byte is set. Not counted: an unflagged ordinary record.
    table.records.push_back(pool_record(1001, 1, 100, 100));
    table.records.push_back(pool_record(1002, 1, 300, 200));
    table.records.push_back(pool_record(2500, 0, 5000, 40));
    table.records.push_back(pool_record(2400, 0, 7, 7));
    table.records.push_back(pool_record(2600, 0, 11, 11));
    table.records.push_back(pool_record(2499, 0, 13, 13));
    table.records.push_back(pool_record(1009, 0, 900000, 900000));
    // Kept: 100 + 200 + 40 + 7 + 11 + 13 = 371. The largest record that
    // keeps less than it reads: 2500, 5000 + 40 = 5040.
    check_eq_u64(nfsu::track_stream_pool_bytes(table), 5040 + 0x200000,
                 "the pool is the larger of the two, plus the headroom");
    table.records[2].size = 40;
    check_eq_u64(nfsu::track_stream_pool_bytes(table), 500 + 0x200000,
                 "with no large texture section, 1002's 300 + 200 = 500 beats the"
                 " sum of 371");
    table.records[1].size = 200;
    check_eq_u64(nfsu::track_stream_pool_bytes(table), 371 + 0x200000,
                 "with every counted record keeping what it reads, the sum");
    table.records[6].track_flag = 1;
    check_eq_u64(nfsu::track_stream_pool_bytes(table), 900371 + 0x200000,
                 "a flagged record is counted");
    table.records.clear();
    check_eq_u64(nfsu::track_stream_pool_bytes(table), 0x200000, "an empty table");
}

void test_permanent_selection() {
    const Fixture fixture = make_fixture();
    const nfsu::TrackStreamTable table = parse_table(fixture.trackb);
    const nfsu::TrackStreamSelection selection =
        select(fixture, nfsu::TrackStreamSet::permanent);
    check(names_of(table, selection.records) == "Y0 X0 Z0 X99 Z3 J3",
          "the permanent set in the loader's order: " + names_of(table, selection.records));
    const nfsu::TrackStreamReason reasons[] = {
        nfsu::TrackStreamReason::fixed,      nfsu::TrackStreamReason::fixed,
        nfsu::TrackStreamReason::fixed,      nfsu::TrackStreamReason::fixed,
        nfsu::TrackStreamReason::extra_list, nfsu::TrackStreamReason::track_flag};
    check(selection.reasons.size() == 6 &&
              std::equal(selection.reasons.begin(), selection.reasons.end(), reasons),
          "the reasons of the permanent set");
    // Four fixed, the one entry the 0x00034154 record keyed on 1003 counts
    // (not its 2301 past the count, and not the later record's 1009), and
    // the one flagged record.
    check_eq_u64(selection.requests.size(), 6, "permanent requests");
    check_eq_u64(selection.requests_without_record, 0, "requests without record");
    check_eq_u64(selection.repeated_requests, 0, "repeated requests");
    check(!selection.stopped_at_limit, "no limit reached");
    // An index into section_lists, which holds the two 0x00034153 records
    // first: the second 0x00034154 record is entry 3.
    {
        const nfsu::TrackZoneReport zones = parse_zones(fixture.trackb);
        check(selection.has_extra_list && selection.extra_list == 3 &&
                  zones.section_lists.size() == 5 &&
                  zones.section_lists[3].chunk_id == kExtraListChunk &&
                  zones.section_lists[3].index == 1,
              "the first 0x00034154 record keyed on the track");
    }
    check(selection.requests.size() == 6 && selection.requests[4].number == 2603 &&
              selection.requests[4].record == 5 && selection.requests[4].selected,
          "the extra list requests 2603");
    // The route check: 0x00034153 keyed on 1003 names zone 1003, whose list
    // names 1003 (a record, selected), 1011 (no record) and 110 (none).
    check(selection.has_route_list, "the route list is found");
    check_eq_u64(selection.route_zones, 1, "route zones");
    check_eq_u64(selection.route_zones_missing, 0, "route zones missing");
    check_eq_u64(selection.route_numbers, 3, "route section numbers");
    check_eq_u64(selection.route_numbers_with_record, 1, "route numbers with a record");
    check_eq_u64(selection.route_numbers_outside, 0, "route numbers outside the set");

    // Another track: its own 0x00034154 and 0x00034153 records, and the
    // table's track byte, which is the table's, so it still requests J3.
    const nfsu::TrackStreamSelection other =
        select(fixture, nfsu::TrackStreamSet::permanent, 1004);
    check(other.has_extra_list && other.extra_list == 2, "1004's extra list");
    check_eq_u64(other.requests_without_record, 1, "2604 has no record");
    check(other.has_route_list && other.route_numbers == 2 &&
              other.route_numbers_with_record == 2 && other.route_numbers_outside == 1,
          "1004's route names zone 1009, whose 1009 is outside the set");
    const nfsu::TrackStreamSelection none =
        select(fixture, nfsu::TrackStreamSet::permanent, 1999);
    check(!none.has_extra_list && !none.has_route_list,
          "a track no list is keyed on has neither");
    check(names_of(table, none.records) == "Y0 X0 Z0 X99 J3",
          "and requests the fixed four and the flagged record");

    // A route zone the zone chunk does not hold is counted as missing.
    nfsu::TrackZoneReport zones = parse_zones(fixture.trackb);
    for (nfsu::TrackZone& zone : zones.zones) {
        if (zone.key == 1003) {
            zone.key = 1005;
        }
    }
    nfsu::TrackStreamSelection missing;
    std::string error;
    check(nfsu::select_track_stream_sections(table, zones, kTrack,
                                             nfsu::TrackStreamSet::permanent, missing,
                                             error) &&
              missing.route_zones == 1 && missing.route_zones_missing == 1 &&
              missing.route_numbers == 0,
          "a route zone with no zone record");
}

void test_bound_sets() {
    const Fixture fixture = make_fixture();
    const nfsu::TrackStreamTable table = parse_table(fixture.trackb);
    const nfsu::TrackStreamSelection zones = select(fixture, nfsu::TrackStreamSet::zones);
    check(names_of(table, zones.records) == "Y0 X0 Z0 X99 Z3 J3 J9",
          "the zones set adds what any zone lists: " + names_of(table, zones.records));
    check(zones.reasons.size() == 7 &&
              zones.reasons[6] == nfsu::TrackStreamReason::zone_list,
          "J9 by a zone list");
    // Zone 1003: 1003 again, 1011 and 110 with no record. Zone 1009: 1003
    // again and 1009.
    check_eq_u64(zones.requests.size(), 6 + 5, "zones requests");
    check_eq_u64(zones.repeated_requests, 2, "zones repeats");
    check_eq_u64(zones.requests_without_record, 2, "zones requests without record");

    const nfsu::TrackStreamSelection stream = select(fixture, nfsu::TrackStreamSet::stream);
    check(names_of(table, stream.records) == "Y0 X0 Z0 X99 Z3 J3 J9 W1",
          "the stream set is every stream record: " + names_of(table, stream.records));
    check_eq_u64(stream.excluded_records, 1, "the file index 0 record is excluded");
    check(stream.reasons.size() == 8 &&
              stream.reasons[7] == nfsu::TrackStreamReason::stream_bound,
          "W1 by the bound");

    // Refusals.
    nfsu::TrackStreamSelection out;
    std::string error;
    nfsu::TrackStreamTable bad = table;
    bad.valid = false;
    check(!nfsu::select_track_stream_sections(bad, parse_zones(fixture.trackb), kTrack,
                                              nfsu::TrackStreamSet::permanent, out,
                                              error) &&
              error.find("streaming table") != std::string::npos,
          "an invalid table is refused");
    nfsu::TrackZoneReport no_zones = parse_zones(fixture.trackb);
    no_zones.valid = false;
    check(!nfsu::select_track_stream_sections(table, no_zones, kTrack,
                                              nfsu::TrackStreamSet::permanent, out,
                                              error) &&
              error.find("zone report") != std::string::npos,
          "an invalid zone report is refused");
}

void test_request_limit() {
    // 300 flagged records: the permanent set stops at the 256th, as
    // 0x00444D5B does; the bounds do not.
    nfsu::TrackStreamTable table;
    table.valid = true;
    for (int k = 0; k < 300; ++k) {
        table.records.push_back(
            pool_record(static_cast<std::int16_t>(3000 + k), 1, 10, 10));
    }
    const nfsu::TrackZoneReport zones = parse_zones(make_fixture().trackb);
    nfsu::TrackStreamSelection selection;
    std::string error;
    check(nfsu::select_track_stream_sections(table, zones, 1999,
                                             nfsu::TrackStreamSet::permanent, selection,
                                             error),
          "a large table selects");
    check_eq_u64(selection.records.size(), 256, "stopped at 256");
    check(selection.stopped_at_limit, "and says so");
    // Four fixed requests with no record, then 256 flagged ones.
    check_eq_u64(selection.requests.size(), 260, "no request after the stop");
    check(nfsu::select_track_stream_sections(table, zones, 1999,
                                             nfsu::TrackStreamSet::stream, selection,
                                             error) &&
              selection.records.size() == 300 && !selection.stopped_at_limit,
          "the stream bound is not capped");
    table.records.resize(256);
    check(nfsu::select_track_stream_sections(table, zones, 1999,
                                             nfsu::TrackStreamSet::permanent, selection,
                                             error) &&
              selection.records.size() == 256 && selection.stopped_at_limit,
          "the 256th request is kept and ends the walk");
    table.records.resize(255);
    check(nfsu::select_track_stream_sections(table, zones, 1999,
                                             nfsu::TrackStreamSet::permanent, selection,
                                             error) &&
              selection.records.size() == 255 && !selection.stopped_at_limit,
          "255 requests do not reach the limit");
}

struct Loaded {
    nfsu::TrackStreamTable table;
    nfsu::TrackStreamSelection selection;
    nfsu::TrackStreamLoad load;
};

bool load_fixture(const Fixture& fixture, nfsu::TrackStreamSet set, Loaded& out) {
    out.table = parse_table(fixture.trackb);
    const nfsu::TrackZoneReport zones = parse_zones(fixture.trackb);
    std::string error;
    if (!nfsu::select_track_stream_sections(out.table, zones, kTrack, set, out.selection,
                                            error)) {
        check(false, "the fixture selects: " + error);
        return false;
    }
    const std::vector<unsigned char>* images[nfsu::kTrackStreamFileCount] = {
        nullptr, &fixture.stream, nullptr};
    const bool ok = nfsu::load_track_stream_sections_from_memory(
        out.table, out.selection, images, nfsu::TrackStreamLoadLimits{}, out.load, error);
    check(ok, "the fixture loads: " + error);
    return ok;
}

void test_load_poll_and_cancel() {
    const Fixture fixture = make_fixture();
    Loaded loaded;
    if (!load_fixture(fixture, nfsu::TrackStreamSet::permanent, loaded)) return;
    const std::vector<unsigned char>* images[nfsu::kTrackStreamFileCount] = {
        nullptr, &fixture.stream, &fixture.texture_file};
    std::string error;
    nfsu::TrackStreamLoad out;
    nfsu::TrackStreamLoadLimits limits;
    unsigned polls = 0;
    limits.poll = [&] { ++polls; };
    check(nfsu::load_track_stream_sections_from_memory(loaded.table, loaded.selection,
          images, limits, out, error), "polled parsing succeeds");
    check(polls >= 4 * loaded.selection.records.size(),
          "parsing and merging service the observer repeatedly between sections");
    limits.release_non_texture_bytes = true;
    bool released_before_next = false;
    limits.poll = [&] {
        if (out.sections.size() > 2 && out.sections[1].loaded &&
            out.sections[1].bytes.empty()) released_before_next = true;
    };
    check(nfsu::load_track_stream_sections_from_memory(loaded.table, loaded.selection,
          images, limits, out, error) && out.ok(), "low-memory sections parse and merge");
    check(released_before_next && !out.sections[0].bytes.empty() &&
          out.geometry.cost.vertices == loaded.load.geometry.cost.vertices &&
          out.objects.cost.instances == loaded.load.objects.cost.instances &&
          out.bytes_read == loaded.load.bytes_read,
          "raw geometry released before next section; texture bytes and merge retained");
    struct Cancel {};
    polls = 0;
    limits.poll = [&] { if (++polls == 3) throw Cancel{}; };
    bool cancelled = false;
    try {
        nfsu::load_track_stream_sections_from_memory(loaded.table, loaded.selection,
            images, limits, out, error);
    } catch (const Cancel&) { cancelled = true; }
    check(cancelled && polls == 3, "parsing cancellation propagates immediately before remaining sections");
}

void test_load_and_merge() {
    const Fixture fixture = make_fixture();
    Loaded loaded;
    if (!load_fixture(fixture, nfsu::TrackStreamSet::permanent, loaded)) {
        return;
    }
    const nfsu::TrackStreamLoad& load = loaded.load;
    check(load.ok(), "the permanent load raises no issue");
    check_eq_u64(load.sections.size(), 6, "six sections");
    std::uint64_t bytes = 0;
    for (const std::size_t k : {1, 2, 3, 4, 5, 6}) {
        bytes += fixture.sections[k].bytes.size();
    }
    check_eq_u64(load.bytes_read, bytes, "bytes read are the selected sizes");
    check_eq_u64(load.stream_file_size, fixture.stream.size(), "stream size");
    if (load.sections.size() != 6) {
        return;
    }
    const nfsu::TrackStreamSection& y0 = load.sections[0];
    check(y0.name == "Y0" && y0.loaded && y0.texture_packs == 1 && y0.geometry_roots == 0 &&
              y0.scenery_sections == 0 && y0.padding_chunks == 1 && y0.chunks == 2 &&
              y0.other_chunks == 0,
          "Y0 holds one pack and one padding chunk");
    check(y0.bytes == fixture.sections[1].bytes, "Y0's bytes are its range");
    check_eq_u64(y0.packs.packs.size(), 1, "Y0 pack parsed");
    check(y0.reason == nfsu::TrackStreamReason::fixed && y0.record == 1 &&
              y0.number == 2500 && y0.file_index == 1,
          "Y0's provenance");
    const nfsu::TrackStreamSection& z0 = load.sections[2];
    check(z0.geometry_roots == 1 && z0.scenery_sections == 1 && z0.padding_chunks == 1,
          "Z0 holds scenery and geometry");
    check_eq_u64(z0.offset, fixture.sections[3].offset, "Z0 offset");
    check(z0.resident_size == z0.size && y0.resident_size == 40, "resident sizes");
    check(load.sections[5].reason == nfsu::TrackStreamReason::track_flag &&
              load.sections[4].reason == nfsu::TrackStreamReason::extra_list,
          "the reasons reach the sections");

    // The merge: meshes in selection order, renumbered, offsets absolute.
    const nfsu::TrackRegionReport& geometry = load.geometry;
    check_eq_u64(geometry.meshes.size(), 5, "ROAD LAMP SIGN CONE BENCH");
    const char* order[] = {"ROAD", "LAMP", "SIGN", "CONE", "BENCH"};
    for (std::size_t m = 0; m < geometry.meshes.size() && m < 5; ++m) {
        check(geometry.meshes[m].name == order[m], "merged mesh " + std::to_string(m));
        check_eq_u64(geometry.meshes[m].index, m, "merged mesh index");
    }
    if (geometry.meshes.size() == 5) {
        check(geometry.meshes[2].offset > fixture.sections[3].offset &&
                  geometry.meshes[2].offset <
                      fixture.sections[3].offset + fixture.sections[3].bytes.size(),
              "SIGN's offset is inside Z0 in the file");
    }
    check(load.sections[1].first_mesh == 0 && load.sections[1].mesh_count == 2 &&
              load.sections[2].first_mesh == 2 && load.sections[2].mesh_count == 1 &&
              load.sections[3].mesh_count == 0 && load.sections[4].first_mesh == 3 &&
              load.sections[5].first_mesh == 4,
          "each section's meshes");
    check_eq_u64(geometry.cost.meshes, 5, "merged mesh count");
    check_eq_u64(geometry.cost.valid_meshes, 5, "merged valid meshes");
    check_eq_u64(geometry.cost.vertices, 6 + 3 + 3 + 3 + 3, "merged vertices");
    check_eq_u64(geometry.cost.triangles, 2 + 1 + 1 + 1 + 1, "merged triangles");
    check_eq_u64(geometry.cost.materials, 5, "merged materials");
    check_eq_u64(geometry.cost.geometry_roots, 4, "merged roots");
    check_eq_u64(geometry.cost.largest_mesh_vertices, 6, "largest mesh");
    check_eq_u64(geometry.cost.source_vertex_bytes, 18 * 24, "source vertex bytes");
    check_eq_u64(geometry.cost.distinct_texture_hashes, 3, "T1 T2 T3");
    check(geometry.cost.has_bounds && geometry.cost.bounds_max[0] == 5.0F,
          "merged bounds reach ROAD's last vertex");
    check(geometry.mesh_table_valid, "every hash table verified");
    std::uint64_t geometry_bytes = 0;
    for (const nfsu::TrackStreamSection& section : load.sections) {
        if (section.geometry_roots > 0) {
            geometry_bytes += section.size;
        }
    }
    check_eq_u64(geometry.cost.region_bytes, geometry_bytes,
                 "merged region bytes are the parsed sections'");
    check_eq_u64(geometry.region.size, load.bytes_read, "merged region size");

    const nfsu::TrackObjectReport& objects = load.objects;
    check_eq_u64(objects.sections.size(), 4, "2600 1011 2603 1003");
    const std::uint32_t numbers[] = {2600, 1011, 2603, 1003};
    for (std::size_t s = 0; s < objects.sections.size() && s < 4; ++s) {
        check_eq_u64(objects.sections[s].number, numbers[s], "merged scenery number");
        check_eq_u64(objects.sections[s].index, s, "merged scenery index");
    }
    check_eq_u64(objects.cost.instances, 1 + 1 + 2 + 4, "merged instances");
    check_eq_u64(objects.cost.objects, 1 + 1 + 2 + 4, "merged objects");
    check_eq_u64(objects.cost.sections, 4, "merged scenery sections");
    check_eq_u64(objects.cost.valid_sections, 4, "merged valid scenery sections");
    check_eq_u64(objects.cost.largest_section_instances, 4, "largest scenery section");
    check(load.sections[3].first_scenery == 1 && load.sections[3].scenery_count == 1 &&
              load.sections[5].first_scenery == 3,
          "each section's scenery");
}

void test_whole_scene_places_across_sections() {
    const Fixture fixture = make_fixture();
    Loaded loaded;
    if (!load_fixture(fixture, nfsu::TrackStreamSet::permanent, loaded)) {
        return;
    }
    nfsu::TrackStreamLoad& load = loaded.load;
    const std::vector<std::uint32_t> wanted =
        nfsu::track_region_texture_hashes(load.geometry);
    std::vector<nfsu::RenderTextureSource> sources;
    std::vector<std::size_t> owners;
    nfsu::track_stream_texture_sources(load, &wanted, sources, owners);
    check_eq_u64(sources.size(), 1, "one section holds a pack");
    check(owners.size() == 1 && owners[0] == 0, "and it is Y0");
    if (sources.size() == 1) {
        check(sources[0].bytes == &load.sections[0].bytes, "the source is Y0's own bytes");
        check(sources[0].wanted == &wanted && sources[0].complete, "wanted and complete");
    }
    // The separate file, behind the stream's packs: T3 only it carries, T1
    // the stream carries first.
    const nfsu::TexturePackSet file_set = nfsu::parse_texture_pack_set(
        fixture.texture_file.data(), fixture.texture_file.size());
    nfsu::RenderTextureSource file;
    file.bytes = &fixture.texture_file;
    for (const nfsu::TexturePack& pack : file_set.packs) {
        file.packs.push_back(&pack);
    }
    file.complete = file_set.complete;
    file.wanted = &wanted;
    sources.push_back(file);
    std::vector<std::size_t> source_owner = owners;
    source_owner.push_back(nfsu::kTrackStreamNoRecord);

    nfsu::RenderTextureSet textures;
    std::string error;
    check(nfsu::decode_render_texture_sources(sources, nfsu::RenderTextureLimits{}, textures,
                                              error),
          "textures decode: " + error);
    check_eq_u64(textures.textures.size(), 3, "T1 T2 from Y0 and T3 from the file");
    check_eq_u64(textures.shadowed.size(), 1, "the file's T1 is shadowed by Y0's");

    nfsu::PreparedTrackScene scene;
    check(nfsu::build_track_scene(load.geometry, load.objects, textures,
                                  nfsu::TrackSceneOptions{}, scene, error),
          "the whole scene builds: " + error);
    // 2600 places ROAD from X0, 1011 places LAMP from X0, 2603 places CONE
    // from itself and LAMP from X0, 1003 places ROAD and LAMP from X0, BENCH
    // from itself and a hash nothing holds.
    check_eq_u64(scene.cost.source_instances, 8, "instances");
    check_eq_u64(scene.cost.placed_instances, 7, "placed across sections");
    check_eq_u64(scene.cost.unplaced_unresolved, 1, "the missing mesh");
    check(scene.unplaced_hashes.size() == 1 && scene.unplaced_hashes[0] == kMissingMesh,
          "and it is the missing hash");
    check_eq_u64(scene.cost.drawn_meshes, 4, "SIGN is placed by nothing");
    check_eq_u64(scene.region.cost.resolved_draws, 5, "every group resolves");

    std::vector<nfsu::TrackStreamSectionCost> costs;
    check(nfsu::track_stream_section_costs(load, scene, source_owner, costs, error),
          "section costs: " + error);
    if (costs.size() == 6) {
        check(costs[0].pack_records == 2 && costs[0].retained_textures == 2 &&
                  costs[0].retained_texture_bytes == 32,
              "Y0 supplies two retained textures of 16 bytes");
        check(costs[1].meshes == 2 && costs[1].valid_meshes == 2 &&
                  costs[1].vertices == 9 && costs[1].triangles == 3 &&
                  costs[1].placed_instances == 0,
              "X0 holds meshes and places nothing");
        check(costs[2].scenery_sections == 1 && costs[2].instances == 1 &&
                  costs[2].placed_instances == 1 && costs[2].retained_textures == 0,
              "Z0 places one; T3 came from the file, not from Z0");
        check(costs[3].meshes == 0 && costs[3].instances == 1 &&
                  costs[3].placed_instances == 1,
              "X99 carries scenery only");
        check(costs[4].instances == 2 && costs[4].placed_instances == 2, "Z3");
        check(costs[5].instances == 4 && costs[5].placed_instances == 3, "J3");
        std::uint64_t retained = 0;
        for (const nfsu::TrackStreamSectionCost& cost : costs) {
            retained += cost.retained_texture_bytes;
        }
        check_eq_u64(retained + 16, scene.region.cost.texture_bytes,
                     "the stream's retained bytes plus the file's T3");
    } else {
        check(false, "six section costs");
    }

    // A consuming preparation must preserve shared-texture resolution and
    // first-use handles while transferring the original payload allocations.
    auto geometry_owned = load.geometry;
    auto textures_owned = textures;
    const auto* vertices_before = geometry_owned.meshes[0].vertices.data();
    const auto* texture_before = textures_owned.textures[0].payload().data();
    nfsu::PreparedTrackScene moved;
    check(nfsu::build_track_scene(std::move(geometry_owned), load.objects,
          std::move(textures_owned), nfsu::TrackSceneOptions{}, moved, error),
          "consuming scene builds: " + error);
    check(moved.region.meshes.size() == scene.region.meshes.size() &&
          moved.region.draws.size() == scene.region.draws.size() &&
          moved.region.cost.resolved_draws == scene.region.cost.resolved_draws &&
          moved.cost.placed_instances == scene.cost.placed_instances,
          "consumed scene preserves draws and placement");
    check(!moved.region.meshes.empty() &&
          moved.region.meshes[0].vertices.data() == vertices_before,
          "geometry allocation transferred without a copy");
    bool texture_transferred = false;
    for (const auto& texture : moved.region.textures)
        texture_transferred |= texture.payload().data() == texture_before;
    check(texture_transferred, "texture allocation transferred without a copy");
    for (std::size_t i = 0; i < moved.region.draws.size() && i < scene.region.draws.size(); ++i) {
        const auto& a = moved.region.draws[i]; const auto& b = scene.region.draws[i];
        check(a.binding == b.binding && a.texture == b.texture &&
              a.texture_source == b.texture_source && a.texture_pack == b.texture_pack &&
              a.texture_record == b.texture_record && a.raw == b.raw,
              "consumed draw preserves binding, provenance and raw material");
    }
    geometry_owned = load.geometry; textures_owned = textures;
    nfsu::TrackSceneOptions tiny; tiny.region.max_texture_bytes = 1;
    check(!nfsu::build_track_scene(std::move(geometry_owned), load.objects,
          std::move(textures_owned), tiny, moved, error) && moved.region.meshes.empty() &&
          moved.instances.empty(), "consuming budget failure clears the output");

    // J3 alone places only its own BENCH: the whole-track scene is what
    // resolves ROAD and LAMP.
    Loaded only;
    only.table = parse_table(fixture.trackb);
    only.selection.records = {6};
    only.selection.reasons = {nfsu::TrackStreamReason::track_flag};
    const std::vector<unsigned char>* images[nfsu::kTrackStreamFileCount] = {
        nullptr, &fixture.stream, nullptr};
    check(nfsu::load_track_stream_sections_from_memory(only.table, only.selection, images,
                                                       nfsu::TrackStreamLoadLimits{},
                                                       only.load, error),
          "J3 alone loads");
    nfsu::PreparedTrackScene alone;
    check(nfsu::build_track_scene(only.load.geometry, only.load.objects,
                                  nfsu::RenderTextureSet{}, nfsu::TrackSceneOptions{}, alone,
                                  error) &&
              alone.cost.placed_instances == 1 && alone.cost.unplaced_unresolved == 3,
          "J3 alone places one instance of four");

    // A cost call over a scene that names scenery the load did not supply.
    nfsu::PreparedTrackScene foreign = scene;
    if (!foreign.instances.empty()) {
        foreign.instances[0].section_index = 99;
        check(!nfsu::track_stream_section_costs(load, foreign, source_owner, costs, error) &&
                  error.find("no stream section supplied") != std::string::npos,
              "a foreign instance is refused");
    }
}

void test_load_refusals() {
    const Fixture fixture = make_fixture();
    std::string error;
    Loaded loaded;
    loaded.table = parse_table(fixture.trackb);
    // Select every stream record by hand, and damage four.
    for (std::size_t k = 1; k < 9; ++k) {
        loaded.selection.records.push_back(k);
        loaded.selection.reasons.push_back(nfsu::TrackStreamReason::stream_bound);
    }
    // X99 is moved to a pack file this load is not given; Z3 is pointed at
    // the gap filler after Y0; J9 starts one byte later; W1 reaches one byte
    // past the file.
    loaded.table.records[4].file_index = 2;
    loaded.table.records[5].data_offset = static_cast<std::uint32_t>(
        fixture.sections[1].bytes.size() + 127U) & ~127U;
    loaded.table.records[5].size = 64;
    loaded.table.records[7].data_offset += 1;
    loaded.table.records[8].size = static_cast<std::uint32_t>(
        fixture.stream.size() - loaded.table.records[8].data_offset + 1);
    const std::vector<unsigned char>* images[nfsu::kTrackStreamFileCount] = {
        nullptr, &fixture.stream, nullptr};
    nfsu::TrackStreamLoad& load = loaded.load;
    check(nfsu::load_track_stream_sections_from_memory(loaded.table, loaded.selection, images,
                                                       nfsu::TrackStreamLoadLimits{}, load,
                                                       error),
          "a damaged selection still loads: " + error);
    check(!load.ok(), "and reports it");
    if (load.sections.size() != 8) {
        check(false, "eight sections attempted");
        return;
    }
    const auto issue = [&](std::size_t s, const std::string& text) {
        return has_issue(load.sections[s].issues, load.sections[s].offset, text);
    };
    check(!load.sections[3].loaded &&
              issue(3, "file index 2, which this load was not given"),
          "X99 names a file the load was not given");
    check(!load.sections[4].loaded && !load.sections[4].issues.empty() &&
              load.sections[4].issues[0].message.find("0xEEEEEEEE") != std::string::npos,
          "Z3's range is the gap filler, which is not a chunk");
    check(!load.sections[6].loaded && issue(6, "not a multiple of 128"), "J9 is misaligned");
    check(!load.sections[7].loaded && issue(7, "in a file of"), "W1 reaches past the file");
    check(load.sections[0].loaded && load.sections[1].loaded && load.sections[2].loaded &&
              load.sections[5].loaded,
          "the sound sections still load");
    check_eq_u64(load.bytes_read,
                 fixture.sections[1].bytes.size() + fixture.sections[2].bytes.size() +
                     fixture.sections[3].bytes.size() + 64 +
                     fixture.sections[6].bytes.size(),
                 "bytes read count only the ranges read");

    // A section whose offset lies wholly past the end of its file (review
    // 3462, F2). W1 above starts inside the file and ends one byte past it;
    // here Z0 starts a whole 2048 bytes past the end, on a boundary no
    // alignment rule refuses. The range test has to refuse the offset
    // itself: file_size - offset would wrap, and the reader would copy bytes
    // from beyond the buffer. The copy read here reserves room past the
    // read, so that a broken range test fails the checks below rather than
    // faulting; its size, which is the file size the loader sees, is the
    // stream's.
    {
        Bytes stream = fixture.stream;
        stream.reserve(stream.size() + 3 * kSectionStep);
        const std::vector<unsigned char>* past_images[nfsu::kTrackStreamFileCount] = {
            nullptr, &stream, nullptr};
        Loaded past;
        past.table = parse_table(fixture.trackb);
        past.selection.records = {3};
        past.selection.reasons = {nfsu::TrackStreamReason::stream_bound};
        const std::uint64_t beyond = stream.size() + kSectionStep;
        past.table.records[3].data_offset = static_cast<std::uint32_t>(beyond);
        past.table.records[3].size = 64;
        nfsu::TrackStreamLoad& refused = past.load;
        check(nfsu::load_track_stream_sections_from_memory(past.table, past.selection,
                                                           past_images,
                                                           nfsu::TrackStreamLoadLimits{},
                                                           refused, error),
              "an offset past the file is an issue, not a failure: " + error);
        const bool named = refused.sections.size() == 1 &&
                           has_issue(refused.sections[0].issues, beyond,
                                     "reaches " + std::to_string(beyond + 64) +
                                         " in a file of " + std::to_string(stream.size()) +
                                         " bytes");
        check(refused.sections.size() == 1 && !refused.sections[0].loaded &&
                  refused.sections[0].bytes.empty() && named,
              "a section starting past the end of its file is refused by name");
        check_eq_u64(refused.bytes_read, 0, "and nothing is read for it");
    }

    // The accepting side of the 128-byte alignment (review 3462, F3). X0's
    // geometry and Y0's texture pack, copied byte for byte to offsets that
    // are odd multiples of 128 - so no larger power of two divides them -
    // load exactly as they do at their own 2048-byte boundaries: moving a
    // section by a multiple of 128 keeps every alignment of the format.
    {
        Bytes shifted = fixture.stream;
        const std::uint64_t x0_at = shifted.size() + 128U;
        shifted.resize(static_cast<std::size_t>(x0_at), 0xEEU);
        shifted.insert(shifted.end(), fixture.sections[2].bytes.begin(),
                       fixture.sections[2].bytes.end());
        const std::uint64_t y0_at =
            ((shifted.size() + kSectionStep - 1) / kSectionStep) * kSectionStep + 384U;
        shifted.resize(static_cast<std::size_t>(y0_at), 0xEEU);
        shifted.insert(shifted.end(), fixture.sections[1].bytes.begin(),
                       fixture.sections[1].bytes.end());
        check(x0_at % 128 == 0 && (x0_at / 128) % 2 == 1 && y0_at % 128 == 0 &&
                  (y0_at / 128) % 2 == 1,
              "both new offsets are odd multiples of 128");

        Loaded in_place;
        in_place.table = parse_table(fixture.trackb);
        in_place.selection.records = {1, 2};
        in_place.selection.reasons = {nfsu::TrackStreamReason::fixed,
                                      nfsu::TrackStreamReason::fixed};
        Loaded moved = in_place;
        moved.table.records[1].data_offset = static_cast<std::uint32_t>(y0_at);
        moved.table.records[2].data_offset = static_cast<std::uint32_t>(x0_at);
        const std::vector<unsigned char>* moved_images[nfsu::kTrackStreamFileCount] = {
            nullptr, &shifted, nullptr};
        check(nfsu::load_track_stream_sections_from_memory(in_place.table, in_place.selection,
                                                           images,
                                                           nfsu::TrackStreamLoadLimits{},
                                                           in_place.load, error) &&
                  nfsu::load_track_stream_sections_from_memory(
                      moved.table, moved.selection, moved_images,
                      nfsu::TrackStreamLoadLimits{}, moved.load, error),
              "both loads run: " + error);
        const auto& a = in_place.load;
        const auto& b = moved.load;
        const bool pair = a.sections.size() == 2 && b.sections.size() == 2;
        check(pair && b.ok() && b.sections[0].loaded && b.sections[1].loaded &&
                  b.sections[0].issues.empty() && b.sections[1].issues.empty(),
              "Y0 at " + std::to_string(y0_at) + " and X0 at " + std::to_string(x0_at) +
                  " load with no issue");
        check(pair && a.ok() && b.sections[1].bytes == a.sections[1].bytes &&
                  b.sections[1].mesh_count == a.sections[1].mesh_count &&
                  b.sections[1].mesh_count == 2 &&
                  b.geometry.cost.valid_meshes == a.geometry.cost.valid_meshes &&
                  b.geometry.cost.valid_meshes == 2,
              "X0 moved by a multiple of 128 decodes the same two meshes");
        check(pair && b.sections[0].texture_packs == 1 &&
                  b.sections[0].packs.packs.size() == 1 &&
                  a.sections[0].packs.packs.size() == 1 &&
                  b.sections[0].packs.packs[0].valid_texture_count() == 2 &&
                  b.sections[0].packs.packs[0].valid_texture_count() ==
                      a.sections[0].packs.packs[0].valid_texture_count(),
              "Y0 moved by a multiple of 128 decodes the same two textures");
        check_eq_u64(b.bytes_read, a.bytes_read, "the same bytes are read");
    }

    // A selection naming a record past the table, and one whose two lists
    // disagree, are failures rather than issues.
    nfsu::TrackStreamSelection bad = loaded.selection;
    bad.records.push_back(99);
    bad.reasons.push_back(nfsu::TrackStreamReason::fixed);
    check(!nfsu::load_track_stream_sections_from_memory(loaded.table, bad, images,
                                                        nfsu::TrackStreamLoadLimits{}, load,
                                                        error) &&
              error.find("record 99") != std::string::npos,
          "a record past the table is refused");
    bad.reasons.pop_back();
    check(!nfsu::load_track_stream_sections_from_memory(loaded.table, bad, images,
                                                        nfsu::TrackStreamLoadLimits{}, load,
                                                        error) &&
              error.find("differ in length") != std::string::npos,
          "records and reasons of two lengths are refused");

    // The byte budget over every section read: exactly the bytes of the
    // permanent set pass, one fewer fails.
    Loaded permanent;
    load_fixture(fixture, nfsu::TrackStreamSet::permanent, permanent);
    nfsu::TrackStreamLoadLimits limits;
    limits.max_total_bytes = permanent.load.bytes_read;
    nfsu::TrackStreamLoad again;
    check(nfsu::load_track_stream_sections_from_memory(permanent.table, permanent.selection,
                                                       images, limits, again, error),
          "the byte budget is inclusive");
    limits.max_total_bytes -= 1;
    check(!nfsu::load_track_stream_sections_from_memory(permanent.table, permanent.selection,
                                                        images, limits, again, error) &&
              error.find("hold more than") != std::string::npos,
          "one byte over the budget fails");
}

void test_stream_set_reports_the_refused_mesh() {
    const Fixture fixture = make_fixture();
    Loaded loaded;
    if (!load_fixture(fixture, nfsu::TrackStreamSet::stream, loaded)) {
        return;
    }
    check(!loaded.load.ok(), "the 60-byte mesh is an issue");
    check_eq_u64(loaded.load.sections.size(), 8, "eight sections");
    if (loaded.load.sections.size() == 8) {
        const nfsu::TrackStreamSection& w1 = loaded.load.sections[7];
        check(w1.name == "W1" && w1.loaded, "W1 loads");
        check(w1.mesh_count == 2, "both of W1's meshes are merged");
    }
    check(loaded.load.geometry.issues.size() == 1 &&
              loaded.load.geometry.issues[0].message.find("stride of 60") !=
                  std::string::npos,
          "W1's crowd mesh is refused by name in the merged report");
    check_eq_u64(loaded.load.geometry.cost.valid_meshes, 7, "the rest decode");
}

bool write_file(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void test_load_from_files() {
    const Fixture fixture = make_fixture();
    std::error_code code;
    const fs::path directory = fs::temp_directory_path(code) / unique_fixture("files");
    fs::remove_all(directory, code);
    fs::create_directories(directory, code);
    const fs::path stream = directory / "STREAM.BUN";
    check(write_file(stream, fixture.stream), "stream written");

    Loaded memory;
    load_fixture(fixture, nfsu::TrackStreamSet::permanent, memory);
    nfsu::TrackStreamFiles files;
    files.stream = stream;
    nfsu::TrackStreamLoad load;
    std::string error;
    check(nfsu::load_track_stream_sections(memory.table, memory.selection, files,
                                           nfsu::TrackStreamLoadLimits{}, load, error),
          "the file load succeeds: " + error);
    check(load.ok(), "and raises no issue");
    check_eq_u64(load.bytes_read, memory.load.bytes_read, "the same bytes as from memory");
    check_eq_u64(load.stream_file_size, fixture.stream.size(), "stream size from the file");
    check_eq_u64(load.geometry.file_size, fixture.stream.size(), "geometry file size");
    bool same = load.sections.size() == memory.load.sections.size();
    for (std::size_t s = 0; same && s < load.sections.size(); ++s) {
        same = load.sections[s].bytes == memory.load.sections[s].bytes &&
               load.sections[s].mesh_count == memory.load.sections[s].mesh_count;
    }
    check(same, "every section matches the in-memory load");

    // A pack file the load is told about but cannot size.
    files.has_pack = true;
    files.pack = directory / "missing_pack.bun";
    check(nfsu::load_track_stream_sections(memory.table, memory.selection, files,
                                           nfsu::TrackStreamLoadLimits{}, load, error) &&
              !load.ok() && has_issue(load.issues, 0, "file index 2: cannot size the file"),
          "a missing pack file is reported");
    files.has_pack = false;
    files.stream = directory / "missing_stream.bun";
    check(nfsu::load_track_stream_sections(memory.table, memory.selection, files,
                                           nfsu::TrackStreamLoadLimits{}, load, error) &&
              !load.ok() &&
              has_issue(load.issues, 0, "file index 1: cannot size the file") &&
              load.bytes_read == 0,
          "a missing stream is reported and nothing is read");
    fs::remove_all(directory, code);
}

void test_budget_rows() {
    const Fixture fixture = make_fixture();
    Loaded loaded;
    if (!load_fixture(fixture, nfsu::TrackStreamSet::permanent, loaded)) {
        return;
    }
    nfsu::PreparedTrackScene scene;
    std::string error;
    check(nfsu::build_track_scene(loaded.load.geometry, loaded.load.objects,
                                  nfsu::RenderTextureSet{}, nfsu::TrackSceneOptions{}, scene,
                                  error),
          "an untextured scene builds");
    nfsu::SceneMemoryRequirement gpu;
    gpu.buffer_extent = 1000;
    gpu.image_extent = 20;
    gpu.staging_largest_bytes = 3;
    std::vector<nfsu::TrackWholeBudgetRow> rows;
    nfsu::track_whole_budget_rows(scene, 77, 555, gpu, rows);
    const char* names[] = {"region.upload",          "region.vertices",
                           "region.triangles",       "region.draws",
                           "region.drawn_triangles", "region.texture_bytes",
                           "textures.decoded_bytes", "frame.draw_list_bytes",
                           "streaming.region",       "streaming.arena",
                           "gpu.arena",              "transient.arena"};
    check_eq_u64(rows.size(), 12, "twelve rows");
    for (std::size_t r = 0; r < rows.size() && r < 12; ++r) {
        check(rows[r].name == names[r], "row " + std::to_string(r) + " is " + names[r]);
        check(rows[r].platform == (r < 8 ? nfsu::TrackWholePlatform::desktop
                                         : nfsu::TrackWholePlatform::switch_plan),
              "row " + std::to_string(r) + " platform");
        check(rows[r].within, "row " + std::to_string(r) + " is within");
    }
    if (rows.size() == 12) {
        const nfsu::TrackDrawCost& cost = scene.region.cost;
        check_eq_u64(rows[0].value,
                     cost.vertex_buffer_bytes + cost.index_buffer_bytes + cost.texture_bytes,
                     "upload value");
        check_eq_u64(rows[0].ceiling,
                     nfsu::track_region_upload_ceiling(nfsu::TrackSceneLimits{}),
                     "upload ceiling");
        check_eq_u64(rows[1].value, cost.vertices, "vertices value");
        check_eq_u64(rows[1].ceiling, nfsu::TrackSceneLimits{}.max_vertices,
                     "vertices ceiling");
        check_eq_u64(rows[2].value, cost.triangles, "triangles value");
        check_eq_u64(rows[3].value, cost.draws, "draws value");
        check_eq_u64(rows[3].ceiling, nfsu::TrackSceneLimits{}.max_draws, "draws ceiling");
        check_eq_u64(rows[4].value, cost.drawn_triangles, "drawn triangles value");
        check_eq_u64(rows[4].ceiling, nfsu::TrackSceneLimits{}.max_draw_triangles,
                     "drawn triangles ceiling");
        check_eq_u64(rows[5].ceiling, nfsu::TrackSceneLimits{}.max_texture_bytes,
                     "texture bytes ceiling");
        check_eq_u64(rows[6].value, 77, "decoded bytes value");
        check_eq_u64(rows[6].ceiling, nfsu::RenderTextureLimits{}.max_total_bytes,
                     "decoded bytes ceiling");
        check_eq_u64(rows[7].value, nfsu::track_scene_frame_ceiling(scene), "frame value");
        check_eq_u64(rows[7].ceiling,
                     nfsu::track_draw_list_frame_ceiling(nfsu::TrackDrawListLimits{}),
                     "frame ceiling");
        check_eq_u64(rows[8].value, 555, "resident value");
        check_eq_u64(rows[8].ceiling, nfsu::kStreamingRegionBudgetPlaceholder,
                     "per-region placeholder");
        check_eq_u64(rows[9].value, 555, "streaming arena value");
        check_eq_u64(rows[9].ceiling,
                     nfsu::kResidentRegionsPlaceholder *
                         nfsu::kStreamingRegionBudgetPlaceholder,
                     "streaming arena");
        check_eq_u64(rows[10].value, 1023, "gpu: buffers + images + one staging copy");
        check_eq_u64(rows[10].ceiling, nfsu::kGpuBudgetPlaceholder, "gpu arena");
        check_eq_u64(rows[11].value, nfsu::track_scene_frame_ceiling(scene),
                     "transient value");
        check_eq_u64(rows[11].ceiling, nfsu::kTransientBudgetPlaceholder, "transient arena");
    }

    // Over, and exactly at, a ceiling.
    nfsu::PreparedTrackScene big;
    big.region.cost.vertices = nfsu::TrackSceneLimits{}.max_vertices;
    big.region.cost.triangles = nfsu::TrackSceneLimits{}.max_triangles + 1;
    big.cost.resident_entries = nfsu::kTransientBudgetPlaceholder / 8 + 1;
    nfsu::track_whole_budget_rows(big, 0, nfsu::kStreamingRegionBudgetPlaceholder, gpu,
                                  rows);
    check(rows.size() == 12 && rows[1].within && !rows[2].within && rows[8].within &&
              !rows[11].within,
          "a value at its ceiling is within, one above is over");
    nfsu::track_whole_budget_rows(big, 0, nfsu::kStreamingRegionBudgetPlaceholder + 1, gpu,
                                  rows);
    check(rows.size() == 12 && !rows[8].within && rows[9].within,
          "one byte over the region placeholder, well inside the arena");
    check(std::string(nfsu::track_whole_platform_name(nfsu::TrackWholePlatform::desktop)) ==
                  "desktop" &&
              std::string(nfsu::track_whole_platform_name(
                  nfsu::TrackWholePlatform::switch_plan)) == "switch",
          "platform names");
}

// --write-fixture <trackb> <stream> <texture>: lays the fixture down for
// tests/track_whole_guards.cmake and runs no check. It writes only where it
// is told and never replaces a file.
bool write_fixture(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        std::error_code code;
        if (fs::exists(path, code)) {
            std::cerr << "error: --write-fixture " << path
                      << " already exists; this command never replaces a file\n";
            return false;
        }
    }
    const Fixture fixture = make_fixture();
    const bool ok = write_file(paths[0], fixture.trackb) &&
                    write_file(paths[1], fixture.stream) &&
                    write_file(paths[2], fixture.texture_file);
    if (!ok) {
        std::cerr << "failed to write the fixture\n";
    }
    return ok;
}

// --write-shares-fixture <trackb> <stream>: lays the minimal F4 share
// fixture (make_shares_fixture, above) down for the new cases in
// tests/track_stream_path_guards.cmake. Never replaces a file.
bool write_shares_fixture(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        std::error_code code;
        if (fs::exists(path, code)) {
            std::cerr << "error: --write-shares-fixture " << path
                      << " already exists; this command never replaces a file\n";
            return false;
        }
    }
    const Fixture fixture = make_shares_fixture();
    const bool ok =
        write_file(paths[0], fixture.trackb) && write_file(paths[1], fixture.stream);
    if (!ok) {
        std::cerr << "failed to write the shares fixture\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--write-fixture") {
        return write_fixture({argv[2], argv[3], argv[4]}) ? 0 : 1;
    }
    if (argc == 4 && std::string(argv[1]) == "--write-shares-fixture") {
        return write_shares_fixture({argv[2], argv[3]}) ? 0 : 1;
    }
    if (argc != 1) {
        std::cerr << "usage: nfsu_track_stream_tests [--write-fixture <trackb> <stream> "
                     "<texture>] [--write-shares-fixture <trackb> <stream>]\n";
        return 1;
    }
    test_constants();
    test_table_parses();
    test_table_refusals();
    test_pool_bytes();
    test_permanent_selection();
    test_bound_sets();
    test_request_limit();
    test_load_poll_and_cancel();
    test_load_and_merge();
    test_whole_scene_places_across_sections();
    test_load_refusals();
    test_stream_set_reports_the_refused_mesh();
    test_load_from_files();
    test_budget_rows();
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed of " << g_checks << '\n';
        return 1;
    }
    std::cout << "all track stream tests passed (" << g_checks << " checks)\n";
    return 0;
}
