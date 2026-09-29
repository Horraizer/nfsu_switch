// The stock-car frame the Switch application builds (nfsu_stock_frame), on
// synthetic inputs laid out as the game lays out its own, the controller
// session it runs (nfsu_stock_view), and the modes a script or a person
// drives it through:
//
//   nfsu_stock_frame_tests                    the unit tests below
//   nfsu_stock_frame_tests --write-fixture D  writes the six synthetic files
//                                             under D, refusing to replace any
//   nfsu_stock_frame_tests --frame ROOT       runs the application's calls
//                                             for the unpainted frame over the
//                                             six files under ROOT and prints
//                                             what it prints
//   nfsu_stock_frame_tests --painted-frame ROOT
//                                             the same for the painted frame
//
// tests/stock_frame_viewer_match.cmake uses the last three to hold both
// frames to nfsu_viewer's on the same files. --frame and --painted-frame
// over the retail installation are the desktop reproductions
// docs/SWITCH_BUILD.md shows; they read the files and write nothing.

#include "nfsu/arg_path.h"
#include "nfsu/input.h"
#include "nfsu/render.h"
#include "nfsu/sha256.h"
#include "nfsu/stock_frame.h"
#include "nfsu/stock_view.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
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

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// A directory name no other process shares, as tests/car_parts_tests.cpp
// builds one (issue #124): several sessions run ctest at once here.
fs::path unique_directory(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return fs::temp_directory_path() /
           ("nfsu_stock_frame_" + label + "_" + std::to_string(pid));
}

// ---------------------------------------------------------------------------
// The synthetic inputs. Each file has the shape its retail counterpart has
// and goes through the real parsers; docs/FORMATS.md is the layout.
// ---------------------------------------------------------------------------

using Bytes = std::vector<unsigned char>;

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8U * i)) & 0xFFU));
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
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

void put_f32(Bytes& bytes, std::size_t offset, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u32(bytes, offset, bits);
}

// A chunk tree written into one file image. Positions are file offsets, so
// the alignment filler is what the format asks for at that offset.
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
        put_u32(bytes_, header + 4, static_cast<std::uint32_t>(bytes_.size() - header - 8));
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append(payload);
        end();
    }

    void append(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    void pad_to(std::size_t alignment, unsigned char filler = nfsu::kAlignmentByte) {
        while (bytes_.size() % alignment != 0) {
            bytes_.push_back(filler);
        }
    }

private:
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

// --- Geometry --------------------------------------------------------------

// Positions are (px, py, pz) with py up; the file stores px, pz, py.
struct Vertex {
    float px, py, pz, u, v;
};

struct Marker {
    std::uint32_t name_hash;
    // The translation row, in the slot frame (longitudinal, lateral,
    // vertical).
    float longitudinal, lateral, vertical;
};

struct MeshSpec {
    std::string name;
    std::uint32_t hash;
    std::vector<Vertex> vertices;
    // One material per quad, in order, each drawing texture slot
    // quad % texture_hashes.size().
    std::vector<std::array<std::uint16_t, 4>> quads;
    std::vector<std::uint32_t> texture_hashes;
    std::vector<Marker> markers;
};

void write_mesh(Writer& w, const MeshSpec& spec, std::size_t& offset, std::size_t& size) {
    offset = w.size();
    w.begin(nfsu::kMeshContainerId);

    Bytes metadata(176, 0);
    for (std::size_t i = 0; i < spec.name.size() && i + 1 < nfsu::kMeshNameSize; ++i) {
        metadata[176 - nfsu::kMeshNameSize + i] = static_cast<unsigned char>(spec.name[i]);
    }
    metadata[nfsu::kTextureListCountOffset] =
        static_cast<unsigned char>(spec.texture_hashes.size());
    metadata[nfsu::kMaterialKeyListCountOffset] = 1;
    w.leaf(nfsu::kMeshMetadataId, metadata);

    Bytes textures;
    for (const std::uint32_t hash : spec.texture_hashes) {
        append_u32(textures, hash);
        append_u32(textures, 0);
    }
    w.leaf(nfsu::kTextureListId, textures);
    Bytes keys;
    append_u32(keys, 0x0BADF00DU);
    append_u32(keys, 0);
    w.leaf(nfsu::kMaterialKeyListId, keys);

    if (!spec.markers.empty()) {
        w.begin(nfsu::kMarkerListId);
        w.pad_to(nfsu::kMarkerDataAlignment);
        for (const Marker& marker : spec.markers) {
            Bytes record;
            append_u32(record, marker.name_hash);
            append_u32(record, 0);
            append_u32(record, 0);
            append_u32(record, 0);
            const float matrix[16] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                      0.0F, 0.0F, 1.0F, 0.0F, marker.longitudinal,
                                      marker.lateral, marker.vertical, 1.0F};
            for (const float element : matrix) {
                append_f32(record, element);
            }
            w.append(record);
        }
        w.end();
    }

    w.begin(nfsu::kGeometryContainerId);

    w.begin(nfsu::kGeometryMetadataId);
    w.pad_to(nfsu::kGeometryMetadataAlignment);
    Bytes record(nfsu::kGeometryMetadataSize, 0);
    put_u32(record, nfsu::kMaterialCountOffset, static_cast<std::uint32_t>(spec.quads.size()));
    put_u32(record, nfsu::kFaceCountOffset, static_cast<std::uint32_t>(spec.quads.size() * 2));
    put_u32(record, nfsu::kVertexCountOffset, static_cast<std::uint32_t>(spec.vertices.size()));
    w.append(record);
    w.end();

    w.begin(nfsu::kVertexBufferId);
    w.pad_to(nfsu::kVertexDataAlignment);
    Bytes vertices;
    for (const Vertex& v : spec.vertices) {
        append_f32(vertices, v.px);
        append_f32(vertices, v.pz);
        append_f32(vertices, v.py);
        append_f32(vertices, 0.0F);
        append_f32(vertices, 0.0F);
        append_f32(vertices, 1.0F);
        vertices.insert(vertices.end(), {0x10U, 0x20U, 0x30U, 0x40U});
        append_f32(vertices, v.u);
        append_f32(vertices, v.v);
    }
    w.append(vertices);
    w.end();

    w.begin(nfsu::kMaterialBufferId);
    w.pad_to(nfsu::kMaterialDataAlignment);
    for (std::size_t q = 0; q < spec.quads.size(); ++q) {
        Bytes material(60, 0);
        put_u32(material, 12, 6);
        put_u32(material, 28,
                static_cast<std::uint32_t>(q % std::max<std::size_t>(1, spec.texture_hashes.size())));
        put_u32(material, 32, 0);
        put_u32(material, 52, static_cast<std::uint32_t>(q * 6));
        put_u32(material, 56, 0x4180U);
        w.append(material);
    }
    w.end();

    w.begin(nfsu::kFaceBufferId);
    w.pad_to(nfsu::kFaceDataAlignment);
    Bytes faces;
    for (const std::array<std::uint16_t, 4>& quad : spec.quads) {
        for (const std::uint16_t index : {quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]}) {
            append_u16(faces, index);
        }
    }
    while (faces.size() % nfsu::kFaceTailAlignment != 0) {
        faces.push_back(0);
    }
    w.append(faces);
    w.end();

    w.end();  // geometry container
    w.end();  // mesh container
    size = w.size() - offset;
}

// A geometry file with a verified mesh directory; the hashes must ascend in
// mesh order.
Bytes build_geometry(const std::vector<MeshSpec>& meshes) {
    Writer w;
    w.begin(nfsu::kGeometryRootId);
    w.leaf(0x00000000U, Bytes(4, 0));
    std::vector<std::size_t> offsets(meshes.size());
    std::vector<std::size_t> sizes(meshes.size());
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        write_mesh(w, meshes[i], offsets[i], sizes[i]);
        w.leaf(0x00000000U, Bytes());
    }
    w.begin(nfsu::kGeometryHeaderId);
    Bytes hashes;
    Bytes directory;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        append_u32(hashes, meshes[i].hash);
        append_u32(hashes, 0);
        append_u32(directory, meshes[i].hash);
        append_u32(directory, static_cast<std::uint32_t>(offsets[i]));
        append_u32(directory, static_cast<std::uint32_t>(sizes[i]));
        append_u32(directory, 0);
        append_u32(directory, 0);
    }
    w.leaf(nfsu::kMeshHashTableId, hashes);
    w.leaf(nfsu::kMeshDirectoryId, directory);
    w.end();
    w.end();
    w.begin(0x80034020U);
    w.leaf(0x00039200U, Bytes(4, 0));
    w.end();
    return w.bytes();
}

// An axis-aligned box's visible faces as quads, textured edge to edge.
void add_box(MeshSpec& mesh, float x0, float x1, float y0, float y1, float z0, float z1) {
    const auto quad = [&mesh](Vertex a, Vertex b, Vertex c, Vertex d) {
        const std::uint16_t first = static_cast<std::uint16_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), {a, b, c, d});
        mesh.quads.push_back({first, static_cast<std::uint16_t>(first + 1),
                              static_cast<std::uint16_t>(first + 2),
                              static_cast<std::uint16_t>(first + 3)});
    };
    quad({x0, y1, z0, 0, 0}, {x1, y1, z0, 1, 0}, {x1, y1, z1, 1, 1}, {x0, y1, z1, 0, 1});
    quad({x0, y0, z1, 0, 0}, {x1, y0, z1, 1, 0}, {x1, y1, z1, 1, 1}, {x0, y1, z1, 0, 1});
    quad({x0, y0, z0, 0, 0}, {x1, y0, z0, 1, 0}, {x1, y1, z0, 1, 1}, {x0, y1, z0, 0, 1});
    quad({x1, y0, z0, 0, 0}, {x1, y0, z1, 1, 0}, {x1, y1, z1, 1, 1}, {x1, y1, z0, 0, 1});
    quad({x0, y0, z0, 0, 0}, {x0, y0, z1, 1, 0}, {x0, y1, z1, 1, 1}, {x0, y1, z0, 0, 1});
}

