#include "nfsu/geometry.h"
#include "nfsu/runtime_skin.h"
#include "nfsu/texture_decode.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;
long g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

std::string hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

void check_u32(std::uint32_t got, std::uint32_t want, const std::string& message) {
    ++g_checks;
    if (got != want) {
        std::cerr << "test failure: " << message << ": got " << hex(got) << " want "
                  << hex(want) << '\n';
        ++g_failures;
    }
}

void check_size(std::size_t got, std::size_t want, const std::string& message) {
    ++g_checks;
    if (got != want) {
        std::cerr << "test failure: " << message << ": got " << got << " want " << want
                  << '\n';
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// Allocation-failure injection
//
// bind_runtime_skin promises that a failed binding leaves the texture set
// exactly as it was. Every refusal it can reach by argument returns before
// it starts, so the part of that promise that carries the weight - the
// claim list rebuilt into a local and committed with a noexcept swap, and
// the single push_back the set is mutated through - is only reachable by
// making an allocation fail. The global operator new below counts
// allocations and throws on the one asked for.
//
// It is armed immediately before the call and disarmed immediately after,
// so nothing else in this file runs while it is armed, and it fires once
// and disarms itself so that the handler catching the throw can build its
// own error message.
//
// What it does not see: with a shared libstdc++, std::basic_string<char> is
// an extern template instantiated inside the library, and its allocations
// go to the library's own operator new rather than this one. So the
// allocations failed here are the container allocations - 11 of them for
// the fixture below - and not the string ones. That is enough for what this
// test is about, because the set is mutated through containers; it is
// written down so the count is not mistaken for "every allocation".
// ---------------------------------------------------------------------------

// 0 disarms; otherwise the 1-based allocation that will throw.
std::size_t g_fail_allocation = 0;
std::size_t g_allocations = 0;
// Which exception it throws. std::length_error is not something an
// allocation can really raise - a vector throws it before it allocates and
// operator new never does - so it is injected only to reach the second
// catch handler, and the test that uses it says so.
bool g_fail_with_length_error = false;

void* counted_allocate(std::size_t size) {
    if (g_fail_allocation != 0) {
        ++g_allocations;
        if (g_allocations == g_fail_allocation) {
            g_fail_allocation = 0;
            if (g_fail_with_length_error) {
                g_fail_with_length_error = false;
                throw std::length_error("injected");
            }
            throw std::bad_alloc();
        }
    }
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

// ---------------------------------------------------------------------------
// Synthetic pack fixtures
//
// The same builder tests/skin_inputs_tests.cpp uses: one JDLZ pack root per
// image, literal-only blobs, and the formats this layer accepts plus ones it
// must refuse. The layout follows docs/FORMATS.md.
// ---------------------------------------------------------------------------

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8U * i)) & 0xFFU));
    }
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

void put_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
}

Bytes jdlz_header(std::uint32_t decompressed, std::uint32_t compressed) {
    Bytes bytes = {'J', 'D', 'L', 'Z', 0x02, 0x10, 0x00, 0x00};
    append_u32(bytes, decompressed);
    append_u32(bytes, compressed);
    return bytes;
}

// Literal-only JDLZ stream: a flags2 byte first, then a zero flags1 byte
// before every group of eight literals.
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
    Bytes blob = jdlz_header(static_cast<std::uint32_t>(data.size()),
                             static_cast<std::uint32_t>(body.size() + 16));
    blob.insert(blob.end(), body.begin(), body.end());
    return blob;
}

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

private:
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

struct TexSpec {
    std::string name;
    std::optional<std::uint32_t> hash;
    std::uint8_t format_code = 0x20;
    std::uint32_t d3d_format = 21;
    std::uint16_t width = 4;
    std::uint16_t height = 4;
    // The decompressed mip chain, one level only.
    Bytes pixels;
    // Appended after align_up(pixels, 128) for P8 records.
    std::optional<Bytes> palette;
    std::optional<Bytes> blob;
    std::optional<std::uint32_t> top_size;
};

unsigned char log2_of(std::uint32_t value) {
    unsigned char result = 0;
    while ((1U << result) < value) {
        ++result;
    }
    return result;
}

bool is_p8_code(std::uint8_t code) {
    return code == 0x08 || code == 0x80 || code == 0x81;
}

Bytes build_pack(const std::vector<TexSpec>& specs) {
    Writer w;
    w.begin(nfsu::kTexturePackRootId);
    w.begin(nfsu::kTextureInfoContainerId);

    Bytes header(124, 0);
    put_u32(header, 0, 4);
    std::memcpy(&header[4], "CARTEXTURES", 11);
    const std::string path = "Test\\Skin.tpk";
    std::memcpy(&header[32], path.data(), path.size());
    put_u32(header, 96, nfsu::bin_hash(path));
    w.leaf(nfsu::kTexturePackHeaderId, header);

    std::vector<std::uint32_t> hashes;
    for (const TexSpec& spec : specs) {
        hashes.push_back(spec.hash.value_or(nfsu::bin_hash(spec.name)));
    }

    Bytes hash_table;
    for (const std::uint32_t hash : hashes) {
        append_u32(hash_table, hash);
        append_u32(hash_table, 0);
    }
    w.leaf(nfsu::kTextureHashTableId, hash_table);

    w.begin(nfsu::kTextureBlobTableId);
    std::vector<std::size_t> blob_entry_offsets;
    for (const std::uint32_t hash : hashes) {
        blob_entry_offsets.push_back(w.size());
        Bytes entry;
        append_u32(entry, hash);
        append_u32(entry, 0);
        append_u32(entry, 0);
        append_u32(entry, 0x100);
        append_u32(entry, 0);
        w.append(entry);
    }
    w.end();

    Bytes info_table;
    std::uint32_t virtual_offset = 0;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const TexSpec& spec = specs[i];
        Bytes record(124, 0);
        std::memcpy(&record[12], spec.name.data(),
                    std::min<std::size_t>(spec.name.size(), nfsu::kTextureNameSize));
        put_u32(record, 36, hashes[i]);
        put_u32(record, 40, 0x001A93CFU);
        const std::uint32_t size = static_cast<std::uint32_t>(spec.pixels.size());
        const bool p8 = is_p8_code(spec.format_code);
        put_u32(record, 48, virtual_offset);
        virtual_offset = (virtual_offset + size + 127U) & ~127U;
        put_u32(record, 52, virtual_offset);
        put_u32(record, 56, size);
        put_u32(record, 60, p8 ? static_cast<std::uint32_t>(nfsu::kP8PaletteSize) : 0U);
        if (p8) {
            virtual_offset += static_cast<std::uint32_t>(nfsu::kP8PaletteSize);
        }
        put_u32(record, 64, spec.top_size.value_or(size));
        put_u16(record, 68, spec.width);
        put_u16(record, 70, spec.height);
        record[72] = log2_of(spec.width);
        record[73] = log2_of(spec.height);
        record[74] = spec.format_code;
        record[78] = 1;
        put_u32(record, 84, 0x01000000U);
        info_table.insert(info_table.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureInfoTableId, info_table);

    Bytes format_table;
    for (const TexSpec& spec : specs) {
        Bytes record(32, 0);
        put_u32(record, 12, 5);
        put_u32(record, 16, 6);
        put_u32(record, 20, spec.d3d_format);
        format_table.insert(format_table.end(), record.begin(), record.end());
    }
    w.leaf(nfsu::kTextureFormatTableId, format_table);
    w.end();

    w.begin(nfsu::kTextureDataContainerId);
    Bytes data_header(24, 0);
    put_u32(data_header, 8, 1);
    put_u32(data_header, 12, nfsu::bin_hash(path));
    w.leaf(nfsu::kTextureDataHeaderId, data_header);
    w.begin(nfsu::kTextureDataId);
    while (w.size() % 128 != 0) {
        w.bytes().push_back(0x11);
    }
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const TexSpec& spec = specs[i];
        Bytes stored = spec.pixels;
        if (spec.palette) {
            stored.resize((stored.size() + 127U) & ~static_cast<std::size_t>(127U), 0);
            stored.insert(stored.end(), spec.palette->begin(), spec.palette->end());
        }
        const Bytes blob = spec.blob.value_or(jdlz_literals(stored));
        put_u32(w.bytes(), blob_entry_offsets[i] + 4, static_cast<std::uint32_t>(w.size()));
        put_u32(w.bytes(), blob_entry_offsets[i] + 8, static_cast<std::uint32_t>(blob.size()));
        w.append(blob);
    }
    w.end();
    w.end();
    w.end();
    w.leaf(0x00000000U, Bytes(16, 0));
    return w.bytes();
}

TexSpec argb_spec(const std::string& name, const std::vector<std::uint32_t>& pixels,
                  std::uint16_t width = 4, std::uint16_t height = 4) {
    TexSpec spec;
    spec.name = name;
    spec.format_code = 0x20;
    spec.d3d_format = 21;
    spec.width = width;
    spec.height = height;
    for (const std::uint32_t pixel : pixels) {
        append_u32(spec.pixels, pixel);
    }
    return spec;
}

// A8R8G8B8 level 0 whose pixel i is the little-endian dword seed + i.
TexSpec argb_ramp(const std::string& name, std::uint32_t seed, std::uint16_t width = 4,
                  std::uint16_t height = 4) {
    std::vector<std::uint32_t> pixels;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < count; ++i) {
        pixels.push_back(seed + static_cast<std::uint32_t>(i));
    }
    return argb_spec(name, pixels, width, height);
}

// P8 level 0 whose index i is `indices[i]`, with a palette whose entry e is
// the little-endian dword palette_seed + e.
TexSpec p8_spec(const std::string& name, std::uint8_t code, const Bytes& indices,
                std::uint32_t palette_seed, std::uint16_t width = 4,
                std::uint16_t height = 4) {
    TexSpec spec;
    spec.name = name;
    spec.format_code = code;
    spec.d3d_format = 41;
    spec.width = width;
    spec.height = height;
    spec.pixels = indices;
    Bytes palette;
    for (std::uint32_t entry = 0; entry < 256; ++entry) {
        append_u32(palette, palette_seed + entry);
    }
    spec.palette = palette;
    return spec;
}

Bytes ramp_indices(std::size_t count, unsigned char modulus) {
    Bytes out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<unsigned char>(i % modulus));
    }
    return out;
}

// One parsed image, kept alive for as long as the sources that name it.
struct Source {
    Bytes bytes;
    nfsu::TexturePack pack;
};

std::unique_ptr<Source> make_source(const std::vector<TexSpec>& specs) {
    auto source = std::unique_ptr<Source>(new Source());
    source->bytes = build_pack(specs);
    source->pack = nfsu::parse_texture_pack(source->bytes.data(), source->bytes.size());
    return source;
}

nfsu::SkinInputSource source_of(const Source& source, nfsu::SkinSourceKind kind,
                                const std::string& label) {
    nfsu::SkinInputSource out;
    out.bytes = &source.bytes;
    out.packs.push_back(&source.pack);
    out.kind = kind;
    out.path = label;
    return out;
}