// Mesh hashes, ascending within each file, and the marker names the stock
// assembly reads (the recovered table in part_placement.h).
constexpr std::uint32_t kBaseA = 0x00001001U;
constexpr std::uint32_t kBodyA = 0x00002001U;
constexpr std::uint32_t kRearBumperA = 0x00003001U;
constexpr std::uint32_t kWheelA = 0x00004001U;
constexpr std::uint32_t kExhaustA = 0x00006001U;
constexpr std::uint32_t kBrakeA = 0x00007001U;
constexpr std::uint32_t kPlateA = 0x00008001U;
constexpr std::uint32_t kLeftExhaust = 0xBCF8A18BU;
constexpr std::uint32_t kRightExhaust = 0xBD7CF15EU;
constexpr std::uint32_t kRearLicensePlate = 0xC805F7E0U;

// Textures: one in each of the three sources, one that two sources hold
// (the car's pack must win), one no source holds (drawn with fallback) and
// one a shared source holds that no mesh asks for (never decoded).
const std::string kCarTexture = "SYNTH_CAR_TEXTURE";
const std::string kGlobalTexture = "SYNTH_GLOBAL_TEXTURE";
const std::string kSharedTexture = "SYNTH_SHARED_TEXTURE";
const std::string kUnusedTexture = "SYNTH_UNUSED_TEXTURE";
constexpr std::uint32_t kMissingTexture = 0x5EED0001U;

// The runtime body skin's inputs, named as the retail files name them
// (docs/RUNTIME_SKIN.md): the body's material groups name 240SX_SKIN1,
// which no pack holds, so it is drawn with fallback until a skin is bound
// to it; the target DUMMY_SKIN1 is in the shared Cars/TEXTURES.BIN, and
// the plain doorline and its mask are in the car's own pack. All three
// are A8R8G8B8 and 4x4, the target's size.
const std::string kBodySkin = "240SX_SKIN1";
const std::string kSkinTarget = "DUMMY_SKIN1";
const std::string kDoorline = "240SX_DOORLINE";
const std::string kDoorlineMask = "240SX_DOORLINE_MASK";

// A wheel model authored the way the retail 240SX_KIT00_WHEEL_A is (issue
// #161): that mesh spans lateral 0.0018..0.2006, with its rim at the
// lateral-zero edge and a flat backing quad at +0.2006, so it is a
// right-hand wheel whose outer face looks along -lateral. Here the rim is
// quad 0, textured with the GLOBALB texture, at lateral 0, and the backing
// quad is quad 1, textured with the car's own, at lateral +0.2. Both are
// 0.5 square about the model's origin.
void add_handed_wheel(MeshSpec& mesh) {
    mesh.texture_hashes = {nfsu::bin_hash(kGlobalTexture), nfsu::bin_hash(kCarTexture)};
    for (const float lateral : {0.0F, 0.2F}) {
        const std::uint16_t first = static_cast<std::uint16_t>(mesh.vertices.size());
        mesh.vertices.insert(mesh.vertices.end(), {{-0.25F, -0.25F, lateral, 0.0F, 0.0F},
                                                   {0.25F, -0.25F, lateral, 1.0F, 0.0F},
                                                   {0.25F, 0.25F, lateral, 1.0F, 1.0F},
                                                   {-0.25F, 0.25F, lateral, 0.0F, 1.0F}});
        mesh.quads.push_back({first, static_cast<std::uint16_t>(first + 1),
                              static_cast<std::uint16_t>(first + 2),
                              static_cast<std::uint16_t>(first + 3)});
    }
}

// handed_wheel swaps the fixture's box wheel, which looks the same from
// either side, for the retail-shaped one above.
Bytes car_geometry(bool handed_wheel = false) {
    MeshSpec base{"240SX_BASE_A", kBaseA, {}, {}, {nfsu::bin_hash(kGlobalTexture)}, {}};
    add_box(base, -2.0F, 2.0F, 0.10F, 0.20F, -0.80F, 0.80F);
    MeshSpec body{"240SX_BODY_A",
                  kBodyA,
                  {},
                  {},
                  {nfsu::bin_hash(kCarTexture), nfsu::bin_hash(kBodySkin),
                   nfsu::bin_hash(kSharedTexture)},
                  {}};
    add_box(body, -1.90F, 1.90F, 0.30F, 1.00F, -0.78F, 0.78F);
    add_box(body, -0.90F, 0.60F, 1.00F, 1.40F, -0.65F, 0.65F);
    MeshSpec rear{"240SX_REAR_BUMPER_A",
                  kRearBumperA,
                  {},
                  {},
                  {nfsu::bin_hash(kSharedTexture)},
                  {{kLeftExhaust, -2.05F, 0.50F, 0.20F},
                   {kRearLicensePlate, -2.06F, 0.00F, 0.45F},
                   {kRightExhaust, -2.05F, -0.50F, 0.20F}}};
    add_box(rear, -2.10F, -1.90F, 0.20F, 0.60F, -0.80F, 0.80F);
    MeshSpec wheel{"240SX_WHEEL_A", kWheelA, {}, {}, {nfsu::bin_hash(kGlobalTexture)}, {}};
    if (handed_wheel) {
        add_handed_wheel(wheel);
    } else {
        add_box(wheel, -0.30F, 0.30F, -0.30F, 0.30F, -0.10F, 0.10F);
    }
    MeshSpec exhaust{"240SX_EXHAUST_A", kExhaustA, {}, {}, {kMissingTexture}, {}};
    add_box(exhaust, -0.15F, 0.05F, -0.05F, 0.05F, -0.05F, 0.05F);
    return build_geometry({base, body, rear, wheel, exhaust});
}

Bytes brake_geometry() {
    MeshSpec brake{"BRAKE_A", kBrakeA, {}, {}, {nfsu::bin_hash(kCarTexture)}, {}};
    add_box(brake, -0.18F, 0.18F, -0.18F, 0.18F, -0.12F, 0.12F);
    return build_geometry({brake});
}

Bytes plate_geometry() {
    MeshSpec plate{"LICENSE_PLATE_STYLE01_A", kPlateA, {}, {},
                   {nfsu::bin_hash(kSharedTexture), nfsu::bin_hash(kGlobalTexture)}, {}};
    add_box(plate, -0.02F, 0.00F, -0.10F, 0.10F, -0.26F, 0.26F);
    return build_geometry({plate});
}

// --- Texture packs ---------------------------------------------------------

Bytes jdlz_literals(const Bytes& data) {
    Bytes body;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i % 8 == 0) {
            body.push_back(0x00);
            if (i == 0) {
                body.push_back(0x00);
            }
        }
        body.push_back(data[i]);
    }
    Bytes blob = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};
    append_u32(blob, static_cast<std::uint32_t>(data.size()));
    append_u32(blob, static_cast<std::uint32_t>(body.size() + 16));
    blob.insert(blob.end(), body.begin(), body.end());
    return blob;
}

struct TextureSpec {
    std::string name;
    // Four A8R8G8B8 texels in a 2x2 block, repeated to fill 4x4.
    std::array<std::uint32_t, 4> texels;
};