// ---------------------------------------------------------------------------
// A three-source fixture in the retail shape: the car's pack, the car's
// vinyls and a shared pack that holds the composite target.
// ---------------------------------------------------------------------------

const char* const kCar = "240SX";
const char* const kTargetName = "DUMMY_SKIN1";

std::uint32_t target_hash() {
    return nfsu::bin_hash(kTargetName);
}

std::uint32_t body_hash() {
    return nfsu::bin_hash(std::string(kCar) + "_SKIN1");
}

// 4 x 4, one pixel per marker in the first row and the background elsewhere,
// which is the shape the retail target has at 512 x 512.
std::vector<std::uint32_t> target_pixels() {
    std::vector<std::uint32_t> pixels(16, 0xBF000000U);
    for (std::size_t i = 0; i < nfsu::kSkinMarkerSurface.size(); ++i) {
        pixels[i] = nfsu::kSkinMarkerSurface[i];
    }
    return pixels;
}

struct Fixture {
    std::unique_ptr<Source> car;
    std::unique_ptr<Source> vinyls;
    std::unique_ptr<Source> shared;
    std::vector<nfsu::SkinInputSource> sources;
};

Fixture make_fixture() {
    Fixture out;
    out.car = make_source({
        argb_ramp("240SX_DOORLINE", 0x10203040U),
        argb_ramp("240SX_DOORLINE_MASK", 0x00000040U),
    });
    out.vinyls = make_source({
        p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 4), 0x20000000U),
        p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 3), 0x00000030U),
    });
    out.shared = make_source({argb_spec(kTargetName, target_pixels())});
    out.sources = {source_of(*out.car, nfsu::SkinSourceKind::car_textures, "car-textures"),
                   source_of(*out.vinyls, nfsu::SkinSourceKind::car_vinyls, "car-vinyls"),
                   source_of(*out.shared, nfsu::SkinSourceKind::other, "shared-1")};
    return out;
}

nfsu::BodySkinSelection fixture_selection() {
    nfsu::BodySkinSelection selection;
    selection.car_name = kCar;
    selection.target = nfsu::SkinTargetType::type32;
    selection.target_hash = target_hash();
    nfsu::SkinVinylLayerSelection layer;
    layer.texture = "TRIBAL5";
    layer.palette_size = 16;
    selection.vinyl_layers[0] = layer;
    nfsu::SkinDoorlineSelection doorline;
    doorline.variant = nfsu::SkinDoorlineVariant::plain;
    selection.doorline = doorline;
    selection.base_color = nfsu::skin_pack_paint(0xC8, 0x1E, 0x1E, 0x80);
    return selection;
}

// Runs the whole layer the viewer runs: plan, path, assemble, target, compose.
bool run_pipeline(const nfsu::BodySkinSelection& selection,
                  const std::vector<nfsu::SkinInputSource>& sources,
                  nfsu::AssembledSkinInputs& assembled, nfsu::SkinTargetChoice& choice,
                  nfsu::RuntimeSkin& skin, std::string& error,
                  const nfsu::SkinInputLimits& limits = nfsu::SkinInputLimits{}) {
    nfsu::BodySkinInputPlan plan;
    if (!nfsu::plan_body_skin_inputs(selection, sources, limits, plan, error)) {
        return false;
    }
    if (!nfsu::choose_skin_target_path(plan.target_texture, choice)) {
        error = choice.reason;
        return false;
    }
    nfsu::BodySkinSelection corrected = selection;
    corrected.target = choice.path;
    if (!nfsu::assemble_body_skin_inputs(corrected, sources, limits, assembled, error)) {
        return false;
    }
    nfsu::SkinTargetSurface surface;
    if (!nfsu::read_skin_target_surface(sources, assembled.plan().target_texture, limits,
                                        surface, error)) {
        return false;
    }
    nfsu::SkinMarkerCache markers;
    return nfsu::compose_runtime_skin(sources, assembled, surface, markers, skin, error);
}

// ---------------------------------------------------------------------------
// Compositor path selection
// ---------------------------------------------------------------------------

nfsu::SkinTextureProvenance resolve_target(
    const std::vector<nfsu::SkinInputSource>& sources,
    const nfsu::BodySkinSelection& selection, bool& planned) {
    nfsu::BodySkinInputPlan plan;
    std::string error;
    planned = nfsu::plan_body_skin_inputs(selection, sources,
                                          nfsu::SkinInputLimits{}, plan, error);
    return plan.target_texture;
}

void test_target_path() {
    Fixture fixture = make_fixture();
    bool planned = false;
    const nfsu::SkinTextureProvenance argb =
        resolve_target(fixture.sources, fixture_selection(), planned);
    check(planned, "the fixture plan succeeds");
    nfsu::SkinTargetChoice choice;
    check(nfsu::choose_skin_target_path(argb, choice), "an A8R8G8B8 target is proven");
    check(choice.proven, "the A8R8G8B8 choice is marked proven");
    check(choice.path == nfsu::SkinTargetType::type32, "an A8R8G8B8 target is type-32");
    check_u32(choice.object_type_byte, 0x20U, "the A8R8G8B8 object type byte");
    check(!choice.expanded_on_upload, "an A8R8G8B8 target is not expanded");
    check(choice.reason.find("0x005A8A50") != std::string::npos,
          "the reason names the type-32 composite");

    // A paletted target: the upload rewrites the type byte to 0x20, so the
    // path is still type-32 and the surface is expanded.
    Fixture paletted = make_fixture();
    paletted.shared = make_source({p8_spec(kTargetName, 0x80, ramp_indices(16, 7),
                                           0xBF000000U)});
    paletted.sources[2] =
        source_of(*paletted.shared, nfsu::SkinSourceKind::other, "shared-1");
    const nfsu::SkinTextureProvenance p8 =
        resolve_target(paletted.sources, fixture_selection(), planned);
    check(planned, "the paletted-target plan succeeds");
    check(nfsu::choose_skin_target_path(p8, choice), "a P8 target is proven");
    check(choice.path == nfsu::SkinTargetType::type32, "a P8 target is still type-32");
    check_u32(choice.object_type_byte, 0x20U, "the P8 object type byte after the upload");
    check(choice.expanded_on_upload, "a P8 target is expanded on upload");
    check(choice.reason.find("0x00407EB1") != std::string::npos,
          "the reason names the type byte rewrite");

    // A DXT1 target: no traced dispatch composites it.
    Fixture dxt = make_fixture();
    TexSpec compressed;
    compressed.name = kTargetName;
    compressed.format_code = 0x22;
    compressed.d3d_format = 0x31545844U;  // 'DXT1'
    compressed.width = 4;
    compressed.height = 4;
    compressed.pixels = Bytes(8, 0x5A);
    dxt.shared = make_source({compressed});
    dxt.sources[2] = source_of(*dxt.shared, nfsu::SkinSourceKind::other, "shared-1");
    nfsu::BodySkinSelection sized = fixture_selection();
    sized.width = 4;
    sized.height = 4;
    const nfsu::SkinTextureProvenance dxt_target =
        resolve_target(dxt.sources, sized, planned);
    check(planned, "the DXT1-target plan succeeds");
    check(!nfsu::choose_skin_target_path(dxt_target, choice),
          "a DXT1 target proves no path");
    check(!choice.proven, "the DXT1 choice is not proven");
    check(choice.reason.find("DXT1") != std::string::npos,
          "the refusal names the format");
    check(choice.reason.find("0x22") != std::string::npos,
          "the refusal names the format code");

    // A target hash nothing claims.
    nfsu::SkinTextureProvenance absent;
    check(!nfsu::choose_skin_target_path(absent, choice),
          "an unresolved target proves no path");
    check(choice.reason.find("no record") != std::string::npos,
          "the refusal says the hash resolved to nothing");
}

// ---------------------------------------------------------------------------
// The target surface
// ---------------------------------------------------------------------------

void test_target_surface() {
    Fixture fixture = make_fixture();
    bool planned = false;
    const nfsu::SkinTextureProvenance target =
        resolve_target(fixture.sources, fixture_selection(), planned);
    check(planned, "the fixture plan succeeds");

    nfsu::SkinTargetSurface surface;
    std::string error;
    check(nfsu::read_skin_target_surface(fixture.sources, target, nfsu::SkinInputLimits{},
                                         surface, error),
          "the A8R8G8B8 target surface is read: " + error);
    check_u32(surface.width, 4U, "the target width");
    check_u32(surface.height, 4U, "the target height");
    check_size(surface.pixels.size(), 16U, "the target pixel count");
    const std::vector<std::uint32_t> expected = target_pixels();
    bool same = surface.pixels.size() == expected.size();
    for (std::size_t i = 0; same && i < expected.size(); ++i) {
        same = surface.pixels[i] == expected[i];
    }
    check(same, "the stored dwords are used unchanged");

    // A paletted target is expanded through its own palette, entry whole.
    Fixture paletted = make_fixture();
    paletted.shared =
        make_source({p8_spec(kTargetName, 0x80, ramp_indices(16, 7), 0xBF000000U)});
    paletted.sources[2] =
        source_of(*paletted.shared, nfsu::SkinSourceKind::other, "shared-1");
    const nfsu::SkinTextureProvenance p8 =
        resolve_target(paletted.sources, fixture_selection(), planned);
    nfsu::SkinTargetSurface expanded;
    check(nfsu::read_skin_target_surface(paletted.sources, p8, nfsu::SkinInputLimits{},
                                         expanded, error),
          "the P8 target surface is read: " + error);
    bool expanded_ok = expanded.pixels.size() == 16;
    for (std::size_t i = 0; expanded_ok && i < 16; ++i) {
        expanded_ok = expanded.pixels[i] == 0xBF000000U + static_cast<std::uint32_t>(i % 7);
    }
    check(expanded_ok, "every index is expanded through the record's own palette");

    // A provenance whose source index is outside the list at all.
    std::vector<nfsu::SkinInputSource> other = {fixture.sources[0]};
    check(!nfsu::read_skin_target_surface(other, target, nfsu::SkinInputLimits{}, surface,
                                          error),
          "a provenance outside the source list is refused");
    check(error.find("does not name a record") != std::string::npos,
          "the refusal says the provenance does not match");

    // A list of the same shape whose target record is a different format at
    // the same info offset, which is what a re-encoded pack looks like. The
    // name and the fixed-stride record offset match, so only comparing
    // everything the read depends on catches it; reading a DXT1 record with
    // an A8R8G8B8 level size would run 4 bytes per pixel off the end of a
    // block-compressed payload.
    Fixture recoded = make_fixture();
    TexSpec compressed;
    compressed.name = kTargetName;
    compressed.format_code = 0x22;
    compressed.d3d_format = 0x31545844U;  // 'DXT1'
    compressed.width = 4;
    compressed.height = 4;
    compressed.pixels = Bytes(8, 0x5A);
    recoded.shared = make_source({compressed});
    recoded.sources[2] =
        source_of(*recoded.shared, nfsu::SkinSourceKind::other, "shared-1");
    const nfsu::TextureRecord& original = fixture.sources[2].packs[0]->textures[0];
    const nfsu::TextureRecord& swapped = recoded.sources[2].packs[0]->textures[0];
    check_u32(swapped.hash, original.hash, "the re-encoded record keeps the name hash");
    check(swapped.info_offset == original.info_offset,
          "the re-encoded record keeps the info offset");
    check(swapped.format != original.format, "the re-encoded record changed format");
    check(!nfsu::read_skin_target_surface(recoded.sources, target,
                                          nfsu::SkinInputLimits{}, surface, error),
          "a record of another format at the same offset is refused");
    check(error.find("does not name a record") != std::string::npos,
          "the re-encoded refusal says the provenance does not match");
    check(surface.pixels.empty(), "nothing was read from the mismatched record");

    // A pixel bound below the target's size.
    nfsu::SkinInputLimits small;
    small.max_pixels = 8;
    check(!nfsu::read_skin_target_surface(fixture.sources, target, small, surface, error),
          "a target over the pixel bound is refused");
    check(surface.pixels.empty(), "a refused target surface is left empty");

    // A payload bound below the target's declared size.
    nfsu::SkinInputLimits tiny_payload;
    tiny_payload.max_payload_bytes = 8;
    check(!nfsu::read_skin_target_surface(fixture.sources, target, tiny_payload, surface,
                                          error),
          "a target over the payload bound is refused");

    // A blob with no JDLZ header at all: the record itself does not
    // validate, so the path selection is what refuses it.
    Fixture headerless = make_fixture();
    TexSpec no_header = argb_spec(kTargetName, target_pixels());
    no_header.blob = Bytes(24, 0x00);
    headerless.shared = make_source({no_header});
    headerless.sources[2] =
        source_of(*headerless.shared, nfsu::SkinSourceKind::other, "shared-1");
    // An unusable target cannot supply the dimensions, so the selection has
    // to carry them or the plan itself refuses before the record is seen.
    nfsu::BodySkinSelection with_size = fixture_selection();
    with_size.width = 4;
    with_size.height = 4;
    const nfsu::SkinTextureProvenance no_header_target =
        resolve_target(headerless.sources, with_size, planned);
    check(planned, "the headerless-target plan succeeds with explicit dimensions");
    check(no_header_target.resolved, "the headerless record still resolves by hash");
    check(!no_header_target.record_valid, "the headerless record does not validate");
    nfsu::SkinTargetChoice bad_choice;
    check(!nfsu::choose_skin_target_path(no_header_target, bad_choice),
          "an invalid target record proves no path");
    check(bad_choice.reason.find("unusable") != std::string::npos,
          "the refusal says the record is unusable");

    // A well-formed JDLZ header whose body cannot produce the declared
    // size. The record validates, so this is the one case that reaches
    // texture_payload and fails there.
    Fixture truncated = make_fixture();
    TexSpec bad_stream = argb_spec(kTargetName, target_pixels());
    Bytes stream = jdlz_header(static_cast<std::uint32_t>(bad_stream.pixels.size()), 24);
    stream.insert(stream.end(), 8, 0x00);
    bad_stream.blob = stream;
    truncated.shared = make_source({bad_stream});
    truncated.sources[2] =
        source_of(*truncated.shared, nfsu::SkinSourceKind::other, "shared-1");
    const nfsu::SkinTextureProvenance bad_target =
        resolve_target(truncated.sources, fixture_selection(), planned);
    check(bad_target.resolved && bad_target.record_valid,
          "a record with a valid JDLZ header validates");
    check(!nfsu::read_skin_target_surface(truncated.sources, bad_target,
                                          nfsu::SkinInputLimits{}, surface, error),
          "a target payload that cannot be decompressed is refused");
    check(!error.empty(), "the payload refusal has a reason");
    check(surface.pixels.empty(), "a refused payload leaves no pixels");
}

// The pack that holds the target must still be paired with the bytes the
// read takes its payload from: parsed from exactly that buffer, at exactly
// that size, with its record tables read. Without that pairing a buffer
// substituted for one of the same size is decoded under the provenance of a
// record it does not hold.
void test_target_surface_pairing() {
    Fixture fixture = make_fixture();
    bool planned = false;
    const nfsu::SkinTextureProvenance target =
        resolve_target(fixture.sources, fixture_selection(), planned);
    check(planned, "the pairing fixture plan succeeds");

    // Another image of exactly the same size holding different pixels. Every
    // offset the record carries still lands inside it, so nothing the read
    // bounds-checks would catch this; what is wrong is that these are not
    // the bytes the pack was parsed from.
    const Bytes substituted = build_pack({argb_ramp(kTargetName, 0x01020304U)});
    check_size(substituted.size(), fixture.shared->bytes.size(),
               "the substituted image is exactly the same size");
    check(substituted != fixture.shared->bytes,
          "the substituted image is not the original");

    std::vector<nfsu::SkinInputSource> substituted_sources = fixture.sources;
    substituted_sources[2].bytes = &substituted;
    nfsu::SkinTargetSurface surface;
    std::string error;
    check(!nfsu::read_skin_target_surface(substituted_sources, target,
                                          nfsu::SkinInputLimits{}, surface, error),
          "a source whose bytes were substituted is refused");
    check(error.find("not parsed from the bytes") != std::string::npos,
          "the refusal names the pairing rule");
    check(surface.pixels.empty(), "nothing was read from the substituted bytes");
    check(!surface.identity.resolved, "a refused read carries no identity");

    // Not a vacuous refusal: parsed properly, those same bytes hold a
    // different level 0, so accepting them would have composited pixels the
    // provenance does not describe.
    auto rogue = make_source({argb_ramp(kTargetName, 0x01020304U)});
    std::vector<nfsu::SkinInputSource> rogue_sources = fixture.sources;
    rogue_sources[2] = source_of(*rogue, nfsu::SkinSourceKind::other, "shared-1");
    const nfsu::SkinTextureProvenance rogue_target =
        resolve_target(rogue_sources, fixture_selection(), planned);
    check(planned, "the substituted image plans on its own sources");
    nfsu::SkinTargetSurface rogue_surface;
    check(nfsu::read_skin_target_surface(rogue_sources, rogue_target,
                                         nfsu::SkinInputLimits{}, rogue_surface, error),
          "the substituted image reads on its own sources: " + error);
    check(rogue_surface.pixels != target_pixels(),
          "the substituted image really holds other pixels");

    // The same buffer at a size the pack was not parsed at.
    nfsu::TexturePack resized = fixture.shared->pack;
    resized.file_size += 1;
    std::vector<nfsu::SkinInputSource> resized_sources = fixture.sources;
    resized_sources[2].packs[0] = &resized;
    check(!nfsu::read_skin_target_surface(resized_sources, target,
                                          nfsu::SkinInputLimits{}, surface, error),
          "a pack whose file size is not the source's is refused");
    check(error.find("not parsed from the bytes") != std::string::npos,
          "the size refusal names the pairing rule");

    // A pack whose record tables were never read: its records are unknown,
    // so nothing may be located in it.
    nfsu::TexturePack unparsed = fixture.shared->pack;
    unparsed.tables_parsed = false;
    std::vector<nfsu::SkinInputSource> unparsed_sources = fixture.sources;
    unparsed_sources[2].packs[0] = &unparsed;
    check(!nfsu::read_skin_target_surface(unparsed_sources, target,
                                          nfsu::SkinInputLimits{}, surface, error),
          "a pack whose record tables were not parsed is refused");
    check(error.find("record tables") != std::string::npos,
          "the refusal names the unparsed tables");

    // A pack no parser ever ran on, paired with an empty buffer. Both
    // sides of the pointer comparison would be null and both sizes zero,
    // so without the earlier check this would pass the pairing and two
    // unrelated sources would carry the same identity.
    const Bytes nothing;
    nfsu::TexturePack unparsed_pack;
    unparsed_pack.tables_parsed = true;
    unparsed_pack.textures = fixture.shared->pack.textures;
    nfsu::SkinInputSource empty_source;
    empty_source.bytes = &nothing;
    empty_source.packs.push_back(&unparsed_pack);
    empty_source.kind = nfsu::SkinSourceKind::other;
    empty_source.path = "shared-1";
    std::vector<nfsu::SkinInputSource> empty_sources = fixture.sources;
    empty_sources[2] = empty_source;
    check(!nfsu::read_skin_target_surface(empty_sources, target, nfsu::SkinInputLimits{},
                                          surface, error),
          "a source with no parsed bytes is refused");
    check(error.find("no parsed bytes") != std::string::npos,
          "the refusal says the source has no parsed bytes");
    check(!nfsu::skin_target_identity(empty_sources, target).resolved,
          "a source with no parsed bytes yields no identity");

    // The untouched fixture still reads, so the refusals above are the
    // pairing checks and not something broken in the fixture.
    check(nfsu::read_skin_target_surface(fixture.sources, target,
                                         nfsu::SkinInputLimits{}, surface, error),
          "the paired source still reads: " + error);
    check(surface.identity.resolved, "a successful read carries its identity");
    check(nfsu::same_skin_target(surface.identity,
                                 nfsu::skin_target_identity(fixture.sources, target)),
          "the identity is the record the provenance names");
    check(surface.identity.source_bytes == fixture.shared->bytes.data(),
          "the identity names the buffer the pixels were read from");
    check_size(surface.identity.source_byte_size, fixture.shared->bytes.size(),
               "and the size that buffer was parsed at");
    check(!nfsu::skin_target_identity(substituted_sources, target).resolved,
          "an unpaired source yields no identity");
}