// A JDLZ pack of 4x4 A8R8G8B8 textures, written into w at its current
// offset; every offset it stores is a file offset.
void write_pack(Writer& w, const std::string& path, const std::vector<TextureSpec>& specs) {
    w.begin(nfsu::kTexturePackRootId);
    w.begin(nfsu::kTextureInfoContainerId);

    Bytes header(124, 0);
    put_u32(header, 0, 4);
    std::memcpy(&header[4], "CARTEXTURES", 11);
    std::memcpy(&header[32], path.data(), path.size());
    put_u32(header, 96, nfsu::bin_hash(path));
    w.leaf(nfsu::kTexturePackHeaderId, header);

    Bytes hash_table;
    for (const TextureSpec& spec : specs) {
        append_u32(hash_table, nfsu::bin_hash(spec.name));
        append_u32(hash_table, 0);
    }
    w.leaf(nfsu::kTextureHashTableId, hash_table);

    w.begin(nfsu::kTextureBlobTableId);
    std::vector<std::size_t> entries;
    for (const TextureSpec& spec : specs) {
        entries.push_back(w.size());
        Bytes entry;
        append_u32(entry, nfsu::bin_hash(spec.name));
        append_u32(entry, 0);
        append_u32(entry, 0);
        append_u32(entry, 0x100);
        append_u32(entry, 0);
        w.append(entry);
    }
    w.end();

    Bytes info;
    std::uint32_t virtual_offset = 0;
    for (const TextureSpec& spec : specs) {
        Bytes record(124, 0);
        std::memcpy(&record[12], spec.name.data(),
                    std::min<std::size_t>(spec.name.size(), nfsu::kTextureNameSize));
        put_u32(record, 36, nfsu::bin_hash(spec.name));
        put_u32(record, 40, 0x001A93CFU);
        const std::uint32_t size = 4 * 4 * 4;
        put_u32(record, 48, virtual_offset);
        virtual_offset = (virtual_offset + size + 127U) & ~127U;
        put_u32(record, 52, virtual_offset);
        put_u32(record, 56, size);
        put_u32(record, 60, 0);
        put_u32(record, 64, size);
        put_u16(record, 68, 4);
        put_u16(record, 70, 4);
        record[72] = 2;
        record[73] = 2;
        record[74] = 0x20;
        record[78] = 1;
        put_u32(record, 84, 0x01000000U);
        info.insert(info.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureInfoTableId, info);

    Bytes formats;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        Bytes record(32, 0);
        put_u32(record, 12, 5);
        put_u32(record, 16, 6);
        put_u32(record, 20, 21);  // D3DFMT_A8R8G8B8
        formats.insert(formats.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureFormatTableId, formats);
    w.end();

    w.begin(nfsu::kTextureDataContainerId);
    Bytes data_header(24, 0);
    put_u32(data_header, 8, 1);
    put_u32(data_header, 12, nfsu::bin_hash(path));
    w.leaf(nfsu::kTextureDataHeaderId, data_header);
    w.begin(nfsu::kTextureDataId);
    w.pad_to(128, 0x11);
    for (std::size_t i = 0; i < specs.size(); ++i) {
        Bytes pixels;
        for (std::size_t y = 0; y < 4; ++y) {
            for (std::size_t x = 0; x < 4; ++x) {
                append_u32(pixels, specs[i].texels[(y / 2) * 2 + (x / 2)]);
            }
        }
        const Bytes blob = jdlz_literals(pixels);
        put_u32(w.bytes(), entries[i] + 4, static_cast<std::uint32_t>(w.size()));
        put_u32(w.bytes(), entries[i] + 8, static_cast<std::uint32_t>(blob.size()));
        w.append(blob);
    }
    w.end();
    w.end();
    w.end();
    w.leaf(0x00000000U, Bytes(16, 0));
}

// with_doorline false leaves the doorline's source and mask out, which is
// how a test makes the skin skip its one input.
Bytes car_textures(bool with_doorline = true) {
    Writer w;
    std::vector<TextureSpec> specs = {
        {kCarTexture, {0xFFC03020U, 0xFF20C030U, 0xFF3020C0U, 0xFFE0E0E0U}}};
    if (with_doorline) {
        specs.push_back({kDoorline, {0x3B101010U, 0x3BF0F0F0U, 0x3B101010U, 0x3BF0F0F0U}});
        specs.push_back(
            {kDoorlineMask, {0x00000000U, 0x00FF00FFU, 0x00FFFF00U, 0x0000FFFFU}});
    }
    write_pack(w, "SYNTH\\240SX.tpk", specs);
    return w.bytes();
}

// with_target false leaves DUMMY_SKIN1 out, so the skin has no target.
Bytes shared_car_textures(bool with_target = true) {
    Writer w;
    // The car's own texture again, which source 0 shadows, GLOBALB.BUN's
    // again, which source 1 shadows, then this source's own. The copies are
    // black, so a decode that searched the sources in another order would
    // draw another frame. Then the skin target, which no mesh names and so
    // is never decoded.
    std::vector<TextureSpec> specs = {
        {kCarTexture, {0xFF000000U, 0xFF000000U, 0xFF000000U, 0xFF000000U}},
        {kGlobalTexture, {0xFF000000U, 0xFF000000U, 0xFF000000U, 0xFF000000U}},
        {kSharedTexture, {0xFF40A0E0U, 0xFFE0A040U, 0xFF808080U, 0xFF104010U}}};
    if (with_target) {
        specs.push_back({kSkinTarget, {0xBF7F7F7FU, 0xBF7F7F7FU, 0xBF7F7F7FU, 0xBF7F7F7FU}});
    }
    write_pack(w, "SYNTH\\CARS.tpk", specs);
    return w.bytes();
}

// --- GLOBALB.BUN: a texture pack, the car type table, the part database ---

struct Wheel {
    float longitudinal, lateral, vertical, radius;
};

void write_car_types(Writer& w) {
    // A padding chunk as GLOBALB.BUN opens with, then the table.
    w.leaf(0x00000000U, Bytes(8, 0));
    w.begin(nfsu::kCarTypeInfoId);
    w.pad_to(nfsu::kCarTypeInfoAlignment, nfsu::kCarTypeInfoFillerByte);
    const Wheel wheels[4] = {{1.25F, 0.75F, 0.10F, 0.30F},
                             {1.25F, -0.75F, 0.10F, 0.30F},
                             {-1.25F, -0.75F, 0.10F, 0.32F},
                             {-1.25F, 0.75F, 0.10F, 0.32F}};
    for (std::size_t r = 0; r < nfsu::kCarTypeInfoRecordCount; ++r) {
        const std::string name = r == 0 ? std::string("240SX") : "CAR" + std::to_string(r);
        Bytes record(nfsu::kCarTypeInfoRecordSize, 0);
        const auto text = [&record](std::size_t offset, const std::string& value) {
            std::memcpy(&record[offset], value.data(), value.size());
        };
        text(nfsu::kCarTypeNameOffset, name);
        text(nfsu::kCarTypeLookupNameOffset, name);
        text(nfsu::kCarTypeGeometryPathOffset, "CARS\\" + name + "\\GEOMETRY.BIN");
        text(nfsu::kCarTypeCompressedPathOffset, "CARS\\" + name + "\\GEOMETRY.LZC");
        text(nfsu::kCarTypeBrandNameOffset, "SYNTH");
        for (std::size_t i = 0; i < nfsu::kCarWheelCount; ++i) {
            const std::size_t base = nfsu::kCarWheelTableOffset + i * nfsu::kCarWheelRecordSize;
            put_f32(record, base + 0x00, wheels[i].longitudinal);
            put_f32(record, base + 0x04, wheels[i].lateral);
            put_f32(record, base + 0x08, wheels[i].vertical);
            put_f32(record, base + 0x10, wheels[i].radius);
            put_f32(record, base + 0x14, 0.2F);
            put_u32(record, base + 0x18, static_cast<std::uint32_t>(i));
            put_f32(record, base + 0x1C, i == 0 || i == 3 ? 0.8F : -0.8F);
            put_f32(record, base + 0x20, i == 0 || i == 3 ? 0.9F : -0.9F);
        }
        w.append(record);
    }
    w.end();
}

struct PartRecord {
    std::string car;
    int slot;
    std::uint8_t level;
    std::string name;
    std::array<std::uint32_t, nfsu::kCarPartLodCount> lods;
};

void write_part_database(Writer& w) {
    // The stock set tests/stock_scene_tests.cpp proves drawable, cut down to
    // one part of each placement kind: the car matrix, the wheel table and
    // both marker kinds.
    std::vector<PartRecord> records = {
        {"240SX", 0, 0, "240SX_BASE", {kBaseA, 0, 0, 0}},
        {"240SX", 2, 0, "240SX_REAR_BUMPER", {kRearBumperA, 0, 0, 0}},
        {"240SX", 5, 0, "240SX_BODY", {kBodyA, 0, 0, 0}},
        {"240SX", 15, 0, "240SX_EXHAUST", {kExhaustA, 0, 0, 0}},
        {"240SX", 17, 0, "240SX_WHEEL", {kWheelA, 0, 0, 0}},
        {"BRAKES", 16, 0, "BRAKE_KIT00", {kBrakeA, kBrakeA, 0, 0}},
        {"PLATES", 18, 3, "LICENSE_PLATE_STYLE01", {kPlateA, 0, 0, 0}},
    };
    std::stable_sort(records.begin(), records.end(),
                     [](const PartRecord& a, const PartRecord& b) {
                         return nfsu::bin_hash(a.car) < nfsu::bin_hash(b.car);
                     });
    Bytes strings;
    Bytes table;
    for (const PartRecord& r : records) {
        const std::uint32_t name_offset = static_cast<std::uint32_t>(strings.size());
        strings.insert(strings.end(), r.name.begin(), r.name.end());
        strings.push_back(0);
        append_u32(table, name_offset);
        append_u32(table, nfsu::bin_hash(r.car));
        append_u32(table, nfsu::bin_hash(r.name));
        append_u32(table, 0);
        table.push_back(static_cast<unsigned char>(r.slot));
        table.push_back(r.level);
        table.push_back(0);
        table.push_back(0);
        append_u32(table, 0);
        append_u32(table, 0);
        append_u16(table, 0);
        append_u16(table, 0);
        for (const std::uint32_t hash : r.lods) {
            append_u32(table, hash);
        }
    }
    Bytes attributes;
    append_u32(attributes, 0x11111111U);
    append_u32(attributes, 7U);

    Bytes header;
    append_u32(header, 0);
    append_u32(header, 0);
    append_u32(header, nfsu::kCarPartDatabaseVersion);
    append_u32(header, 0);
    append_u32(header, static_cast<std::uint32_t>(records.size()));
    append_u32(header, 0);
    append_u32(header, 1);
    append_u32(header, 0);

    w.begin(nfsu::kCarPartDatabaseId);
    w.leaf(nfsu::kCarPartHeaderId, header);
    w.leaf(nfsu::kCarPartStringTableId, strings);
    w.leaf(nfsu::kCarPartAttributeTableId, attributes);
    w.leaf(nfsu::kCarPartRecordTableId, table);
    w.end();
}

Bytes global_bun() {
    Writer w;
    // The pack first, so that its blob offsets and its 128-byte data
    // alignment are file offsets without any shifting.
    write_pack(w, "SYNTH\\GLOBAL.tpk",
               {{kGlobalTexture, {0xFFF0D000U, 0xFF202020U, 0xFFD0F000U, 0xFF602000U}},
                {kUnusedTexture, {0xFFFF00FFU, 0xFFFF00FFU, 0xFFFF00FFU, 0xFFFF00FFU}}});
    write_car_types(w);
    write_part_database(w);
    w.leaf(0x00034201U, Bytes(12, 0));
    return w.bytes();
}

std::vector<Bytes> fixture_files() {
    std::vector<Bytes> files(nfsu::kStockFrameFileCount);
    files[static_cast<std::size_t>(nfsu::StockFrameFile::car_geometry)] = car_geometry();
    files[static_cast<std::size_t>(nfsu::StockFrameFile::car_textures)] = car_textures();
    files[static_cast<std::size_t>(nfsu::StockFrameFile::global_bun)] = global_bun();
    files[static_cast<std::size_t>(nfsu::StockFrameFile::shared_car_textures)] =
        shared_car_textures();
    files[static_cast<std::size_t>(nfsu::StockFrameFile::brake_geometry)] = brake_geometry();
    files[static_cast<std::size_t>(nfsu::StockFrameFile::plate_geometry)] = plate_geometry();
    return files;
}

// Writes the fixture under root, creating the directories, and refuses to
// replace a file that is already there. Returns false with a message.
bool write_fixture(const fs::path& root, const std::vector<Bytes>& files, std::string& error,
                   std::size_t skip = nfsu::kStockFrameFileCount) {
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i == skip) {
            continue;
        }
        const fs::path path = root / fs::path(nfsu::stock_frame_file_path(i));
        std::error_code code;
        fs::create_directories(path.parent_path(), code);
        if (code) {
            error = "cannot create " + path.parent_path().u8string();
            return false;
        }
        if (fs::exists(fs::symlink_status(path, code))) {
            error = path.u8string() + " already exists";
            return false;
        }
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(files[i].data()),
                  static_cast<std::streamsize>(files[i].size()));
        out.close();
        if (!out) {
            error = "cannot write " + path.u8string();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// The application's calls, and what --frame and --painted-frame print about
// them.
// ---------------------------------------------------------------------------

struct Run {
    nfsu::StockFrameInputs inputs;
    nfsu::StockFrame frame;
    nfsu::StockView view;
    nfsu::StockFrameImage image;
    bool read = false;
    bool prepared = false;
    bool started = false;
    bool rendered = false;
    bool encoded = false;
    std::string error;
};

// The skin switch/car_main.cpp prepares with: the painted one by default,
// none with --unpainted.
nfsu::StockFrameSkin app_skin(bool painted) {
    return painted ? nfsu::stock_240sx_skin() : nfsu::StockFrameSkin{};
}

// Exactly the sequence switch/car_main.cpp runs for the frame at the
// default camera, with its arguments: read, prepare, start the view over
// the prepared scene, render through the view, encode.
void run_frame(const fs::path& root, Run& run, bool painted = false) {
    run.read = nfsu::read_stock_frame_inputs(root, run.inputs);
    if (!run.read) {
        return;
    }
    run.prepared = nfsu::prepare_stock_frame(run.inputs, nfsu::stock_240sx_request(),
                                             app_skin(painted), run.frame, run.error);
    if (!run.prepared) {
        return;
    }
    run.started = run.view.start(run.frame.scene, run.error);
    if (!run.started) {
        return;
    }
    run.rendered = run.view.render(run.frame.frame, run.frame.stats, run.error);
    if (!run.rendered) {
        return;
    }
    run.encoded = nfsu::encode_stock_frame(run.frame.frame, run.image, run.error);
}

int frame_mode(const fs::path& root, bool painted, bool tuned = false) {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    auto run = std::make_unique<Run>();
    const Clock::time_point t0 = Clock::now();
    run->read = nfsu::read_stock_frame_inputs(root, run->inputs, tuned);
    const Clock::time_point t1 = Clock::now();
    for (const nfsu::StockFrameInput& input : run->inputs.files) {
        std::cout << "file " << input.path << ' '
                  << (input.read ? "read " + std::to_string(input.bytes.size()) + " bytes"
                                 : "NOT READ: " + input.error)
                  << '\n';
    }
    if (!run->read) {
        std::cout << "result: inputs missing\n";
        return 2;
    }
    run->prepared = nfsu::prepare_stock_frame(
        run->inputs, tuned ? nfsu::tuned_240sx_request() : nfsu::stock_240sx_request(),
        tuned ? nfsu::tuned_240sx_skin() : app_skin(painted), run->frame, run->error);
    const Clock::time_point t2 = Clock::now();
    for (const std::string& issue : run->frame.issues) {
        std::cout << "issue: " << issue << '\n';
    }
    if (!run->prepared) {
        std::cout << "result: not prepared: " << run->error << '\n';
        return 2;
    }
    run->started = run->view.start(run->frame.scene, run->error);
    if (!run->started) {
        std::cout << "result: no view: " << run->error << '\n';
        return 2;
    }
    const Clock::time_point t3 = Clock::now();
    run->rendered = run->view.render(run->frame.frame, run->frame.stats, run->error);
    const Clock::time_point t4 = Clock::now();
    if (!run->rendered) {
        std::cout << "result: not rendered: " << run->error << '\n';
        return 2;
    }
    run->encoded = nfsu::encode_stock_frame(run->frame.frame, run->image, run->error);
    const Clock::time_point t5 = Clock::now();
    if (!run->encoded) {
        std::cout << "result: not encoded: " << run->error << '\n';
        return 2;
    }
    std::cout << nfsu::stock_frame_slots_line(run->frame.report) << '\n';
    for (const std::string& line : nfsu::stock_frame_stats_lines(run->frame.stats)) {
        std::cout << line << '\n';
    }
    for (const std::string& line : nfsu::stock_frame_skin_lines(run->frame)) {
        std::cout << line << '\n';
    }
    std::cout << "view_default " << (run->view.at_default() ? 1 : 0) << '\n';
    std::cout << "inputs_ok " << (run->frame.inputs_ok ? 1 : 0) << '\n';
    std::cout << "tga_bytes " << run->image.tga.size() << '\n';
    std::cout << "sha256 " << nfsu::sha256_hex(run->image.sha256) << '\n';
    std::printf("read_ms %.3f prepare_ms %.3f render_ms %.3f encode_sha256_ms %.3f\n",
                ms(t0, t1), ms(t1, t2), ms(t3, t4), ms(t4, t5));
    return 0;
}

// ---------------------------------------------------------------------------
// The unit tests.
// ---------------------------------------------------------------------------

struct FixtureDirectory {
    fs::path root;
    explicit FixtureDirectory(const std::string& label) : root(unique_directory(label)) {
        std::error_code code;
        fs::remove_all(root, code);
    }
    ~FixtureDirectory() {
        std::error_code code;
        fs::remove_all(root, code);
    }
};

bool has_issue_for(const nfsu::StockFrame& frame, const std::string& path) {
    for (const std::string& issue : frame.issues) {
        if (issue.compare(0, path.size() + 1, path + ":") == 0) {
            return true;
        }
    }
    return false;
}

const nfsu::RenderTexture* texture_with_hash(const nfsu::RenderTextureSet& set,
                                             std::uint32_t hash) {
    for (const nfsu::RenderTexture& texture : set.textures) {
        if (texture.hash == hash) {
            return &texture;
        }
    }
    return nullptr;
}

void test_file_layout() {
    const char* expected[] = {"Cars/240SX/GEOMETRY.BIN", "Cars/240SX/TEXTURES.BIN",
                              "Global/GLOBALB.BUN",      "Cars/TEXTURES.BIN",
                              "Cars/BRAKES/GEOMETRY.BIN", "Cars/PLATES/GEOMETRY.BIN"};
    for (std::size_t i = 0; i < nfsu::kStockFrameFileCount; ++i) {
        const char* path = nfsu::stock_frame_file_path(i);
        check(path != nullptr && std::string(path) == expected[i],
              "file path " + std::to_string(i));
    }
    check(nfsu::stock_frame_file_path(nfsu::kStockFrameFileCount) == nullptr,
          "no seventh file");
    const nfsu::StockSceneRequest request = nfsu::stock_240sx_request();
    check(request.car_type_name == "240SX" && request.lod == 0 &&
              request.pass == nfsu::CarDrawPass::world &&
              request.place_wheels_from_culling_table,
          "the pinned command's request: 240SX, LOD A, world, --place-wheels");
    check(std::string(nfsu::kStock240sxFrameSha256).size() == 64 &&
              nfsu::kStock240sxFrameBytes == 18U + 1280U * 720U * 4U,
          "the pinned frame is a 1280x720 32-bit TGA");
    check(std::string(nfsu::kStock240sxPaintedFrameSha256).size() == 64 &&
              nfsu::kStock240sxPaintedFrameBytes == nfsu::kStock240sxFrameBytes &&
              std::string(nfsu::kStock240sxPaintedFrameSha256) !=
                  nfsu::kStock240sxFrameSha256,
          "the painted pin is another 1280x720 32-bit TGA");

    // The painted command's skin options, one by one.
    const nfsu::StockFrameSkin skin = nfsu::stock_240sx_skin();
    check(skin.enabled && skin.car == "240SX" && skin.target_name == "DUMMY_SKIN1" &&
              skin.body_bind,
          "--skin-car 240SX, the default target, <car>_SKIN1 bound");
    check(skin.selection.doorline &&
              skin.selection.doorline->variant == nfsu::SkinDoorlineVariant::plain,
          "--skin-doorline plain");
    check(skin.selection.base_color == nfsu::skin_pack_paint(30, 60, 160, 128),
          "--skin-base 30 60 160 128");
    bool nothing_else = !skin.selection.hood_spoiler_vinyl;
    for (const auto& layer : skin.selection.vinyl_layers) {
        nothing_else = nothing_else && !layer;
    }
    for (const auto& color : skin.selection.special_colors) {
        nothing_else = nothing_else && !color;
    }
    check(nothing_else, "no vinyl, no hood or spoiler vinyl, no special colour");
    check(!nfsu::StockFrameSkin{}.enabled, "a default StockFrameSkin paints nothing");

    const nfsu::StockSceneRequest tuned = nfsu::tuned_240sx_request();
    check(tuned.installed_parts.names[nfsu::kSlotFrontBumper] ==
              "240SX_KIT01_FRONT_BUMPER" &&
              tuned.installed_parts.names[nfsu::kSlotRearBumper] ==
                  "240SX_KIT01_REAR_BUMPER" &&
              tuned.installed_parts.names[nfsu::kSlotSkirt] == "240SX_KIT01_SKIRT" &&
              tuned.installed_parts.names[nfsu::kSlotWheel] == "BBS_STYLE01_15_23",
          "tuned preset names four verified retail parts");
    const nfsu::StockFrameSkin tuned_skin = nfsu::tuned_240sx_skin();
    check(tuned_skin.enabled && tuned_skin.selection.base_color ==
              nfsu::skin_pack_paint(200, 30, 30, 128) &&
              tuned_skin.selection.vinyl_layers[0] &&
              tuned_skin.selection.vinyl_layers[0]->texture == "TRIBAL5" &&
              tuned_skin.selection.doorline &&
              tuned_skin.selection.doorline->variant ==
                  nfsu::SkinDoorlineVariant::body_kit,
          "tuned preset has non-default paint, a vinyl and kit doorline");
}

// The synthetic stock car end to end: every input read and clean, every
// kind of placement drawn, a texture from each of the three sources, the
// encoding byte for byte what write_frame_tga writes, and the same hash on
// a second run.
void test_fixture_frame() {
    FixtureDirectory directory("frame");
    std::string error;
    check(write_fixture(directory.root, fixture_files(), error), "fixture: " + error);

    auto run = std::make_unique<Run>();
    run_frame(directory.root, *run);
    check(run->read && run->prepared && run->rendered && run->encoded,
          "the synthetic frame is made: " + run->error);
    if (!run->encoded) {
        return;
    }
    const nfsu::StockFrame& frame = run->frame;
    check(frame.inputs_ok && frame.issues.empty(), "the synthetic inputs are clean");
    for (const std::string& issue : frame.issues) {
        std::cerr << "  issue: " << issue << '\n';
    }

    const nfsu::StockSceneReport& report = frame.report;
    check(report.installed == 7 && report.drawn == 7 && report.fell_back == 0,
          "seven slots installed and drawn");
    check(report.slots[17].placement == nfsu::StockPlacement::wheel_table &&
              report.slots[17].instance_count == 4 &&
              report.slots[16].placement == nfsu::StockPlacement::wheel_table &&
              report.slots[16].instance_count == 4,
          "the wheel and the brake at the four culling placements");
    check(report.slots[15].placement == nfsu::StockPlacement::marker &&
              report.slots[15].instance_count == 2 &&
              report.slots[18].placement == nfsu::StockPlacement::marker &&
              report.slots[18].instance_count == 1,
          "the exhaust at both exhaust markers, the plate at its marker");
    check(report.slots[18].geometry_source == 2 && report.slots[16].geometry_source == 1,
          "the plate from PLATES, the brake from BRAKES");
    check(frame.stats.instances == 14 && frame.stats.instances_stand_in == 8,
          "14 drawn copies, the 8 wheel-table ones stand-ins");

    // The decode: the car's pack wins the hash two sources hold, each
    // shared source contributes its own, and nothing unwanted is decoded.
    const nfsu::RenderTexture* car = texture_with_hash(frame.textures, nfsu::bin_hash(kCarTexture));
    const nfsu::RenderTexture* global =
        texture_with_hash(frame.textures, nfsu::bin_hash(kGlobalTexture));
    const nfsu::RenderTexture* shared =
        texture_with_hash(frame.textures, nfsu::bin_hash(kSharedTexture));
    check(car != nullptr && car->valid && car->source == 0, "the car's texture from source 0");
    check(global != nullptr && global->valid && global->source == 1,
          "the GLOBALB texture from source 1");
    check(shared != nullptr && shared->valid && shared->source == 2,
          "the Cars/TEXTURES.BIN texture from source 2");
    check(texture_with_hash(frame.textures, nfsu::bin_hash(kUnusedTexture)) == nullptr &&
              frame.textures.not_wanted >= 1,
          "a texture no stock mesh uses is not decoded");
    check(frame.textures.shadowed.size() == 2,
          "the car's pack and GLOBALB.BUN each shadow a copy in Cars/TEXTURES.BIN");
    check(frame.wanted.size() == 5, "five texture hashes wanted, 240SX_SKIN1 among them");
    check(!frame.skinned && frame.skin_binding.hashes.empty() &&
              nfsu::stock_frame_skin_lines(frame).empty() &&
              texture_with_hash(frame.textures, nfsu::bin_hash(kBodySkin)) == nullptr,
          "the unpainted frame has no skin, and nothing claims 240SX_SKIN1");
    check(run->started && run->view.at_default() && !run->view.needs_render(),
          "the frame was rendered through the view, at the default camera");
    check(frame.stats.groups_resolved > 0 && frame.stats.groups_unresolved > 0 &&
              frame.stats.pixels_drawn > 20000 && frame.stats.pixels_fallback > 0 &&
              frame.stats.pixels_fallback < frame.stats.pixels_drawn,
          "textured and fallback pixels both drawn");

    // The encoding: the TGA write_frame_tga publishes, byte for byte.
    const nfsu::Frame& image = frame.frame;
    check(image.width() == 1280 && image.height() == 720, "1280x720");
    check(run->image.tga.size() == nfsu::kStock240sxFrameBytes, "the TGA size");
    const fs::path written = directory.root / "written.tga";
    nfsu::FrameWriteResult write;
    check(nfsu::write_frame_tga(image, written, {}, write), "write_frame_tga: " + write.error);
    std::ifstream in(written, std::ios::binary);
    const std::string on_disk((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    check(on_disk == run->image.tga, "the encoding is the file write_frame_tga writes");
    check(nfsu::sha256(on_disk.data(), on_disk.size()) == run->image.sha256,
          "the digest is the file's");

    // The same inputs give the same frame.
    auto again = std::make_unique<Run>();
    run_frame(directory.root, *again);
    check(again->encoded && again->image.sha256 == run->image.sha256, "a second run agrees");
}

// Each input missing in turn: that one is named, with the loaders' message,
// every other is read, and nothing is prepared.
void test_missing_inputs() {
    const std::vector<Bytes> files = fixture_files();
    for (std::size_t missing = 0; missing < nfsu::kStockFrameFileCount; ++missing) {
        FixtureDirectory directory("missing" + std::to_string(missing));
        std::string error;
        check(write_fixture(directory.root, files, error, missing), "fixture: " + error);
        auto run = std::make_unique<Run>();
        run->read = nfsu::read_stock_frame_inputs(directory.root, run->inputs);
        check(!run->read, "a missing input fails the read");
        for (std::size_t i = 0; i < nfsu::kStockFrameFileCount; ++i) {
            const nfsu::StockFrameInput& input = run->inputs.files[i];
            check(input.path == nfsu::stock_frame_file_path(i), "the input is named");
            if (i == missing) {
                check(!input.read && input.error == "failed to open file" && input.bytes.empty(),
                      "missing " + input.path + " is reported");
            } else {
                check(input.read && input.error.empty() && input.bytes == files[i],
                      "present " + input.path + " is read whole");
            }
        }
        check(!nfsu::prepare_stock_frame(run->inputs, nfsu::stock_240sx_request(), run->frame,
                                         error) &&
                  error == "1 of 6 input files could not be read",
              "nothing is prepared from a partial set: " + error);
    }
    // A root that does not exist: all six named.
    auto run = std::make_unique<Run>();
    check(!nfsu::read_stock_frame_inputs(unique_directory("absent"), run->inputs),
          "an absent root");
    for (const nfsu::StockFrameInput& input : run->inputs.files) {
        check(!input.read && input.error == "failed to open file", "absent " + input.path);
    }
}

// Each input replaced by garbage, by a truncated copy and by an empty file:
// the run never throws or crashes, and whatever it concludes, a parser's
// complaint names the file it is about.
void test_damaged_inputs() {
    const std::vector<Bytes> files = fixture_files();
    for (std::size_t damaged = 0; damaged < nfsu::kStockFrameFileCount; ++damaged) {
        const std::string path = nfsu::stock_frame_file_path(damaged);
        for (int kind = 0; kind < 3; ++kind) {
            std::vector<Bytes> copy = files;
            if (kind == 0) {
                for (std::size_t i = 0; i < copy[damaged].size(); ++i) {
                    copy[damaged][i] = static_cast<unsigned char>((i * 131U + 7U) & 0xFFU);
                }
            } else if (kind == 1) {
                copy[damaged].resize(copy[damaged].size() / 2);
            } else {
                copy[damaged].clear();
            }
            FixtureDirectory directory("damaged" + std::to_string(damaged) + "_" +
                                       std::to_string(kind));
            std::string error;
            check(write_fixture(directory.root, copy, error), "fixture: " + error);
            auto run = std::make_unique<Run>();
            try {
                run_frame(directory.root, *run);
            } catch (...) {
                check(false, "a damaged " + path + " threw");
                continue;
            }
            const std::string what = path + " damaged (" + std::to_string(kind) + ")";
            check(run->read, what + ": every file is still read");
            check(!run->frame.inputs_ok || !run->prepared, what + ": not clean");
            check(has_issue_for(run->frame, path) || (!run->prepared && !run->error.empty()),
                  what + ": the damage is reported");
            if (!run->frame.issues.empty()) {
                check(has_issue_for(run->frame, path), what + ": the issue names " + path);
            }
        }
    }
}

// The lines the viewer prints, from a report with a different value in
// every field, so that a line reading the wrong field cannot pass by luck.
void test_stats_lines() {
    nfsu::RenderStats s;
    s.meshes = 1;
    s.meshes_rendered = 2;
    s.meshes_skipped_invalid = 3;
    s.material_groups = 4;
    s.groups_resolved = 5;
    s.groups_unresolved = 6;
    s.groups_unusable = 7;
    s.groups_invalid = 8;
    s.unresolved_hashes = 9;
    s.triangles_submitted = 10;
    s.triangles_resolved = 11;
    s.triangles_fallback = 12;
    s.triangles_bad_index = 13;
    s.triangles_degenerate_source = 14;
    s.triangles_outside = 15;
    s.triangles_clipped = 16;
    s.triangles_degenerate_screen = 17;
    s.triangles_culled = 18;
    s.triangles_front = 19;
    s.triangles_back = 20;
    s.triangles_rasterized = 21;
    s.raster_triangles = 22;
    s.pixel_tests = 23;
    s.fragments = 24;
    s.fragments_alpha_discarded = 25;
    s.fragments_far_discarded = 26;
    s.fragments_depth_failed = 27;
    s.fragments_written = 28;
    s.pixels_drawn = 29;
    s.pixels_fallback = 30;
    s.min_x = 31;
    s.max_x = 32;
    s.min_y = 33;
    s.max_y = 34;
    s.instances = 35;
    s.instances_exact = 36;
    s.instances_stand_in = 37;
    const std::vector<std::string> expected = {
        "meshes: 1 rendered 2 skipped_invalid 3",
        "material groups: 4 resolved 5 unresolved 6 unusable 7 invalid 8",
        "unresolved hashes: 9",
        "triangles submitted: 10 textured 11 fallback 12",
        "triangles bad_index: 13 degenerate_source: 14",
        "triangles outside: 15 clipped: 16 degenerate_screen: 17 culled: 18",
        "triangles front: 19 back: 20 rasterized: 21 raster_pieces: 22",
        "pixel tests: 23",
        "fragments: 24 alpha_discarded 25 far_discarded 26 depth_failed 27 written 28",
        "pixels drawn: 29 fallback 30",
        "drawn bounds: x 31..32 y 33..34",
        "instances: 35 exact 36 stand_in 37"};
    check(nfsu::stock_frame_stats_lines(s) == expected, "the stats lines");
    s.pixels_drawn = 0;
    check(nfsu::stock_frame_stats_lines(s)[10] == "drawn bounds: none",
          "no pixels, no bounds");

    nfsu::StockSceneReport report;
    report.installed = 1;
    report.with_model = 2;
    report.drawn = 3;
    report.fell_back = 4;
    report.markers.resize(5);
    check(nfsu::stock_frame_slots_line(report) ==
              "stock slots: installed 1 with_model 2 drawn 3 by_fallback 4 markers 5",
          "the slots line");
}

void test_encode_refuses_an_empty_frame() {
    nfsu::StockFrameImage image;
    image.tga = "stale";
    std::string error;
    check(!nfsu::encode_stock_frame(nfsu::Frame{}, image, error) && error == "frame is empty" &&
              image.tga.empty(),
          "an empty frame is not encoded");
}

// ---------------------------------------------------------------------------
// The painted frame and the console's controller session (issue #148).
// ---------------------------------------------------------------------------

// The six files as read_stock_frame_inputs leaves them once each was read.
void fill_inputs(const std::vector<Bytes>& files, nfsu::StockFrameInputs& inputs) {
    for (std::size_t i = 0; i < nfsu::kStockFrameFileCount; ++i) {
        nfsu::StockFrameInput& input = inputs.files[i];
        input.path = nfsu::stock_frame_file_path(i);
        input.read = true;
        input.error.clear();
        input.bytes = files[i];
    }
}

// The SHA-256 of a frame's TGA encoding, or why there is none.
std::string frame_digest(const nfsu::Frame& frame) {
    nfsu::StockFrameImage image;
    std::string error;
    if (!nfsu::encode_stock_frame(frame, image, error)) {
        return "not encoded: " + error;
    }
    return nfsu::sha256_hex(image.sha256);
}

bool has_issue_starting(const nfsu::StockFrame& frame, const std::string& prefix) {
    for (const std::string& issue : frame.issues) {
        if (issue.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

// The synthetic car painted: the skin is composed from the car's doorline
// and the shared target, bound under both hashes, and the body's groups,
// which fall back without it, resolve to it. The rest of the frame - the
// geometry, the instances - is the unpainted one's.
void test_painted_fixture_frame() {
    FixtureDirectory directory("painted");
    std::string error;
    check(write_fixture(directory.root, fixture_files(), error), "fixture: " + error);

    auto painted = std::make_unique<Run>();
    run_frame(directory.root, *painted, true);
    check(painted->encoded, "the painted synthetic frame is made: " + painted->error);
    auto plain = std::make_unique<Run>();
    run_frame(directory.root, *plain, false);
    check(plain->encoded, "the unpainted synthetic frame is made: " + plain->error);
    if (!painted->encoded || !plain->encoded) {
        return;
    }
    const nfsu::StockFrame& frame = painted->frame;
    check(frame.inputs_ok && frame.issues.empty(), "the painted inputs are clean");
    for (const std::string& issue : frame.issues) {
        std::cerr << "  issue: " << issue << '\n';
    }
    check(frame.skinned, "the frame is skinned");
    check(frame.skin_sources ==
              std::vector<std::string>{"car-textures", "shared-1", "shared-2"},
          "the skin searches the car's pack, then GLOBALB.BUN, then Cars/TEXTURES.BIN");
    check(frame.skin.path == nfsu::SkinTargetType::type32 && frame.skin.width == 4 &&
              frame.skin.height == 4 && frame.skin.layers == 1 &&
              frame.skin.rgba.size() == 64,
          "a 4x4 type-32 skin of one layer");
    const nfsu::BodySkinInputPlan& plan = frame.skin_plan;
    check(plan.car_name == "240SX" && plan.inputs.size() == 1 && plan.usable_inputs == 1 &&
              plan.skipped_inputs == 0 &&
              plan.inputs[0].role == nfsu::SkinSourceRole::doorline &&
              plan.inputs[0].source_name == kDoorline &&
              plan.inputs[0].mask_name == kDoorlineMask &&
              plan.inputs[0].source.source_index == 0,
          "the one input is the plain doorline, from the car's own pack");
    check(plan.target_texture.resolved && plan.target_texture.source_index == 2 &&
              plan.target_texture.stored_name == kSkinTarget,
          "the target is DUMMY_SKIN1, from skin source 2, Cars/TEXTURES.BIN");
    check(plan.base_color == nfsu::skin_pack_paint(30, 60, 160, 128) &&
              plan.special_color_defaulted == std::array<bool, 4>{true, true, true, true},
          "the base paint, and every special colour falling back to it");

    const nfsu::RuntimeSkinBinding& binding = frame.skin_binding;
    check(binding.hashes == std::vector<std::uint32_t>{0x530B82B0U, 0x77A20245U} &&
              nfsu::bin_hash(kSkinTarget) == 0x530B82B0U &&
              nfsu::bin_hash(kBodySkin) == 0x77A20245U,
          "bound under DUMMY_SKIN1 and 240SX_SKIN1, in that order");
    check(binding.source == 3 && binding.bytes == 64 && binding.displaced.empty() &&
              binding.displaced_claims.empty() && binding.preserved_claims.empty() &&
              binding.texture_index + 1 == frame.textures.textures.size(),
          "as texture source 3, the last texture, displacing nothing");
    const nfsu::RenderTexture& generated = frame.textures.textures.back();
    check(generated.valid && generated.source == 3 && generated.rgba == frame.skin.rgba,
          "the last texture is the generated skin");

    const nfsu::RenderStats& paint = frame.stats;
    const nfsu::RenderStats& bare = plain->frame.stats;
    check(paint.unresolved_hashes + 1 == bare.unresolved_hashes &&
              paint.groups_resolved > bare.groups_resolved &&
              paint.pixels_fallback < bare.pixels_fallback &&
              paint.pixels_drawn == bare.pixels_drawn && paint.instances == bare.instances &&
              paint.triangles_submitted == bare.triangles_submitted,
          "240SX_SKIN1 resolves once the skin is bound, and nothing else changes shape");
    check(painted->image.sha256 != plain->image.sha256, "the painted frame is another frame");

    const std::vector<std::string> lines = nfsu::stock_frame_skin_lines(frame);
    const std::vector<std::string> expected = {
        "skin: car \"240SX\" path type32 4x4 layers 1 owned_bytes " +
            std::to_string(frame.skin_owned_bytes) + " skipped_inputs 0",
        "skin bound: hash 0x530B82B0 \"DUMMY_SKIN1\"",
        "skin bound: hash 0x77A20245 \"240SX_SKIN1\"",
        "skin generated: texture " + std::to_string(binding.texture_index) +
            " source 3 bytes 64"};
    check(lines == expected, "the skin lines");
    check(frame.skin_owned_bytes > 0, "the assembly owned its layer");

    auto again = std::make_unique<Run>();
    run_frame(directory.root, *again, true);
    check(again->encoded && again->image.sha256 == painted->image.sha256,
          "a second painted run agrees");
}

// The two ways the skin stops short, each as the viewer's --skin-car does:
// no target is fatal, a skipped input is an issue.
void test_skin_refusals() {
    const std::size_t shared =
        static_cast<std::size_t>(nfsu::StockFrameFile::shared_car_textures);
    const std::size_t car = static_cast<std::size_t>(nfsu::StockFrameFile::car_textures);
    {
        std::vector<Bytes> files = fixture_files();
        files[shared] = shared_car_textures(false);
        auto run = std::make_unique<Run>();
        fill_inputs(files, run->inputs);
        std::string error;
        const bool prepared =
            nfsu::prepare_stock_frame(run->inputs, nfsu::stock_240sx_request(),
                                      nfsu::stock_240sx_skin(), run->frame, error);
        // The plan takes the target's size from the target record, so with
        // no record the size is zero and the plan refuses; the viewer prints
        // "error: skin inputs: the target has a zero dimension" and exits 2.
        check(!prepared && error == "skin inputs: the target has a zero dimension" &&
                  !run->frame.skinned,
              "no DUMMY_SKIN1 anywhere: refused, as the viewer refuses it: " + error);
        const bool unpainted = nfsu::prepare_stock_frame(
            run->inputs, nfsu::stock_240sx_request(), run->frame, error);
        check(unpainted, "the same files still make the unpainted frame: " + error);
    }
    {
        std::vector<Bytes> files = fixture_files();
        files[car] = car_textures(false);
        auto run = std::make_unique<Run>();
        fill_inputs(files, run->inputs);
        std::string error;
        const bool prepared =
            nfsu::prepare_stock_frame(run->inputs, nfsu::stock_240sx_request(),
                                      nfsu::stock_240sx_skin(), run->frame, error);
        check(prepared, "no doorline: the skin is still composed: " + error);
        check(run->frame.skinned && run->frame.skin.layers == 0 &&
                  run->frame.skin_plan.skipped_inputs == 1 && !run->frame.inputs_ok &&
                  has_issue_starting(run->frame,
                                     "skin: input doorline skipped source_not_found"),
              "the skipped doorline is an issue and the inputs are not clean");
    }
}

// The view renders exactly what render_stock_frame renders at
// RenderOptions{}: the same bytes, so the same hash.
void test_view_renders_the_stock_frame() {
    auto run = std::make_unique<Run>();
    fill_inputs(fixture_files(), run->inputs);
    std::string error;
    check(nfsu::prepare_stock_frame(run->inputs, nfsu::stock_240sx_request(), run->frame, error),
          "prepared: " + error);
    check(nfsu::render_stock_frame(run->frame, nfsu::RenderOptions{}, error),
          "render_stock_frame: " + error);
    check(run->view.start(run->frame.scene, error), "the view starts: " + error);
    nfsu::Frame through_view;
    nfsu::RenderStats stats;
    check(run->view.render(through_view, stats, error), "the view renders: " + error);
    check(through_view.rgba() == run->frame.frame.rgba() &&
              frame_digest(through_view) == frame_digest(run->frame.frame) &&
              stats.pixels_drawn == run->frame.stats.pixels_drawn,
          "the view's frame is render_stock_frame's, byte for byte");
}

// What drew one pixel of a rendered stock frame.
struct PixelOwner {
    bool drawn = false;
    std::uint32_t mesh_hash = 0;
    std::size_t material = 0;
    std::uint32_t slot = 0;
    std::uint32_t copy = 0;
};

// The owner of the pixel a point lands on, the point given in the raw slot
// frame (longitudinal, lateral, vertical) and projected with the camera
// the frame was rendered with. The group comes from the frame's triangle
// id and the ranges prepared_triangle_ranges gives for the order
// render_scene submitted in; the copy from the id's position inside that
// group's range, which holds one run of triangles per drawn copy in the
// order prepared_mesh_instances gives.
bool owner_at(const nfsu::StockFrame& frame, const nfsu::RenderOptions& options,
              const float (&point)[3], PixelOwner& owner, std::string& error) {
    owner = PixelOwner{};
    nfsu::CameraFrame camera;
    if (!nfsu::make_camera_frame(options.camera, options.width, options.height, camera,
                                 error)) {
        return false;
    }
    const nfsu::ClipVertex clip =
        nfsu::project_point(camera, nfsu::Vec3{point[0], point[2], -static_cast<double>(point[1])});
    if (!(clip.w > 0.0)) {
        error = "the point is behind the camera";
        return false;
    }
    const double sx = (clip.x / clip.w + 1.0) * 0.5 * options.width;
    const double sy = (1.0 - clip.y / clip.w) * 0.5 * options.height;
    if (!(sx >= 0.0 && sy >= 0.0 && sx < options.width && sy < options.height)) {
        error = "the point is outside the frame";
        return false;
    }
    const std::size_t pixel = static_cast<std::size_t>(std::floor(sy)) * options.width +
                              static_cast<std::size_t>(std::floor(sx));
    const std::uint32_t id = frame.frame.ids()[pixel];
    if (id == nfsu::kNoTriangle) {
        return true;
    }
    std::vector<std::uint32_t> order;
    std::vector<nfsu::PreparedTriangleRange> ranges;
    nfsu::PreparedMeshInstances copies;
    if (!nfsu::prepared_draw_order(frame.scene,
                                   options.material_state == nfsu::MaterialStateMode::retail,
                                   order, nullptr, error) ||
        !nfsu::prepared_triangle_ranges(frame.scene, order, ranges, error) ||
        !nfsu::prepared_mesh_instances(frame.scene, copies, error)) {
        return false;
    }
    for (const nfsu::PreparedTriangleRange& range : ranges) {
        if (id < range.first_id || id - range.first_id >= range.count) {
            continue;
        }
        const nfsu::PreparedDraw& draw = frame.scene.draws[range.draw];
        const std::uint32_t per_copy = draw.index_count / 3;
        const std::size_t copy = (id - range.first_id) / per_copy;
        const nfsu::PreparedInstance& instance =
            frame.scene.instances[copies.order[copies.first[draw.mesh] + copy]];
        owner.drawn = true;
        owner.mesh_hash = instance.mesh_hash;
        owner.material = draw.material_index;
        owner.slot = instance.part_slot;
        owner.copy = instance.copy;
        return true;
    }
    error = "no range holds triangle " + std::to_string(id);
    return false;
}

// Issue #161. The owner orbited the painted stock car to its left side and
// saw both wheels there drawn as flat dark squares: the wheel model is
// authored for the right-hand side, and the stand-in placed the left
// wheels without the half turn the retail draw gives wheels 0 and 3, so a
// camera on the left saw each left wheel's inboard backing quad in front
// of its rim. With a retail-shaped wheel, a camera square to either side
// must see the near-side wheels' rims.
void test_wheels_face_out_on_both_sides() {
    std::vector<Bytes> files = fixture_files();
    files[static_cast<std::size_t>(nfsu::StockFrameFile::car_geometry)] = car_geometry(true);
    auto run = std::make_unique<Run>();
    fill_inputs(files, run->inputs);
    std::string error;
    check(nfsu::prepare_stock_frame(run->inputs, nfsu::stock_240sx_request(), run->frame, error),
          "the handed-wheel car is prepared: " + error);
    check(run->frame.inputs_ok, "the handed-wheel inputs are clean");
    if (run->frame.scene.instances.empty()) {
        return;
    }
    struct Side {
        const char* name;
        double yaw;
        std::size_t wheels[2];
    };
    // yaw -90 puts the eye on the car's left, where lateral is positive;
    // wheels 0 and 3 are the left pair of write_car_types.
    const Side sides[] = {{"left", -90.0, {0, 3}}, {"right", 90.0, {1, 2}}};
    for (const Side& side : sides) {
        nfsu::RenderOptions options;
        options.camera.yaw_deg = side.yaw;
        options.camera.pitch_deg = 0.0;
        check(nfsu::render_stock_frame(run->frame, options, error),
              std::string("rendered from the ") + side.name + ": " + error);
        for (const std::size_t w : side.wheels) {
            // A point in the plane of the wheel's centre, 0.22 below it:
            // inside the 0.5 square rim, and on a line of sight that passes
            // under the brake box (0.18 about the centre, 0.12 either side
            // of that plane) and under the base and the body. On the old
            // placement the same line meets the left wheels' backing quad
            // first, 0.2 outboard of the rim.
            const float* centre = run->frame.report.placements[w].position;
            const float point[3] = {centre[0], centre[1], centre[2] - 0.22F};
            PixelOwner owner;
            const std::string what =
                std::string("from the ") + side.name + ", wheel " + std::to_string(w);
            check(owner_at(run->frame, options, point, owner, error), what + ": " + error);
            check(owner.drawn && owner.mesh_hash == kWheelA && owner.slot == 17 &&
                      owner.copy == w,
                  what + " is what the camera sees there");
            check(owner.material == 0,
                  what + " shows its rim (material 0), not its backing quad (material " +
                      std::to_string(owner.material) + ")");
        }
    }
}

// docs/INPUT.md's table, written out here by bit number rather than taken
// from the constants the mapping itself uses.
void test_npad_mapping() {
    struct Row {
        unsigned bit;
        nfsu::GamepadButton button;
    };
    const Row rows[] = {
        {0, nfsu::GamepadButton::face_east},       {1, nfsu::GamepadButton::face_south},
        {2, nfsu::GamepadButton::face_north},      {3, nfsu::GamepadButton::face_west},
        {4, nfsu::GamepadButton::stick_left},      {5, nfsu::GamepadButton::stick_right},
        {6, nfsu::GamepadButton::shoulder_left},   {7, nfsu::GamepadButton::shoulder_right},
        {8, nfsu::GamepadButton::trigger_left},    {9, nfsu::GamepadButton::trigger_right},
        {10, nfsu::GamepadButton::start},          {11, nfsu::GamepadButton::select},
        {12, nfsu::GamepadButton::dpad_left},      {13, nfsu::GamepadButton::dpad_up},
        {14, nfsu::GamepadButton::dpad_right},     {15, nfsu::GamepadButton::dpad_down}};
    std::uint32_t all = 0;
    for (const Row& row : rows) {
        const std::uint32_t got = nfsu::gamepad_buttons_from_npad(1ULL << row.bit);
        check(got == nfsu::gamepad_button_bit(row.button),
              "npad bit " + std::to_string(row.bit) + " is " +
                  nfsu::gamepad_button_name(row.button));
        check((all & got) == 0, "npad bit " + std::to_string(row.bit) + " is a new button");
        all |= got;
    }
    check(all == nfsu::kGamepadButtonMask, "the sixteen bits are the sixteen buttons");
    check(nfsu::gamepad_buttons_from_npad(0xFFFFULL) == nfsu::kGamepadButtonMask,
          "all sixteen at once");
    for (unsigned bit = 16; bit < 64; ++bit) {
        check(nfsu::gamepad_buttons_from_npad(1ULL << bit) == 0,
              "npad bit " + std::to_string(bit) + " is no button");
    }
    check(nfsu::gamepad_buttons_from_npad(~0ULL) == nfsu::kGamepadButtonMask,
          "every bit set is the sixteen buttons and nothing more");
    check(nfsu::kNpadButtonA == 1ULL && nfsu::kNpadButtonPlus == 1ULL << 10U &&
              nfsu::kNpadButtonMinus == 1ULL << 11U && nfsu::kNpadButtonDown == 1ULL << 15U,
          "the constants are libnx's bit numbers");
}

void test_view_bindings() {
    const std::vector<nfsu::InputBinding> bindings = nfsu::stock_view_bindings();
    std::string error;
    check(nfsu::validate_input_bindings(nfsu::camera_action_kinds(), bindings, error),
          "the control table validates: " + error);
    const auto find = [&bindings](nfsu::GamepadControl control) -> const nfsu::InputBinding* {
        for (const nfsu::InputBinding& binding : bindings) {
            if (binding.control.kind == control.kind && binding.control.index == control.index) {
                return &binding;
            }
        }
        return nullptr;
    };
    const auto bound_to = [&find](nfsu::GamepadControl control, std::uint16_t action) {
        const nfsu::InputBinding* binding = find(control);
        return binding != nullptr && binding->action == action && !binding->invert;
    };
    check(bindings.size() == 4, "four controls are bound");
    check(bound_to(nfsu::gamepad_control(nfsu::GamepadAxis::left_x),
                   nfsu::camera_action_id(nfsu::CameraAction::orbit_x)) &&
              bound_to(nfsu::gamepad_control(nfsu::GamepadAxis::left_y),
                       nfsu::camera_action_id(nfsu::CameraAction::orbit_y)),
          "the left stick orbits");
    check(bound_to(nfsu::gamepad_control(nfsu::GamepadButton::select),
                   nfsu::camera_command_action(nfsu::InspectorCommand::reset_camera)),
          "- resets the camera");
    check(bound_to(nfsu::gamepad_control(nfsu::GamepadButton::start),
                   nfsu::camera_action_id(nfsu::CameraAction::exit_session)),
          "+ exits");
    check(find(nfsu::gamepad_control(nfsu::GamepadAxis::right_x)) == nullptr &&
              find(nfsu::gamepad_control(nfsu::GamepadAxis::right_y)) == nullptr &&
              find(nfsu::gamepad_control(nfsu::GamepadButton::trigger_left)) == nullptr &&
              find(nfsu::gamepad_control(nfsu::GamepadButton::dpad_up)) == nullptr,
          "the right stick, the triggers and the D-pad are not bound");
}

void test_default_check() {
    check(nfsu::stock_view_is_default(nfsu::RenderOptions{}), "RenderOptions{} is the default");
    int changed = 0;
    const auto differs = [&changed](const std::string& what, auto change) {
        nfsu::RenderOptions options;
        change(options);
        check(!nfsu::stock_view_is_default(options), what + " changed is not the default");
        ++changed;
    };
    using O = nfsu::RenderOptions;
    differs("width", [](O& o) { o.width = 1279; });
    differs("height", [](O& o) { o.height = 721; });
    differs("yaw by one ulp", [](O& o) { o.camera.yaw_deg = std::nextafter(35.0, 36.0); });
    differs("pitch", [](O& o) { o.camera.pitch_deg = 16.0; });
    differs("distance", [](O& o) { o.camera.distance = 6.0; });
    differs("target x", [](O& o) { o.camera.target.x = 0.0; });
    differs("target y", [](O& o) { o.camera.target.y = 0.0; });
    differs("target z", [](O& o) { o.camera.target.z = 1.0; });
    differs("fov", [](O& o) { o.camera.fov_y_deg = 40.0; });
    differs("near", [](O& o) { o.camera.near_plane = 0.1; });
    differs("far", [](O& o) { o.camera.far_plane = 50.0; });
    differs("flip_v", [](O& o) { o.flip_v = true; });
    differs("alpha_cutoff", [](O& o) { o.alpha_cutoff = 1; });
    differs("vertex_color", [](O& o) { o.vertex_color = true; });
    differs("cull", [](O& o) { o.cull = nfsu::CullMode::back; });
    differs("shading", [](O& o) { o.shading = nfsu::ShadingMode::none; });
    differs("material_state", [](O& o) { o.material_state = nfsu::MaterialStateMode::retail; });
    differs("alpha_min", [](O& o) { o.coefficients.alpha_min = 0.5; });
    differs("alpha_range", [](O& o) { o.coefficients.alpha_range = 0.5; });
    for (int k = 0; k < 3; ++k) {
        const std::string channel = std::to_string(k);
        differs("environment_min " + channel,
                [k](O& o) { o.coefficients.environment_min[k] = 1.0; });
        differs("environment_range " + channel,
                [k](O& o) { o.coefficients.environment_range[k] = 1.0; });
        differs("environment_color " + channel,
                [k](O& o) { o.coefficients.environment_color[k] = 1; });
    }
    differs("environment_power", [](O& o) { o.coefficients.environment_power = 2; });
    differs("environment_enabled", [](O& o) { o.coefficients.environment_enabled = false; });
    for (int k = 0; k < 4; ++k) {
        differs("background " + std::to_string(k), [k](O& o) { o.background[k] = 0; });
    }
    check(changed == 34, "every field was changed once");
}

// A recorded session over the synthetic car, frame by frame, as the
// console's loop runs it: step, then render while the frame is due, then
// compare the hash only at the default camera.
void test_view_session() {
    auto run = std::make_unique<Run>();
    fill_inputs(fixture_files(), run->inputs);
    std::string error;
    check(nfsu::prepare_stock_frame(run->inputs, nfsu::stock_240sx_request(),
                                    nfsu::stock_240sx_skin(), run->frame, error),
          "prepared: " + error);
    nfsu::StockView& view = run->view;

    // Before a scene is bound nothing moves and nothing renders.
    check(!view.needs_render() && !view.step(nfsu::GamepadFrame{}).exit,
          "no scene: no session");
    nfsu::Frame nothing;
    nfsu::RenderStats nothing_stats;
    check(!view.render(nothing, nothing_stats, error) && error == "no scene to render",
          "no scene: no render");

    check(nfsu::render_stock_frame(run->frame, nfsu::RenderOptions{}, error),
          "the pinned camera's frame: " + error);
    const std::string pinned = frame_digest(run->frame.frame);

    check(view.start(run->frame.scene, error), "the view starts: " + error);
    check(view.needs_render() && view.at_default(), "the first frame is due, at the default");

    const std::string text =
        "nfsu-input 1\n"
        "# + still held from launching the application: latched, no exit\n"
        "0 0 start 0 0 0 0\n"
        "# + released and the left stick fully right for 16 ms: an orbit\n"
        "16000 0 - 32767 0 0 0\n"
        "# the right stick alone, which is not bound: nothing\n"
        "32000 0 - 0 0 32767 -32767\n"
        "# the D-pad, not bound either, and the left stick fully up\n"
        "48000 0 dpad_up 0 32767 0 0\n"
        "# five seconds later, the left stick fully left: 100 ms of orbit\n"
        "5048000 0 - -32767 0 0 0\n"
        "# -: back to the default camera\n"
        "5064000 0 select 0 0 0 0\n"
        "# +: leave\n"
        "5080000 0 start 0 0 0 0\n";
    std::vector<nfsu::GamepadFrame> frames;
    check(nfsu::parse_input_recording(text, frames, error) && frames.size() == 7,
          "the recording parses: " + error);
    if (frames.size() != 7) {
        return;
    }
    nfsu::Frame shown;
    nfsu::RenderStats stats;
    const auto render = [&]() {
        std::string render_error;
        if (!view.render(shown, stats, render_error)) {
            return "render failed: " + render_error;
        }
        return frame_digest(shown);
    };
    const auto close_to = [](double a, double b) { return std::fabs(a - b) < 1e-9; };

    nfsu::StockViewStep step = view.step(frames[0]);
    check(!step.exit && !step.camera.moving && view.needs_render() && view.at_default(),
          "frame 0: + held since launch does not exit");
    check(render() == pinned && !view.needs_render(),
          "frame 0: the default camera's frame is the pinned camera's");

    step = view.step(frames[1]);
    const double yaw_1 = view.options().camera.yaw_deg;
    check(!step.exit && step.camera.moving && view.needs_render() && !view.at_default(),
          "frame 1: the left stick moves the camera away from the default");
    check(close_to(yaw_1, 35.0 - 120.0 * 0.016) && view.options().camera.pitch_deg == 15.0,
          "frame 1: right lowers yaw by 120 degrees a second for 16 ms");
    const std::string moved = render();
    check(moved != pinned && moved.size() == 64, "frame 1: another frame is rendered");

    step = view.step(frames[2]);
    check(!step.camera.moving && !view.needs_render() &&
              view.options().camera.yaw_deg == yaw_1,
          "frame 2: the right stick does nothing");

    step = view.step(frames[3]);
    const double pitch_3 = view.options().camera.pitch_deg;
    check(step.camera.commands.empty() && view.options().camera.yaw_deg == yaw_1 &&
              close_to(pitch_3, 15.0 + 120.0 * 0.016) && view.needs_render(),
          "frame 3: the D-pad snaps no view, and up raises the pitch");
    render();

    step = view.step(frames[4]);
    check(close_to(view.options().camera.yaw_deg, yaw_1 + 12.0) &&
              view.options().camera.pitch_deg == pitch_3,
          "frame 4: after a stall the orbit moves as 100 ms would, left raising yaw");
    render();

    step = view.step(frames[5]);
    check(step.camera.commands ==
                  std::vector<nfsu::InspectorCommand>{nfsu::InspectorCommand::reset_camera} &&
              view.at_default() && view.needs_render() && !step.exit,
          "frame 5: - returns the camera to the default, to the bit");
    check(render() == pinned, "frame 5: and the frame is the pinned camera's again");

    step = view.step(frames[6]);
    check(step.exit, "frame 6: + exits");

    // A new start forgets the controller: + held across it is latched again.
    check(view.start(run->frame.scene, error), "the view starts again: " + error);
    nfsu::GamepadFrame held = frames[6];
    held.time_us += 16000;
    check(!view.step(held).exit, "a restarted view latches a held +");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--frame") {
        return frame_mode(nfsu::argument_path(argv[2]), false);
    }
    if (argc == 3 && std::string(argv[1]) == "--painted-frame") {
        return frame_mode(nfsu::argument_path(argv[2]), true);
    }
    if (argc == 3 && std::string(argv[1]) == "--tuned-frame") {
        return frame_mode(nfsu::argument_path(argv[2]), true, true);
    }
    if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
        std::string error;
        if (!write_fixture(nfsu::argument_path(argv[2]), fixture_files(), error)) {
            std::cerr << "error: " << error << '\n';
            return 2;
        }
        std::cout << "wrote the six synthetic inputs\n";
        return 0;
    }
    if (argc != 1) {
        std::cerr << "usage: nfsu_stock_frame_tests [--frame ROOT | --painted-frame ROOT | "
                     "--write-fixture DIR]\n";
        return 1;
    }
    test_file_layout();
    test_fixture_frame();
    test_missing_inputs();
    test_damaged_inputs();
    test_stats_lines();
    test_encode_refuses_an_empty_frame();
    test_painted_fixture_frame();
    test_skin_refusals();
    test_view_renders_the_stock_frame();
    test_wheels_face_out_on_both_sides();
    test_npad_mapping();
    test_view_bindings();
    test_default_check();
    test_view_session();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "stock frame tests passed\n";
    return 0;
}