// Two valid records of the same format and the same size are
// indistinguishable once their level 0 is in a vector, so the surface has
// to say which record it came from and the composite has to check it.
void test_swapped_target() {
    const char* const kOtherName = "DUMMY_SKIN2";
    std::vector<std::uint32_t> other_pixels = target_pixels();
    for (std::size_t i = nfsu::kSkinMarkerSurface.size(); i < other_pixels.size(); ++i) {
        other_pixels[i] = 0xBF00FF00U;
    }
    Fixture fixture = make_fixture();
    fixture.shared = make_source({argb_spec(kTargetName, target_pixels()),
                                  argb_spec(kOtherName, other_pixels)});
    fixture.sources[2] =
        source_of(*fixture.shared, nfsu::SkinSourceKind::other, "shared-1");

    // Inputs assembled for DUMMY_SKIN1.
    const nfsu::SkinInputLimits limits;
    std::string error;
    nfsu::AssembledSkinInputs assembled;
    check(nfsu::assemble_body_skin_inputs(fixture_selection(), fixture.sources, limits,
                                          assembled, error),
          "the swap fixture assembles: " + error);

    // The surface of DUMMY_SKIN2, which is valid, the same size and proves
    // the same path, so nothing but the identity separates the two.
    nfsu::BodySkinSelection other_selection = fixture_selection();
    other_selection.target_hash = nfsu::bin_hash(kOtherName);
    bool planned = false;
    const nfsu::SkinTextureProvenance other_target =
        resolve_target(fixture.sources, other_selection, planned);
    check(planned, "the second target plans");
    nfsu::SkinTargetSurface other_surface;
    check(nfsu::read_skin_target_surface(fixture.sources, other_target, limits,
                                         other_surface, error),
          "the second target surface is read: " + error);
    check(other_surface.choice.proven &&
              other_surface.choice.path == nfsu::SkinTargetType::type32,
          "the second target proves the same path");
    check_u32(other_surface.width, assembled.request().width,
              "the two targets are the same width");
    check_u32(other_surface.height, assembled.request().height,
              "the two targets are the same height");
    check(!nfsu::same_skin_target(other_surface.identity,
                                  nfsu::skin_target_identity(
                                      fixture.sources,
                                      assembled.plan().target_texture)),
          "the two targets are different records");

    nfsu::SkinMarkerCache markers;
    nfsu::RuntimeSkin swapped;
    check(!nfsu::compose_runtime_skin(fixture.sources, assembled, other_surface, markers,
                                      swapped, error),
          "a surface read from another record is refused");
    check(error.find("was not read from the record") != std::string::npos,
          "the refusal says the surface is not the assembled target");
    check(swapped.rgba.empty(), "a refused swap produces no pixels");

    // The matching pair still composes, so the check refuses the swap and
    // nothing else.
    nfsu::SkinTargetSurface own;
    check(nfsu::read_skin_target_surface(fixture.sources, assembled.plan().target_texture,
                                         limits, own, error),
          "the assembled target's own surface is read: " + error);
    check(own.pixels != other_surface.pixels,
          "the two targets really hold different pixels");
    nfsu::SkinMarkerCache own_markers;
    nfsu::RuntimeSkin composed;
    check(nfsu::compose_runtime_skin(fixture.sources, assembled, own, own_markers,
                                     composed, error),
          "the matching pair composes: " + error);
    check_u32(composed.target_hash, target_hash(),
              "the composite is labelled with its own target");

    // A surface whose identity was never filled, which is what a hand-built
    // one carries, is refused rather than trusted.
    nfsu::SkinTargetSurface anonymous = own;
    anonymous.identity = nfsu::SkinTargetIdentity{};
    nfsu::SkinMarkerCache anonymous_markers;
    nfsu::RuntimeSkin refused;
    check(!nfsu::compose_runtime_skin(fixture.sources, assembled, anonymous,
                                      anonymous_markers, refused, error),
          "a surface with no identity is refused");

    // The swap that needs no second record: one record, two source lists.
    // `rogue` holds the same two textures under the same names at the same
    // sizes, so its pack has exactly the same layout and every field the
    // provenance carries matches; only the pixels, and the buffer they live
    // in, differ. Without the buffer in the identity this composite would
    // succeed and bind the rogue pixels under the fixture's target hash.
    auto rogue = make_source({argb_spec(kTargetName, other_pixels),
                              argb_spec(kOtherName, target_pixels())});
    std::vector<nfsu::SkinInputSource> rogue_sources = fixture.sources;
    rogue_sources[2] = source_of(*rogue, nfsu::SkinSourceKind::other, "shared-1");
    bool rogue_planned = false;
    const nfsu::SkinTextureProvenance rogue_target =
        resolve_target(rogue_sources, fixture_selection(), rogue_planned);
    check(rogue_planned, "the rogue list plans");
    nfsu::SkinTargetSurface rogue_surface;
    check(nfsu::read_skin_target_surface(rogue_sources, rogue_target, limits,
                                         rogue_surface, error),
          "the rogue list reads its own target: " + error);
    check(rogue_surface.pixels != own.pixels,
          "the rogue target really holds other pixels");
    check(nfsu::same_skin_target(rogue_surface.identity,
                                 nfsu::skin_target_identity(rogue_sources, rogue_target)),
          "the rogue surface matches its own list");
    nfsu::SkinMarkerCache rogue_markers;
    nfsu::RuntimeSkin rogue_skin;
    check(!nfsu::compose_runtime_skin(fixture.sources, assembled, rogue_surface,
                                      rogue_markers, rogue_skin, error),
          "a surface read from another source list is refused");
    check(error.find("different source lists") != std::string::npos,
          "the refusal names the two lists");
    check(rogue_skin.rgba.empty(), "a refused cross-list surface produces no pixels");

    // The check is symmetric: naming the rogue list while handing over the
    // fixture's own surface is refused as well, so it is the pair that has
    // to agree and not one side of it.
    //
    // What this does not prove, because the layer does not check it: that
    // `sources` is the list `assembled` was built from. Only the target
    // record is re-located in it, and the layer pixels the composite uses
    // are buffers `assembled` owns, so a list differing anywhere but the
    // target changes nothing. See the header.
    nfsu::SkinMarkerCache other_list_markers;
    nfsu::RuntimeSkin other_list_skin;
    check(!nfsu::compose_runtime_skin(rogue_sources, assembled, own, other_list_markers,
                                      other_list_skin, error),
          "naming the rogue list with the fixture's own surface is refused");
}


// Every field of SkinTargetIdentity, changed one at a time.
//
// same_skin_target is what keeps one target's pixels from being composited,
// labelled and bound as another's, and a comparison that silently drops a
// field goes on answering "the same target" for two that are not. The
// negative cases in the tests above differ in several fields at once, so
// none of them pins an individual field: with pack_root_offset, pack_hash,
// storage, mip_count or palette_bytes deleted from the comparison the whole
// suite still passed. Each case below differs in exactly one field.
//
// The names are bound from the struct itself. A structured binding must
// name exactly as many as the struct has members, so a field added to
// SkinTargetIdentity does not compile until it is named here and has a case
// of its own - the same pin src/runtime_skin.cpp puts on the comparison.
void test_identity_fields() {
    Fixture fixture = make_fixture();
    bool planned = false;
    const nfsu::SkinTextureProvenance target =
        resolve_target(fixture.sources, fixture_selection(), planned);
    check(planned, "the identity fixture plans");
    const nfsu::SkinTargetIdentity base =
        nfsu::skin_target_identity(fixture.sources, target);
    check(base.resolved, "the fixture identity resolves");
    check(nfsu::same_skin_target(base, base), "an identity is itself");

    nfsu::SkinTargetIdentity probe = base;
    auto& [resolved, source_index, pack_index, pack_root_offset, pack_hash, record_index,
           record_hash, table_hash, info_offset, storage, format, format_code,
           d3d_format, width, height, mip_count, data_size, palette_bytes,
           top_level_size, blob_offset, blob_size, source_bytes, source_byte_size] =
        probe;

    std::size_t fields = 0;
    // Called with exactly one field of `probe` changed. It checks both
    // directions, because a comparison that is not symmetric would be a
    // defect of its own, and then puts the field back.
    const auto differs = [&](const std::string& field) {
        ++fields;
        check(!nfsu::same_skin_target(base, probe),
              "an identity differing only in " + field + " is not the same target");
        check(!nfsu::same_skin_target(probe, base),
              "an identity differing only in " + field + " is not the same either way");
        probe = base;
        check(nfsu::same_skin_target(base, probe),
              "putting " + field + " back makes it the same target again");
    };

    resolved = false;
    differs("resolved");
    source_index = base.source_index + 1;
    differs("source_index");
    pack_index = base.pack_index + 1;
    differs("pack_index");
    pack_root_offset = base.pack_root_offset + 1;
    differs("pack_root_offset");
    pack_hash = base.pack_hash ^ 1U;
    differs("pack_hash");
    record_index = base.record_index + 1;
    differs("record_index");
    record_hash = base.record_hash ^ 1U;
    differs("record_hash");
    table_hash = base.table_hash ^ 1U;
    differs("table_hash");
    info_offset = base.info_offset + 1;
    differs("info_offset");
    storage = base.storage == nfsu::TextureStorage::jdlz ? nfsu::TextureStorage::raw
                                                         : nfsu::TextureStorage::jdlz;
    differs("storage");
    format = base.format == nfsu::PixelFormat::a8r8g8b8 ? nfsu::PixelFormat::p8
                                                        : nfsu::PixelFormat::a8r8g8b8;
    differs("format");
    format_code = static_cast<std::uint8_t>(base.format_code ^ 1U);
    differs("format_code");
    d3d_format = base.d3d_format ^ 1U;
    differs("d3d_format");
    width = base.width + 1;
    differs("width");
    height = base.height + 1;
    differs("height");
    mip_count = base.mip_count + 1;
    differs("mip_count");
    data_size = base.data_size + 1;
    differs("data_size");
    palette_bytes = base.palette_bytes + 1;
    differs("palette_bytes");
    top_level_size = base.top_level_size + 1;
    differs("top_level_size");
    blob_offset = base.blob_offset + 1;
    differs("blob_offset");
    blob_size = base.blob_size + 1;
    differs("blob_size");
    // The buffer half. It is a pointer that is compared and never
    // dereferenced, so another live buffer of the same size is the case to
    // make: a second source list holding a pack of the same layout at the
    // same index differs in nothing else.
    const nfsu::SkinInputSource& elsewhere = fixture.sources[0];
    check(elsewhere.bytes != nullptr && elsewhere.bytes->data() != base.source_bytes,
          "the second buffer is a different one");
    source_bytes = elsewhere.bytes->data();
    differs("source_bytes");
    source_byte_size = base.source_byte_size + 1;
    differs("source_byte_size");

    check_size(fields, 23U, "every field of SkinTargetIdentity has a case");

    // An unresolved identity matches nothing, including another unresolved
    // one, so a hand-built surface cannot pass by leaving the identity out.
    const nfsu::SkinTargetIdentity empty;
    check(!nfsu::same_skin_target(empty, empty), "two unresolved identities do not match");
    check(!nfsu::same_skin_target(base, empty), "a resolved identity does not match one");
}

// ---------------------------------------------------------------------------
// Composition
// ---------------------------------------------------------------------------

void test_compose() {
    Fixture fixture = make_fixture();
    nfsu::AssembledSkinInputs assembled;
    nfsu::SkinTargetChoice choice;
    nfsu::RuntimeSkin skin;
    std::string error;
    check(run_pipeline(fixture_selection(), fixture.sources, assembled, choice, skin,
                       error),
          "the fixture composes: " + error);
    check_u32(skin.width, 4U, "the generated width");
    check_u32(skin.height, 4U, "the generated height");
    check_u32(skin.target_hash, target_hash(), "the generated texture's target hash");
    check(skin.path == nfsu::SkinTargetType::type32, "the generated path is type-32");
    check_size(skin.layers, 2U, "both selected sources became layers");
    check_size(skin.rgba.size(), 16U * 4U, "the generated RGBA size");

    // The RGBA bytes are exactly the documented mapping of the composite's
    // own dwords, checked against a direct call rather than against numbers
    // written down here.
    nfsu::SkinTargetSurface surface;
    check(nfsu::read_skin_target_surface(fixture.sources, assembled.plan().target_texture,
                                         nfsu::SkinInputLimits{}, surface, error),
          "the target surface is read again: " + error);
    nfsu::SkinMarkerCache markers;
    nfsu::BodySkin32 direct;
    check(nfsu::composite_body_skin_32(assembled.request(), surface.pixels, markers, direct,
                                       error),
          "the composite runs directly: " + error);
    bool mapped = direct.pixels.size() * 4U == skin.rgba.size();
    for (std::size_t i = 0; mapped && i < direct.pixels.size(); ++i) {
        const std::uint32_t pixel = direct.pixels[i];
        mapped = skin.rgba[i * 4U + 0] == ((pixel >> 16U) & 0xFFU) &&
                 skin.rgba[i * 4U + 1] == ((pixel >> 8U) & 0xFFU) &&
                 skin.rgba[i * 4U + 2] == (pixel & 0xFFU) &&
                 skin.rgba[i * 4U + 3] == ((pixel >> 24U) & 0xFFU);
    }
    check(mapped, "RGBA is red byte 2, green byte 1, blue byte 0, GLOSS byte 3");
    check(markers.valid, "the composite filled the marker cache");

    // Deterministic: the same inputs give byte-identical output, twice.
    nfsu::AssembledSkinInputs again_inputs;
    nfsu::SkinTargetChoice again_choice;
    nfsu::RuntimeSkin again;
    check(run_pipeline(fixture_selection(), fixture.sources, again_inputs, again_choice,
                       again, error),
          "the fixture composes a second time: " + error);
    check(again.rgba == skin.rgba, "two runs of the same selection are byte-identical");

    // A different base paint changes the pixels, so the comparison above is
    // not comparing two empty buffers.
    nfsu::BodySkinSelection repainted = fixture_selection();
    repainted.base_color = nfsu::skin_pack_paint(0x00, 0xFF, 0x00, 0x10);
    nfsu::AssembledSkinInputs repainted_inputs;
    nfsu::SkinTargetChoice repainted_choice;
    nfsu::RuntimeSkin repainted_skin;
    check(run_pipeline(repainted, fixture.sources, repainted_inputs, repainted_choice,
                       repainted_skin, error),
          "a repainted selection composes: " + error);
    check(repainted_skin.rgba != skin.rgba, "a different base paint changes the pixels");

    // A surface whose dimensions are not the assembled request's.
    nfsu::SkinTargetSurface mismatched = surface;
    mismatched.width = 2;
    mismatched.height = 8;
    nfsu::RuntimeSkin refused;
    nfsu::SkinMarkerCache fresh;
    check(!nfsu::compose_runtime_skin(fixture.sources, assembled, mismatched, fresh,
                                      refused, error),
          "a target of other dimensions is refused");
    check(refused.rgba.empty(), "a refused composite leaves no pixels");

    // A surface whose path was never proven.
    nfsu::SkinTargetSurface unproven = surface;
    unproven.choice = nfsu::SkinTargetChoice{};
    unproven.choice.reason = "test";
    check(!nfsu::compose_runtime_skin(fixture.sources, assembled, unproven, fresh,
                                      refused, error),
          "an unproven target is refused");
}

// ---------------------------------------------------------------------------
// Source priority and fallback
// ---------------------------------------------------------------------------

void test_source_priority() {
    // Both the car pack and the vinyls hold 240SX_TRIBAL5 and its mask, with
    // different pixels. The first source that claims the hash wins.
    auto car = make_source({
        argb_ramp("240SX_DOORLINE", 0x10203040U),
        argb_ramp("240SX_DOORLINE_MASK", 0x00000040U),
        p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 4), 0x40000000U),
        p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 3), 0x00000030U),
    });
    auto vinyls = make_source({
        p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 4), 0x20000000U),
        p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 3), 0x00000030U),
    });
    auto shared = make_source({argb_spec(kTargetName, target_pixels())});

    const std::vector<nfsu::SkinInputSource> car_first = {
        source_of(*car, nfsu::SkinSourceKind::car_textures, "car-textures"),
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "car-vinyls"),
        source_of(*shared, nfsu::SkinSourceKind::other, "shared-1")};
    const std::vector<nfsu::SkinInputSource> vinyls_first = {
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "car-vinyls"),
        source_of(*car, nfsu::SkinSourceKind::car_textures, "car-textures"),
        source_of(*shared, nfsu::SkinSourceKind::other, "shared-1")};

    nfsu::AssembledSkinInputs first_inputs;
    nfsu::SkinTargetChoice first_choice;
    nfsu::RuntimeSkin first;
    std::string error;
    check(run_pipeline(fixture_selection(), car_first, first_inputs, first_choice, first,
                       error),
          "the car-first order composes: " + error);
    const nfsu::SkinInputLayerInfo& first_vinyl = first_inputs.plan().inputs[0];
    check_size(first_vinyl.source.source_index, 0U, "the car pack wins the vinyl hash");
    check_size(first_vinyl.source.also_claimed_by.size(), 1U,
               "the losing claimant is recorded");
    check_size(first_vinyl.source.also_claimed_by[0].source, 1U,
               "the losing claimant names its source");

    nfsu::AssembledSkinInputs second_inputs;
    nfsu::SkinTargetChoice second_choice;
    nfsu::RuntimeSkin second;
    check(run_pipeline(fixture_selection(), vinyls_first, second_inputs, second_choice,
                       second, error),
          "the vinyls-first order composes: " + error);
    check_size(second_inputs.plan().inputs[0].source.source_index, 0U,
               "the vinyls pack wins when it is first");
    check(first.rgba != second.rgba, "source order decides which pixels are composited");
}

void test_fallback() {
    // A vinyl whose mask is not in any source is skipped; the composite goes
    // on with the layers that did resolve.
    auto car = make_source({
        argb_ramp("240SX_DOORLINE", 0x10203040U),
        argb_ramp("240SX_DOORLINE_MASK", 0x00000040U),
    });
    auto vinyls = make_source({
        p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 4), 0x20000000U),
        // no 240SX_TRIBAL5_MASK
    });
    auto shared = make_source({argb_spec(kTargetName, target_pixels())});
    const std::vector<nfsu::SkinInputSource> sources = {
        source_of(*car, nfsu::SkinSourceKind::car_textures, "car-textures"),
        source_of(*vinyls, nfsu::SkinSourceKind::car_vinyls, "car-vinyls"),
        source_of(*shared, nfsu::SkinSourceKind::other, "shared-1")};

    nfsu::AssembledSkinInputs inputs;
    nfsu::SkinTargetChoice choice;
    nfsu::RuntimeSkin skin;
    std::string error;
    check(run_pipeline(fixture_selection(), sources, inputs, choice, skin, error),
          "a missing mask does not stop the composite: " + error);
    check_size(inputs.plan().skipped_inputs, 1U, "the unpaired vinyl is skipped");
    check(inputs.plan().inputs[0].skip_reason == nfsu::SkinSkipReason::mask_not_found,
          "the skip reason is the missing mask");
    check(!inputs.plan().inputs[0].skip_detail.empty(), "the skip carries a detail");
    check_size(skin.layers, 1U, "only the doorline became a layer");

    // The result is exactly what selecting only the doorline gives.
    nfsu::BodySkinSelection doorline_only = fixture_selection();
    doorline_only.vinyl_layers[0].reset();
    nfsu::AssembledSkinInputs bare_inputs;
    nfsu::SkinTargetChoice bare_choice;
    nfsu::RuntimeSkin bare;
    check(run_pipeline(doorline_only, sources, bare_inputs, bare_choice, bare, error),
          "the doorline-only selection composes: " + error);
    check(bare.rgba == skin.rgba, "a skipped layer contributes nothing");

    // A vinyl that is not the target's size is skipped for that reason.
    TexSpec wrong_size = p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(64, 4), 0x20000000U,
                                 8, 8);
    auto sized = make_source({wrong_size,
                              p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 3),
                                      0x00000030U)});
    const std::vector<nfsu::SkinInputSource> sized_sources = {
        source_of(*car, nfsu::SkinSourceKind::car_textures, "car-textures"),
        source_of(*sized, nfsu::SkinSourceKind::car_vinyls, "car-vinyls"),
        source_of(*shared, nfsu::SkinSourceKind::other, "shared-1")};
    nfsu::AssembledSkinInputs sized_inputs;
    nfsu::SkinTargetChoice sized_choice;
    nfsu::RuntimeSkin sized_skin;
    check(run_pipeline(fixture_selection(), sized_sources, sized_inputs, sized_choice,
                       sized_skin, error),
          "a mis-sized vinyl does not stop the composite: " + error);
    check(sized_inputs.plan().inputs[0].skip_reason ==
              nfsu::SkinSkipReason::source_dimension_mismatch,
          "the skip reason is the dimension mismatch");
    check(sized_skin.rgba == bare.rgba, "the mis-sized vinyl contributes nothing");

    // A malformed record is skipped the same way rather than refused: the
    // blob is not a JDLZ stream that can produce the declared level.
    TexSpec malformed = p8_spec("240SX_TRIBAL5", 0x80, ramp_indices(16, 4), 0x20000000U);
    malformed.blob = Bytes(32, 0x00);
    auto broken = make_source({malformed,
                               p8_spec("240SX_TRIBAL5_MASK", 0x08, ramp_indices(16, 3),
                                       0x00000030U)});
    const std::vector<nfsu::SkinInputSource> broken_sources = {
        source_of(*car, nfsu::SkinSourceKind::car_textures, "car-textures"),
        source_of(*broken, nfsu::SkinSourceKind::car_vinyls, "car-vinyls"),
        source_of(*shared, nfsu::SkinSourceKind::other, "shared-1")};
    nfsu::AssembledSkinInputs broken_inputs;
    nfsu::SkinTargetChoice broken_choice;
    nfsu::RuntimeSkin broken_skin;
    check(run_pipeline(fixture_selection(), broken_sources, broken_inputs, broken_choice,
                       broken_skin, error),
          "a malformed vinyl does not stop the composite: " + error);
    check(broken_inputs.plan().inputs[0].skip_reason ==
              nfsu::SkinSkipReason::source_record_unusable,
          "the skip reason is the unusable record");
    check(broken_skin.rgba == bare.rgba, "the malformed vinyl contributes nothing");
}

void test_missing_target() {
    Fixture fixture = make_fixture();
    nfsu::BodySkinSelection selection = fixture_selection();
    selection.target_hash = nfsu::bin_hash("NO_SUCH_TEXTURE");

    // With no dimensions of its own, the selection has nothing to fall back
    // on and the plan itself refuses.
    nfsu::BodySkinInputPlan plan;
    std::string error;
    check(!nfsu::plan_body_skin_inputs(selection, fixture.sources, nfsu::SkinInputLimits{},
                                       plan, error),
          "an unresolved target with no dimensions is refused");
    check(!error.empty(), "the refusal has a reason");

    // With explicit dimensions the plan succeeds and the path selection is
    // what refuses, because there is no record to take a path from.
    selection.width = 4;
    selection.height = 4;
    check(nfsu::plan_body_skin_inputs(selection, fixture.sources, nfsu::SkinInputLimits{},
                                      plan, error),
          "an unresolved target with dimensions still plans: " + error);
    check(!plan.target_texture.resolved, "the target is reported unresolved");
    nfsu::SkinTargetChoice choice;
    check(!nfsu::choose_skin_target_path(plan.target_texture, choice),
          "an unresolved target proves no path");
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

nfsu::RenderTexture pack_texture(std::uint32_t hash, std::uint32_t size,
                                 unsigned char seed, std::size_t source) {
    nfsu::RenderTexture out;
    out.hash = hash;
    out.claims[0] = hash;
    out.claim_count = 1;
    out.width = size;
    out.height = size;
    out.valid = true;
    out.rgba.assign(std::size_t{size} * size * 4U, seed);
    out.source = source;
    return out;
}

std::uint64_t rgba_sum(const nfsu::RenderTextureSet& set) {
    std::uint64_t total = 0;
    for (const nfsu::RenderTexture& texture : set.textures) {
        total += texture.rgba.size();
    }
    return total;
}

nfsu::RuntimeSkin fixture_skin() {
    Fixture fixture = make_fixture();
    nfsu::AssembledSkinInputs assembled;
    nfsu::SkinTargetChoice choice;
    nfsu::RuntimeSkin skin;
    std::string error;
    check(run_pipeline(fixture_selection(), fixture.sources, assembled, choice, skin,
                       error),
          "the binding fixture composes: " + error);
    return skin;
}

// Every field of RuntimeSkinBinding at its default.
//
// A refused binding reports nothing, and "nothing" has to mean the whole
// struct: an index or a source left behind is a report of something that
// did not happen, and the argument refusals return before the catch
// handlers that reset it, so nothing else would put it back. The names are
// bound from the struct, so a field added to RuntimeSkinBinding does not
// compile until it has been decided about here.
bool reports_nothing(const nfsu::RuntimeSkinBinding& binding) {
    const auto& [hashes, texture_index, source, bytes, displaced, displaced_claims,
                 preserved_claims] = binding;
    return hashes.empty() && texture_index == 0 && source == 0 && bytes == 0 &&
           displaced.empty() && displaced_claims.empty() && preserved_claims.empty();
}

void test_binding_rules() {
    const nfsu::RuntimeSkin skin = fixture_skin();
    nfsu::RenderTextureSet set;
    set.textures.push_back(pack_texture(0x11111111U, 2, 7, 0));
    set.decoded_bytes = rgba_sum(set);
    const nfsu::RenderTextureSet original = set;

    nfsu::RuntimeSkinBinding binding;
    std::string error;
    // Every refusal is checked by its message as well as by its answer.
    // The message is what the viewer prints, so a branch that reported
    // nothing would ship unnoticed, and two of these branches are otherwise
    // indistinguishable from one another: {body_hash()} is a well-formed
    // list of one hash that is simply not the target, and is not the
    // malformed-size case it looks like.
    check(!nfsu::bind_runtime_skin(skin, {}, 1, set, binding, error),
          "an empty hash list is refused");
    check(error.find("binds 1 to 3 hashes, got 0") != std::string::npos,
          "the empty list says how many it takes: " + error);
    check(reports_nothing(binding),
          "and the empty refusal reports nothing");
    check(!nfsu::bind_runtime_skin(skin, {body_hash()}, 1, set, binding, error),
          "a list whose first hash is not the target is refused");
    check(error.find("the first bound hash must be the target hash") != std::string::npos,
          "the wrong first hash says so: " + error);
    check(reports_nothing(binding),
          "and the first refusal reports nothing");
    check(error.find(hex(skin.target_hash)) != std::string::npos &&
              error.find(hex(body_hash())) != std::string::npos,
          "and names both the wanted and the given hash: " + error);
    check(!nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash(), body_hash()}, 1,
                                   set, binding, error),
          "a repeated hash is refused");
    check(error.find("the bound hashes repeat " + hex(body_hash())) != std::string::npos,
          "the repeat names the hash: " + error);
    check(reports_nothing(binding),
          "and the repeat refusal reports nothing");
    check(!nfsu::bind_runtime_skin(
              skin, {skin.target_hash, body_hash(), 0x1U, 0x2U}, 1, set, binding, error),
          "more than three hashes are refused");
    check(error.find("binds 1 to 3 hashes, got 4") != std::string::npos,
          "the overlong list says how many it got: " + error);
    check(reports_nothing(binding),
          "and the overlong refusal reports nothing");
    nfsu::RuntimeSkin empty;
    empty.target_hash = skin.target_hash;
    check(!nfsu::bind_runtime_skin(empty, {skin.target_hash}, 1, set, binding, error),
          "a skin with no pixels is refused");
    check(error.find("zero dimension") != std::string::npos,
          "the empty skin says which dimension rule it broke: " + error);
    check(reports_nothing(binding),
          "and the zero refusal reports nothing");
    // A skin that has dimensions but not the pixels for them: the one
    // refusal left, and the only one that reads skin.rgba.
    nfsu::RuntimeSkin short_skin = skin;
    short_skin.rgba.pop_back();
    check(!nfsu::bind_runtime_skin(short_skin, {skin.target_hash}, 1, set, binding, error),
          "a skin whose pixels do not match its dimensions is refused");
    check(error.find("bytes, expected") != std::string::npos,
          "the short skin says what it expected: " + error);
    check(reports_nothing(binding),
          "and the short refusal reports nothing");
    check_size(set.textures.size(), original.textures.size(),
              "a refused binding leaves the texture count alone");
    check(set.decoded_bytes == original.decoded_bytes,
          "a refused binding leaves decoded_bytes alone");
    check(reports_nothing(binding), "a refused binding reports nothing");

    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 4, set, binding,
                                  error),
          "the skin binds: " + error);
    check_size(set.textures.size(), 2U, "the generated texture was appended");
    check_size(binding.texture_index, 1U, "the binding reports where it landed");
    const nfsu::RenderTexture& generated = set.textures[binding.texture_index];
    check_u32(generated.hash, skin.target_hash, "the generated texture's hash");
    check_size(generated.claim_count, 2U, "both hashes are claimed");
    check(generated.claims_hash(skin.target_hash), "the target hash is claimed");
    check(generated.claims_hash(body_hash()), "the body hash is claimed");
    check(generated.usable(), "the generated texture is usable");
    check_size(generated.source, 4U, "the provenance source the caller gave");
    check_size(generated.pack, 0U, "the generated texture has no pack");
    check_size(generated.record, 0U, "the generated texture has no record");
    check(generated.rgba == skin.rgba, "the generated pixels are the composed ones");
    // Expected independently of the implementation: nothing was displaced,
    // so the decoded bytes are what they were plus the generated texture.
    check(set.decoded_bytes == original.decoded_bytes + skin.rgba.size(),
          "decoded_bytes grew by exactly the generated texture");
    check(set.decoded_bytes == rgba_sum(set),
          "decoded_bytes still sums the retained textures");
    check(binding.displaced.empty(), "nothing was displaced");

    // The maximum the cap allows, which is also the last claims slot.
    nfsu::RenderTextureSet three;
    three.textures.push_back(pack_texture(0x11111111U, 2, 7, 0));
    three.decoded_bytes = rgba_sum(three);
    nfsu::RuntimeSkinBinding wide;
    const std::vector<std::uint32_t> all = {skin.target_hash, body_hash(), 0x0BADF00DU};
    check(all.size() == nfsu::kMaxRuntimeSkinHashes, "the fixture binds the cap");
    check(nfsu::bind_runtime_skin(skin, all, 4, three, wide, error),
          "the cap many hashes bind: " + error);
    const nfsu::RenderTexture& wide_texture = three.textures[wide.texture_index];
    check_size(wide_texture.claim_count, nfsu::kMaxRuntimeSkinHashes,
               "every hash is claimed");
    for (const std::uint32_t hash : all) {
        check(wide_texture.claims_hash(hash), "bound hash " + hex(hash) + " is claimed");
    }
}

void test_binding_displaces() {
    const nfsu::RuntimeSkin skin = fixture_skin();
    nfsu::RenderTextureSet set;
    set.textures.push_back(pack_texture(0x11111111U, 2, 7, 0));
    // A pack record that claims a bound hash. Left in place it would make
    // the binding ambiguous, which resolve_material reports as unusable.
    nfsu::RenderTexture rival = pack_texture(body_hash(), 2, 9, 1);
    rival.pack = 3;
    rival.record = 5;
    set.textures.push_back(rival);
    set.textures.push_back(pack_texture(skin.target_hash, 2, 11, 2));
    set.unusable_claims.push_back(nfsu::UnusableClaim{body_hash(), 1, 3, 6});
    set.unusable_claims.push_back(nfsu::UnusableClaim{0x22222222U, 1, 3, 7});
    set.decoded_bytes = rgba_sum(set);
    const std::uint64_t kept_bytes = set.textures[0].rgba.size();

    nfsu::RuntimeSkinBinding binding;
    std::string error;
    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 4, set, binding,
                                  error),
          "the skin binds over two claimants: " + error);
    check_size(set.textures.size(), 2U, "both claimants were removed");
    check_size(binding.displaced.size(), 2U, "both claimants were reported");
    check_u32(binding.displaced[0].hash, body_hash(), "the first displaced hash");
    check_size(binding.displaced[0].source, 1U, "the first displaced source");
    check_size(binding.displaced[0].pack, 3U, "the first displaced pack");
    check_size(binding.displaced[0].record, 5U, "the first displaced record");
    check(binding.displaced[0].was_usable, "the first displaced texture was usable");
    check_u32(binding.displaced[1].hash, skin.target_hash, "the second displaced hash");
    check_size(binding.displaced_claims.size(), 1U, "the bound unusable claim is removed");
    check_u32(binding.displaced_claims[0].hash, body_hash(), "the displaced claim hash");
    check_size(set.unusable_claims.size(), 1U, "the unrelated claim is kept");
    check_u32(set.unusable_claims[0].hash, 0x22222222U, "the unrelated claim is intact");
    // Expected independently of the implementation: only the one unrelated
    // texture and the generated one are left.
    check(set.decoded_bytes == kept_bytes + skin.rgba.size(),
          "decoded_bytes is the kept texture plus the generated one");
    check(set.decoded_bytes == rgba_sum(set),
          "decoded_bytes still sums the retained textures");
    check(set.textures[0].hash == 0x11111111U, "the unrelated texture is kept");
    // The appended texture shifts down as the claimants above it are
    // erased, so its reported index has to follow.
    check_size(binding.texture_index, set.textures.size() - 1U,
               "the generated texture is still reported last");
    check(set.textures[binding.texture_index].rgba == skin.rgba,
          "the reported index names the generated pixels");
    check(set.textures[binding.texture_index].claims_hash(body_hash()),
          "the reported index names the texture that claims the bound hash");
}

// What a material slot naming `hash` would resolve to in `set`, which is
// the behaviour a binding must not change for a hash it never named.
nfsu::BindingKind binding_of(const nfsu::RenderTextureSet& set, std::uint32_t hash) {
    nfsu::Mesh mesh;
    mesh.texture_hashes = {hash};
    nfsu::MeshMaterial slot;
    slot.texture_slot = 0;
    return nfsu::resolve_material(mesh, slot, set).kind;
}

// A record whose three hash fields disagree claims all three (see
// RenderTexture::claims). One bound hash among them takes the whole record
// away, so the other two have to be kept as unusable claims or they would
// silently stop matching anything.
void test_binding_preserves_claims() {
    const nfsu::RuntimeSkin skin = fixture_skin();
    const std::uint32_t alias = 0x33333333U;
    const std::uint32_t other = 0x44444444U;

    nfsu::RenderTextureSet set;
    set.textures.push_back(pack_texture(0x11111111U, 2, 7, 0));
    nfsu::RenderTexture multi = pack_texture(alias, 2, 9, 1);
    // claims are in the order info +36, hash table +0, blob table +0.
    // info +36 is an unbound hash, which RenderTexture::hash repeats
    // because it is that same field; the hash table holds the bound one and
    // the blob table a second unbound one. Only body_hash() is bound.
    multi.claims[0] = alias;
    multi.claims[1] = body_hash();
    multi.claims[2] = other;
    multi.claim_count = 3;
    multi.valid = false;
    multi.problem = "three disagreeing hash fields";
    multi.pack = 3;
    multi.record = 5;
    set.textures.push_back(multi);
    set.decoded_bytes = rgba_sum(set);

    check(binding_of(set, alias) == nfsu::BindingKind::unusable,
          "the first unbound alias is unusable before the binding");
    check(binding_of(set, other) == nfsu::BindingKind::unusable,
          "the second unbound alias is unusable before the binding");
    check(binding_of(set, body_hash()) == nfsu::BindingKind::unusable,
          "the bound hash is unusable before the binding");

    nfsu::RuntimeSkinBinding binding;
    std::string error;
    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 4, set, binding,
                                  error),
          "the skin binds over a multi-claim record: " + error);
    check_size(binding.displaced.size(), 1U, "the multi-claim record is displaced");
    check_u32(binding.displaced[0].hash, body_hash(),
              "it is displaced under the bound hash it claimed");
    // Two, not three: info +36 and the hash table name the same hash, and
    // one hash is one claim.
    check_size(binding.preserved_claims.size(), 2U,
               "both unbound claims are preserved, the repeated one once");
    if (binding.preserved_claims.size() == 2U) {
        check_u32(binding.preserved_claims[0].hash, alias,
                  "the record's own hash comes first");
        check_u32(binding.preserved_claims[1].hash, other, "then the remaining claim");
        check_size(binding.preserved_claims[0].source, 1U, "the preserved claim's source");
        check_size(binding.preserved_claims[0].pack, 3U, "the preserved claim's pack");
        check_size(binding.preserved_claims[0].record, 5U, "the preserved claim's record");
    } else {
        check(false, "the preserved claims cannot be inspected");
    }
    check_size(set.unusable_claims.size(), 2U, "the preserved claims are in the set");

    // The point of keeping them: a hash this binding never named resolves
    // exactly as it did before.
    check(binding_of(set, alias) == nfsu::BindingKind::unusable,
          "the first unbound alias is still unusable after the binding");
    check(binding_of(set, other) == nfsu::BindingKind::unusable,
          "the second unbound alias is still unusable after the binding");
    check(binding_of(set, body_hash()) == nfsu::BindingKind::resolved,
          "the bound hash now resolves to the generated texture");
    check(binding_of(set, skin.target_hash) == nfsu::BindingKind::resolved,
          "the target hash resolves to the generated texture");
    check(binding_of(set, 0x11111111U) == nfsu::BindingKind::resolved,
          "the unrelated pack texture still resolves");
    check(binding_of(set, 0x55555555U) == nfsu::BindingKind::unresolved,
          "a hash nothing ever claimed is still unresolved");
    check(set.decoded_bytes == rgba_sum(set),
          "decoded_bytes still sums the retained textures");

    // A hash the retained claim list already covers is not preserved a
    // second time: one entry per hash is what the decode leaves, and
    // resolve_material counts the claim list once however many entries
    // hold the hash.
    nfsu::RenderTextureSet already;
    nfsu::RenderTexture covered = multi;
    already.textures.push_back(covered);
    already.unusable_claims.push_back(nfsu::UnusableClaim{alias, 9, 9, 9});
    already.decoded_bytes = rgba_sum(already);
    nfsu::RuntimeSkinBinding no_duplicate;
    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 4, already,
                                  no_duplicate, error),
          "the skin binds over a record whose claim is already held: " + error);
    check_size(no_duplicate.preserved_claims.size(), 1U,
               "only the hash the list did not already hold is preserved");
    if (!no_duplicate.preserved_claims.empty()) {
        check_u32(no_duplicate.preserved_claims[0].hash, other,
                  "and it is the one that was not held");
    }
    check_size(already.unusable_claims.size(), 2U, "the claim list holds each hash once");
    check(binding_of(already, alias) == nfsu::BindingKind::unusable,
          "the already-held hash is still unusable");
    check(binding_of(already, other) == nfsu::BindingKind::unusable,
          "the newly preserved hash is unusable");

    // Two removed records claiming one unbound hash give it one entry too.
    nfsu::RenderTextureSet twice;
    nfsu::RenderTexture first = pack_texture(body_hash(), 2, 9, 1);
    first.claims[0] = body_hash();
    first.claims[1] = other;
    first.claim_count = 2;
    first.valid = false;
    nfsu::RenderTexture second = pack_texture(skin.target_hash, 2, 9, 2);
    second.claims[0] = skin.target_hash;
    second.claims[1] = other;
    second.claim_count = 2;
    second.valid = false;
    twice.textures.push_back(first);
    twice.textures.push_back(second);
    twice.decoded_bytes = rgba_sum(twice);
    check(binding_of(twice, other) == nfsu::BindingKind::unusable,
          "the shared unbound hash is unusable before the binding");
    nfsu::RuntimeSkinBinding shared_hash;
    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 4, twice,
                                  shared_hash, error),
          "the skin binds over two records sharing an unbound hash: " + error);
    check_size(shared_hash.displaced.size(), 2U, "both records are displaced");
    check_size(shared_hash.preserved_claims.size(), 1U,
               "the shared unbound hash is preserved once");
    check(binding_of(twice, other) == nfsu::BindingKind::unusable,
          "the shared unbound hash is still unusable");

    // A record all of whose claims are bound leaves nothing behind.
    nfsu::RenderTextureSet all_bound;
    nfsu::RenderTexture both = pack_texture(skin.target_hash, 2, 9, 1);
    both.claims[0] = skin.target_hash;
    both.claims[1] = body_hash();
    both.claim_count = 2;
    both.valid = false;
    all_bound.textures.push_back(both);
    all_bound.decoded_bytes = rgba_sum(all_bound);
    nfsu::RuntimeSkinBinding nothing_kept;
    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 4, all_bound,
                                  nothing_kept, error),
          "a record whose every claim is bound binds: " + error);
    check(nothing_kept.preserved_claims.empty(),
          "a record with no unbound claim preserves nothing");
    check(all_bound.unusable_claims.empty(), "and adds nothing to the set");

    // A refused binding preserves nothing either, because it changes
    // nothing at all. The set carries both a claimant and existing claims,
    // so there is something for a refusal to have damaged.
    nfsu::RenderTextureSet untouched;
    untouched.textures.push_back(multi);
    untouched.unusable_claims.push_back(nfsu::UnusableClaim{alias, 7, 7, 7});
    untouched.unusable_claims.push_back(nfsu::UnusableClaim{body_hash(), 8, 8, 8});
    untouched.decoded_bytes = rgba_sum(untouched);
    nfsu::RuntimeSkinBinding refused;
    // A well-formed list of one hash that is not the target. It takes the
    // wrong-first-hash branch, not the list-size one, which is why the
    // message is checked: without it the two are the same "false" and the
    // case could drift onto the other branch unnoticed.
    check(!nfsu::bind_runtime_skin(skin, {body_hash()}, 4, untouched, refused, error),
          "a first hash that is not the target is still refused here");
    check(error.find("the first bound hash must be the target hash") != std::string::npos,
          "and it is that branch and not the list size: " + error);
    check(reports_nothing(refused), "a refused binding reports nothing at all");
    check_size(untouched.textures.size(), 1U,
               "a refused binding leaves the textures alone");
    check_size(untouched.unusable_claims.size(), 2U,
               "a refused binding leaves the claims alone");
    check(untouched.unusable_claims[0].hash == alias &&
              untouched.unusable_claims[1].hash == body_hash(),
          "a refused binding leaves the claim list in order");
    check(untouched.decoded_bytes == rgba_sum(untouched),
          "a refused binding leaves decoded_bytes alone");
}


// Deep equality, so that "the set is untouched" is a claim about the set
// and not about its size.
bool same_set(const nfsu::RenderTextureSet& a, const nfsu::RenderTextureSet& b) {
    if (a.textures.size() != b.textures.size() ||
        a.unusable_claims.size() != b.unusable_claims.size() ||
        a.shadowed.size() != b.shadowed.size() ||
        a.blocked_sources.size() != b.blocked_sources.size() ||
        a.decoded_bytes != b.decoded_bytes || a.not_wanted != b.not_wanted) {
        return false;
    }
    // Element-wise, not by size: a member rewritten at the same length is
    // exactly what "untouched" has to exclude.
    for (std::size_t i = 0; i < a.shadowed.size(); ++i) {
        const nfsu::ShadowedTexture& x = a.shadowed[i];
        const nfsu::ShadowedTexture& y = b.shadowed[i];
        if (x.hash != y.hash || x.source != y.source || x.pack != y.pack ||
            x.record != y.record || x.by_source != y.by_source) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.textures.size(); ++i) {
        const nfsu::RenderTexture& x = a.textures[i];
        const nfsu::RenderTexture& y = b.textures[i];
        if (x.hash != y.hash || x.claims != y.claims || x.claim_count != y.claim_count ||
            x.width != y.width || x.height != y.height || x.rgba != y.rgba ||
            x.valid != y.valid || x.problem != y.problem || x.source != y.source ||
            x.pack != y.pack || x.record != y.record) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.unusable_claims.size(); ++i) {
        const nfsu::UnusableClaim& x = a.unusable_claims[i];
        const nfsu::UnusableClaim& y = b.unusable_claims[i];
        if (x.hash != y.hash || x.source != y.source || x.pack != y.pack ||
            x.record != y.record) {
            return false;
        }
    }
    return a.blocked_sources == b.blocked_sources;
}

// The refusal the arguments cannot reach.
//
// Every check bind_runtime_skin makes on its arguments returns before it
// touches anything, so none of them exercises what the "leaves the set
// untouched" guarantee is actually about: the claim list rebuilt into a
// local, the single push_back the set is mutated through, and the noexcept
// swap that commits the list. Only an allocation that throws goes through
// that machinery, so one is made to throw - at every allocation the call
// makes, in turn.
//
// The set has something to lose at each of those points: a texture that
// stays, two that are removed, each claiming an unbound hash that has to be
// preserved, and an existing claim the rebuild has to carry over.
//
// It is also shaped so that the rebuilt list is longer than the live one,
// which is what makes the commit itself a place a defect could hide:
// assigning the rebuilt list over the live one instead of swapping it would
// have to allocate, after the textures have already been changed. With a
// rebuilt list no longer than the live one that assignment never allocates
// and the two are indistinguishable.
void test_binding_allocation_failure() {
    const nfsu::RuntimeSkin skin = fixture_skin();
    const std::uint32_t alias = 0x33333333U;
    const std::uint32_t second_alias = 0x55555555U;

    nfsu::RenderTextureSet baseline;
    baseline.textures.push_back(pack_texture(0x11111111U, 2, 7, 0));
    nfsu::RenderTexture multi = pack_texture(skin.target_hash, 2, 9, 1);
    multi.claims[1] = alias;
    multi.claim_count = 2;
    multi.valid = false;
    baseline.textures.push_back(multi);
    nfsu::RenderTexture second = pack_texture(body_hash(), 2, 11, 2);
    second.claims[1] = second_alias;
    second.claim_count = 2;
    second.valid = false;
    baseline.textures.push_back(second);
    baseline.unusable_claims.push_back(nfsu::UnusableClaim{0x22222222U, 1, 3, 7});
    // The three members bind_runtime_skin has no business touching. They are
    // given values so that "untouched" is a claim about their contents and
    // not about three defaults that a wipe would leave looking the same.
    baseline.shadowed.push_back(nfsu::ShadowedTexture{0x66666666U, 1, 2, 3, 0});
    baseline.shadowed.push_back(nfsu::ShadowedTexture{0x77777777U, 2, 3, 4, 1});
    baseline.blocked_sources.push_back(5);
    baseline.not_wanted = 9;
    baseline.decoded_bytes = rgba_sum(baseline);

    // Built before the counter is armed: an initializer list at the call
    // site would allocate outside the call and throw out of this test.
    const std::vector<std::uint32_t> hashes = {skin.target_hash, body_hash()};

    std::size_t refusals = 0;
    bool bound = false;
    for (std::size_t attempt = 1; attempt <= 256 && !bound; ++attempt) {
        nfsu::RenderTextureSet probe = baseline;
        nfsu::RuntimeSkinBinding binding;
        std::string error;
        bool ok = false;
        bool escaped = false;
        g_allocations = 0;
        g_fail_allocation = attempt;
        try {
            ok = nfsu::bind_runtime_skin(skin, hashes, 4, probe, binding, error);
        } catch (...) {
            escaped = true;
        }
        g_fail_allocation = 0;
        check(!escaped, "no exception escapes the binding at allocation " +
                            std::to_string(attempt));
        if (escaped) {
            return;
        }
        if (ok) {
            // The counter ran past the end of the call, so every allocation
            // it makes has now been failed in turn.
            bound = true;
            check(!same_set(probe, baseline), "the binding that succeeded changed the set");
            check(error.empty(), "a successful binding reports no error");
            check_size(binding.preserved_claims.size(), 2U,
                       "both unbound claims were preserved");
            check_size(probe.unusable_claims.size(), 3U,
                       "the committed claim list is longer than the one it replaced");
            break;
        }
        ++refusals;
        check(!error.empty(),
              "an allocation failure is reported at allocation " + std::to_string(attempt));
        check(error.find("out of memory") != std::string::npos ||
                  error.find("larger than a vector can hold") != std::string::npos,
              "and says it was an allocation: " + error);
        check(same_set(probe, baseline),
              "the set is byte-for-byte what it was at allocation " +
                  std::to_string(attempt));
        check(reports_nothing(binding),
              "and the binding reports nothing at allocation " + std::to_string(attempt));
    }
    check(bound, "the binding succeeds once no allocation fails");
    check(refusals > 0, "at least one allocation was made to fail");

    // The other catch handler. Nothing reachable throws std::length_error
    // here - a vector throws it before it allocates, and operator new never
    // does - so it is injected where an allocation would throw. That pins
    // the handler, its message and the rollback, not a reachable path.
    {
        nfsu::RenderTextureSet probe = baseline;
        nfsu::RuntimeSkinBinding binding;
        std::string error;
        bool ok = true;
        bool escaped = false;
        g_allocations = 0;
        g_fail_with_length_error = true;
        g_fail_allocation = 1;
        try {
            ok = nfsu::bind_runtime_skin(skin, hashes, 4, probe, binding, error);
        } catch (...) {
            escaped = true;
        }
        g_fail_allocation = 0;
        g_fail_with_length_error = false;
        check(!escaped, "no std::length_error escapes the binding");
        check(!ok, "an injected length_error refuses the binding");
        check(error.find("larger than a vector can hold") != std::string::npos,
              "and says the texture set is too large: " + error);
        check(same_set(probe, baseline), "and leaves the set alone");
        check(reports_nothing(binding), "and reports nothing");
    }
}

// ---------------------------------------------------------------------------
// The prepared scene
// ---------------------------------------------------------------------------

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

nfsu::MeshVertex vertex(float px, float py, float pz, float u, float v) {
    nfsu::MeshVertex out;
    out.px = px;
    out.py = py;
    out.pz = pz;
    out.u = u;
    out.v = v;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
    return out;
}

nfsu::MeshMaterial material(std::uint32_t first, std::uint32_t count, std::uint32_t slot,
                            unsigned char tag) {
    nfsu::MeshMaterial out;
    out.first_index = first;
    out.index_count = count;
    out.texture_slot = slot;
    out.material_key_slot = 0x100U + tag;
    out.flags = 0x5000U + tag;
    out.offset = 0x1000U + tag;
    for (int k = 0; k < 3; ++k) {
        out.bounds_min[k] = -1.0F - static_cast<float>(k) - tag;
        out.bounds_max[k] = 1.0F + static_cast<float>(k) + tag;
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

// One body mesh whose only material slot names <car>_SKIN1, which is what
// the 240SX geometry does for 637 of its material groups.
nfsu::GeometryReport body_geometry() {
    nfsu::GeometryReport report;
    nfsu::Mesh body;
    body.index = 0;
    body.name = "240SX_BODY_A";
    body.valid = true;
    body.vertices = {vertex(0, 0, -1, 0, 0), vertex(0, 1, -1, 0, 1),
                     vertex(0, 1, 1, 1, 1), vertex(0, 0, 1, 1, 0)};
    body.triangles = {{0, 1, 2}, {0, 2, 3}};
    body.texture_hashes = {body_hash()};
    body.materials = {material(0, 6, 0, 1)};
    body.directory_hash = 0x01020304U;
    body.directory_offset = 0x200U;
    body.directory_size = 0x80U;
    body.has_directory_entry = true;
    report.meshes.push_back(body);
    report.mesh_directory_valid = true;
    return report;
}

// The set the viewer decodes when no runtime-skin option is given: the car
// pack only, decoded through the same API the viewer uses.
nfsu::RenderTextureSet decode_car_pack(const Source& car) {
    nfsu::RenderTextureSet set;
    std::string error;
    check(nfsu::decode_render_textures(car.bytes, car.pack, nfsu::RenderTextureLimits{},
                                       set, error),
          "the car pack decodes: " + error);
    return set;
}

void test_prepared_scene() {
    Fixture fixture = make_fixture();
    const nfsu::GeometryReport geometry = body_geometry();
    const nfsu::MeshSelection all;
    const nfsu::SceneLimits limits;

    // Without a runtime skin the body hash is in no pack, so the group is
    // unresolved and drawn with the fallback. That is the behaviour every
    // run without --skin-car keeps.
    nfsu::RenderTextureSet plain = decode_car_pack(*fixture.car);
    nfsu::PreparedCarScene before;
    std::string error;
    check(nfsu::prepare_car_scene(geometry, plain, all, limits, before, error),
          "the plain scene prepares: " + error);
    check_size(before.draws.size(), 1U, "one material group");
    check(before.draws[0].binding == nfsu::BindingKind::unresolved,
          "the body group is unresolved without a runtime skin");
    check(before.textures.empty(), "no texture is prepared without a runtime skin");
    check_size(before.unresolved_hashes.size(), 1U, "the body hash is reported unresolved");

    // With one, the same geometry and the same pack resolve the body group
    // to the generated texture, and the scene passes its own validator.
    nfsu::RenderTextureSet bound = decode_car_pack(*fixture.car);
    const nfsu::RuntimeSkin skin = fixture_skin();
    nfsu::RuntimeSkinBinding binding;
    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 1, bound, binding,
                                  error),
          "the skin binds into the decoded set: " + error);
    nfsu::PreparedCarScene after;
    check(nfsu::prepare_car_scene(geometry, bound, all, limits, after, error),
          "the bound scene prepares: " + error);
    check(nfsu::validate_prepared_scene(after, error),
          "the bound scene validates: " + error);
    check_size(after.draws.size(), 1U, "one material group");
    check(after.draws[0].binding == nfsu::BindingKind::resolved,
          "the body group resolves to the generated texture");
    check_size(after.textures.size(), 1U, "exactly one texture is prepared");
    check(after.textures[0].rgba == skin.rgba, "the prepared texture is the composed one");
    check_u32(after.draws[0].texture_hash, body_hash(), "the draw keeps the slot hash");
    check_size(after.draws[0].texture_source, binding.source,
               "the draw carries the generated provenance");
    check(after.unresolved_hashes.empty(), "nothing is left unresolved");

    // A pack record claiming the body hash would make the group unusable if
    // it stayed; binding removes it, so the group resolves either way.
    nfsu::RenderTextureSet rival = decode_car_pack(*fixture.car);
    rival.textures.push_back(pack_texture(body_hash(), 2, 3, 0));
    rival.decoded_bytes = rgba_sum(rival);
    nfsu::PreparedCarScene ambiguous;
    check(nfsu::prepare_car_scene(geometry, rival, all, limits, ambiguous, error),
          "the rival scene prepares: " + error);
    check(ambiguous.draws[0].binding == nfsu::BindingKind::resolved,
          "one pack claimant alone resolves");
    nfsu::RuntimeSkinBinding rival_binding;
    check(nfsu::bind_runtime_skin(skin, {skin.target_hash, body_hash()}, 1, rival,
                                  rival_binding, error),
          "the skin binds over a pack claimant: " + error);
    check_size(rival_binding.displaced.size(), 1U, "the pack claimant is displaced");
    nfsu::PreparedCarScene resolved;
    check(nfsu::prepare_car_scene(geometry, rival, all, limits, resolved, error),
          "the displaced scene prepares: " + error);
    check(resolved.draws[0].binding == nfsu::BindingKind::resolved,
          "the generated texture is the single claimant");
    check(resolved.textures.size() == 1 && resolved.textures[0].rgba == skin.rgba,
          "the generated texture won over the pack record");
}

} // namespace

// Replacing these replaces them for the whole test program, which is the
// point: the allocations bind_runtime_skin makes are inside libstdc++
// containers and there is no hook to reach them. Only the plain forms are
// replaced; an over-aligned allocation keeps the library's own pair, which
// stays consistent with itself.
void* operator new(std::size_t size) { return counted_allocate(size); }
void* operator new[](std::size_t size) { return counted_allocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    test_target_path();
    test_target_surface();
    test_target_surface_pairing();
    test_swapped_target();
    test_identity_fields();
    test_compose();
    test_source_priority();
    test_fallback();
    test_missing_target();
    test_binding_rules();
    test_binding_displaces();
    test_binding_preserves_claims();
    test_binding_allocation_failure();
    test_prepared_scene();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "runtime skin tests passed, " << g_checks << " checks\n";
    return 0;
}
