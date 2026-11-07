#include "nfsu/runtime_skin.h"

#include <algorithm>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <utility>

namespace nfsu {

namespace {

std::string hex8(std::uint8_t value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", static_cast<unsigned int>(value));
    return buffer;
}

std::string hex32(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned int>(value));
    return buffer;
}

// The size of RenderTexture::claims. A record answers to its own hash
// field as well, which RenderTexture::claims_hash also counts, so one more
// slot than this holds every hash one texture can answer to.
constexpr std::size_t kClaimSlots =
    std::tuple_size<decltype(RenderTexture::claims)>::value;

std::uint32_t read_u32_le(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

// The identity of a record located in a source list, filled from the
// record, its pack and its source. Every field is a copy, so neither side
// of the comparison below derives anything.
SkinTargetIdentity identity_of(
    std::size_t source_index,
    std::size_t pack_index,
    const SkinInputSource& source,
    const TexturePack& pack,
    const TextureRecord& record) noexcept {
    SkinTargetIdentity out;
    out.resolved = true;
    out.source_index = source_index;
    out.pack_index = pack_index;
    // locate_target has already refused a source with no bytes, so this
    // pointer is neither null nor the data() of an empty vector.
    out.source_bytes = source.bytes->data();
    out.source_byte_size = source.bytes->size();
    out.pack_root_offset = pack.root_offset;
    out.pack_hash = pack.pack_hash;
    out.record_index = record.index;
    out.record_hash = record.hash;
    out.table_hash = record.table_hash;
    out.info_offset = record.info_offset;
    out.storage = record.storage;
    out.format = record.format;
    out.format_code = record.format_code;
    out.d3d_format = record.d3d_format;
    out.width = record.width;
    out.height = record.height;
    out.mip_count = record.mip_count;
    out.data_size = record.data_size;
    out.palette_bytes = record.palette_size;
    out.top_level_size = record.top_level_size;
    out.blob_offset = record.blob_offset;
    out.blob_size = record.blob_size;
    return out;
}

// The identity a provenance states, which is everything but the buffer: a
// provenance is a position and a shape, and does not carry the bytes it was
// resolved against.
SkinTargetIdentity provenance_identity(const SkinTextureProvenance& target) noexcept {
    SkinTargetIdentity out;
    if (!target.resolved) {
        return out;
    }
    out.resolved = true;
    out.source_index = target.source_index;
    out.pack_index = target.pack_index;
    out.pack_root_offset = target.pack_root_offset;
    out.pack_hash = target.pack_hash;
    out.record_index = target.record_index;
    out.record_hash = target.record_hash;
    out.table_hash = target.table_hash;
    out.info_offset = target.info_offset;
    out.storage = target.storage;
    out.format = target.format;
    out.format_code = target.format_code;
    out.d3d_format = target.d3d_format;
    out.width = target.width;
    out.height = target.height;
    out.mip_count = target.mip_count;
    out.data_size = target.data_size;
    out.palette_bytes = target.record_palette_bytes;
    out.top_level_size = target.top_level_size;
    out.blob_offset = target.blob_offset;
    out.blob_size = target.blob_size;
    return out;
}

// Everything but the buffer, for the two sides that are known to share one.
//
// The left side is read through a structured binding, which has to name
// exactly as many members as the struct has, so a field added to
// SkinTargetIdentity does not compile until it has been decided about here.
// That is deliberate: an incomplete comparison here is the one failure that
// makes no noise at all - one target's pixels composited, labelled and
// bound as another's - and it is the defect this layer has already had
// twice, first from comparing only the name hash and the info offset, then
// from an identity that was a position and not a file.
bool same_record(const SkinTargetIdentity& a, const SkinTargetIdentity& b) noexcept {
    const auto& [resolved, source_index, pack_index, pack_root_offset, pack_hash,
                 record_index, record_hash, table_hash, info_offset, storage, format,
                 format_code, d3d_format, width, height, mip_count, data_size,
                 palette_bytes, top_level_size, blob_offset, blob_size, source_bytes,
                 source_byte_size] = a;
    // The buffer is the one part this comparison leaves out, on purpose;
    // same_skin_target adds it. Named here only so the binding above stays
    // exhaustive.
    static_cast<void>(source_bytes);
    static_cast<void>(source_byte_size);
    return resolved && b.resolved && source_index == b.source_index &&
           pack_index == b.pack_index && pack_root_offset == b.pack_root_offset &&
           pack_hash == b.pack_hash && record_index == b.record_index &&
           record_hash == b.record_hash && table_hash == b.table_hash &&
           info_offset == b.info_offset && storage == b.storage && format == b.format &&
           format_code == b.format_code && d3d_format == b.d3d_format &&
           width == b.width && height == b.height && mip_count == b.mip_count &&
           data_size == b.data_size && palette_bytes == b.palette_bytes &&
           top_level_size == b.top_level_size && blob_offset == b.blob_offset &&
           blob_size == b.blob_size;
}

std::string describe_identity(const SkinTargetIdentity& identity) {
    if (!identity.resolved) {
        return "no record";
    }
    // The pack root offset and the buffer size are in here because two
    // identities can differ in nothing else: two genuine roots parsed from
    // one file share a pack index, and two source lists share everything
    // but the buffer.
    return "source " + std::to_string(identity.source_index) + " pack " +
           std::to_string(identity.pack_index) + " at " +
           std::to_string(identity.pack_root_offset) + " record " +
           std::to_string(identity.record_index) + " hash " +
           hex32(identity.record_hash) + " code " + hex8(identity.format_code) + " " +
           std::to_string(identity.width) + "x" + std::to_string(identity.height) +
           " of a " + std::to_string(identity.source_byte_size) + " byte source";
}

// The record the provenance names, with its identity, or null with `error`
// set when this source list is not the one the provenance was resolved
// against. `identity` is left unresolved on every failure.
const TextureRecord* locate_target(
    const std::vector<SkinInputSource>& sources,
    const SkinTextureProvenance& target,
    SkinTargetIdentity& identity,
    std::string& error) {
    identity = SkinTargetIdentity{};
    const char* const not_here =
        "the target provenance does not name a record of these sources";
    if (target.source_index >= sources.size()) {
        error = not_here;
        return nullptr;
    }
    const SkinInputSource& source = sources[target.source_index];
    if (source.bytes == nullptr || target.pack_index >= source.packs.size()) {
        error = not_here;
        return nullptr;
    }
    const TexturePack* pack = source.packs[target.pack_index];
    if (pack == nullptr) {
        error = not_here;
        return nullptr;
    }
    // The bytes the payload is read from must be the ones the pack was
    // parsed from, at the size it was parsed at, and the pack's record
    // tables must have been read. The first two are the pairing rule
    // plan_body_skin_inputs and decode_sources both enforce; the third is
    // required by plan_body_skin_inputs, while decode_sources instead
    // treats it as an incomplete source. This read needs all three: a
    // buffer substituted for one of the same size stays inside every bound
    // that texture_payload checks, so it would be decoded through offsets
    // that describe a different file, under the provenance of a record that
    // file does not hold.
    // A pack with no parse source is one no parser ever ran on, and an
    // empty buffer has a data() that is null or unusable. Either would make
    // the pairing below compare null against null and pass, which would let
    // two unrelated sources carry the same identity.
    if (pack->parse_source == nullptr || source.bytes->empty()) {
        error = "the target source has no parsed bytes";
        return nullptr;
    }
    if (pack->parse_source != source.bytes->data() ||
        pack->file_size != source.bytes->size()) {
        error = "the target source's pack was not parsed from the bytes of that source";
        return nullptr;
    }
    if (!pack->tables_parsed) {
        error = "the target source's pack has no parsed record tables, so its records "
                "are unknown";
        return nullptr;
    }
    const TextureRecord* found = nullptr;
    if (target.record_index < pack->textures.size() &&
        pack->textures[target.record_index].index == target.record_index) {
        found = &pack->textures[target.record_index];
    } else {
        for (const TextureRecord& record : pack->textures) {
            if (record.index == target.record_index) {
                found = &record;
                break;
            }
        }
    }
    // The provenance keeps the record's raw identifiers, so a source list
    // that is not the one it came from is caught here rather than silently
    // composited from the wrong pixels.
    //
    // Everything the read below depends on is compared, not just the two
    // identifiers: a re-encoded variant of the same pack keeps the name and
    // the fixed-stride info-record offset, so a record of another format
    // would otherwise be read with the level size of the one the path was
    // chosen from. The comparison is the same SkinTargetIdentity
    // compose_runtime_skin makes between a surface and the assembled
    // target, so the two cannot drift apart.
    //
    // The buffer is not compared here, because both sides of this
    // comparison come from the same source list by construction. It is
    // carried out of here instead, so that compose_runtime_skin can compare
    // it against a surface that may have been read from another list.
    if (found == nullptr) {
        error = not_here;
        return nullptr;
    }
    const SkinTargetIdentity located =
        identity_of(target.source_index, target.pack_index, source, *pack, *found);
    if (!same_record(located, provenance_identity(target))) {
        error = not_here;
        return nullptr;
    }
    identity = located;
    return found;
}

} // namespace

SkinTargetIdentity skin_target_identity(
    const std::vector<SkinInputSource>& sources,
    const SkinTextureProvenance& target) {
    SkinTargetIdentity out;
    std::string ignored;
    locate_target(sources, target, out, ignored);
    return out;
}

bool same_skin_target(const SkinTargetIdentity& a, const SkinTargetIdentity& b) noexcept {
    return same_record(a, b) && a.source_bytes == b.source_bytes &&
           a.source_byte_size == b.source_byte_size;
}

bool choose_skin_target_path(const SkinTextureProvenance& target, SkinTargetChoice& choice) {
    choice = SkinTargetChoice{};
    if (!target.resolved) {
        choice.reason = "the target hash resolved to no record";
        return false;
    }
    if (!target.record_valid) {
        choice.reason = "the target record is unusable: " +
                        (target.record_problem.empty() ? std::string("invalid texture record")
                                                       : target.record_problem);
        return false;
    }
    // classify_format only reports a8r8g8b8 for format code 0x20 with
    // D3DFORMAT 21, and p8 for the three paletted codes with D3DFORMAT 41,
    // so the format alone decides both cases below.
    if (target.format == PixelFormat::a8r8g8b8) {
        choice.proven = true;
        choice.path = SkinTargetType::type32;
        choice.object_type_byte = target.format_code;
        choice.expanded_on_upload = false;
        choice.reason =
            "format code 0x20 stays in the texture object at +0x4A, so 0x005AA262 "
            "dispatches to the type-32 composite at 0x005A8A50";
        return true;
    }
    if (target.format == PixelFormat::p8) {
        choice.proven = true;
        choice.path = SkinTargetType::type32;
        choice.object_type_byte = 0x20;
        choice.expanded_on_upload = true;
        choice.reason =
            "the upload at 0x00407DB0 expands a D3DFORMAT-41 record through its own "
            "palette and writes 0x20 over +0x4A at 0x00407EB1, so 0x005AA262 dispatches "
            "to the type-32 composite at 0x005A8A50";
        return true;
    }
    choice.reason = "no traced dispatch composites a record of format " +
                    std::string(pixel_format_name(target.format)) + " (code " +
                    hex8(target.format_code) + ", D3DFORMAT " +
                    std::to_string(target.d3d_format) +
                    "): 0x005A8A50 returns at 0x005A8A7E unless the object type byte is 0x20";
    return false;
}

bool read_skin_target_surface(
    const std::vector<SkinInputSource>& sources,
    const SkinTextureProvenance& target,
    const SkinInputLimits& limits,
    SkinTargetSurface& out,
    std::string& error) {
    out = SkinTargetSurface{};
    error.clear();
    if (!choose_skin_target_path(target, out.choice)) {
        error = out.choice.reason;
        out = SkinTargetSurface{};
        return false;
    }
    std::string located;
    SkinTargetIdentity identity;
    const TextureRecord* record = locate_target(sources, target, identity, located);
    if (record == nullptr) {
        error = located;
        out = SkinTargetSurface{};
        return false;
    }
    const std::uint64_t pixels = std::uint64_t{record->width} * record->height;
    // The effective bound is the smaller of the caller's and the path's own
    // cap, the same lowering-only rule SkinInputLimits::max_pixels has.
    const std::size_t type_cap = out.choice.path == SkinTargetType::type8
                                     ? kSkinMaxType8Pixels
                                     : kSkinMaxType32Pixels;
    const std::size_t cap = std::min<std::size_t>(limits.max_pixels, type_cap);
    if (pixels == 0) {
        error = "the target record has a zero dimension";
        out = SkinTargetSurface{};
        return false;
    }
    if (pixels > cap) {
        error = "the target is " + std::to_string(record->width) + "x" +
                std::to_string(record->height) + ", over the " + std::to_string(cap) +
                " pixel bound";
        out = SkinTargetSurface{};
        return false;
    }
    if (record->data_size > limits.max_payload_bytes) {
        error = "the target record declares " + std::to_string(record->data_size) +
                " payload bytes, over the bound";
        out = SkinTargetSurface{};
        return false;
    }
    if (pixels * 4ULL > limits.max_total_bytes) {
        error = "the target surface exceeds the total byte budget";
        out = SkinTargetSurface{};
        return false;
    }

    try {
        std::uint64_t level0 = 0;
        if (!level_size(record->format, record->width, record->height, level0)) {
            error = "the target record has no computable level size";
            out = SkinTargetSurface{};
            return false;
        }
        std::vector<unsigned char> payload;
        std::size_t unused_input = 0;
        P8Palette stored{};
        const bool paletted = record->format == PixelFormat::p8;
        const SkinInputSource& source = sources[target.source_index];
        if (!texture_payload(source.bytes->data(), source.bytes->size(), *record, payload,
                             unused_input, error, paletted ? &stored : nullptr)) {
            out = SkinTargetSurface{};
            return false;
        }
        if (payload.size() < level0) {
            error = "the target record payload is shorter than its level 0";
            out = SkinTargetSurface{};
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(pixels);
        out.pixels.resize(count);
        if (paletted) {
            // 0x00407E70..0x00407E7B copies a palette entry into the pixel
            // whole, so no lane is rearranged.
            const SkinPalette palette = skin_palette_from_p8(stored);
            for (std::size_t i = 0; i < count; ++i) {
                out.pixels[i] = palette[payload[i]];
            }
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                out.pixels[i] = read_u32_le(payload.data() + i * 4U);
            }
        }
    } catch (const std::bad_alloc&) {
        error = "out of memory while reading the target surface";
        out = SkinTargetSurface{};
        return false;
    } catch (const std::length_error&) {
        error = "the target surface is larger than a vector can hold";
        out = SkinTargetSurface{};
        return false;
    }
    out.width = record->width;
    out.height = record->height;
    // The identity of the record actually located, buffer included, so the
    // surface carries where its pixels came from and compose_runtime_skin
    // can refuse one that is not the assembled target's.
    out.identity = identity;
    return true;
}

bool compose_runtime_skin(
    const std::vector<SkinInputSource>& sources,
    const AssembledSkinInputs& inputs,
    const SkinTargetSurface& target,
    SkinMarkerCache& markers,
    RuntimeSkin& out,
    std::string& error) {
    out = RuntimeSkin{};
    error.clear();
    if (!target.choice.proven) {
        error = "the target record does not prove a compositor path: " + target.choice.reason;
        return false;
    }
    if (target.choice.path != SkinTargetType::type32) {
        // Unreachable while choose_skin_target_path only ever proves the
        // type-32 path; kept so the refusal is a message and not a crash.
        error = "only the type-32 composite produces pixels whose lane order is proven";
        return false;
    }
    // Pixels in a vector carry no provenance, so without this two valid
    // same-sized type-32 records are interchangeable and the composite
    // would label and bind one target's pixels as the other's. The target
    // is re-located in `sources` rather than taken from the plan, so the
    // comparison includes the buffer and decides a file and not a position.
    SkinTargetIdentity wanted;
    std::string located;
    if (locate_target(sources, inputs.plan().target_texture, wanted, located) ==
        nullptr) {
        error = "the assembled inputs name no record of these sources: " + located;
        return false;
    }
    if (!same_skin_target(target.identity, wanted)) {
        error = "the target surface was not read from the record the assembled inputs "
                "resolved: the surface holds " + describe_identity(target.identity) +
                ", the inputs name " + describe_identity(wanted);
        if (same_record(target.identity, wanted)) {
            error += "; the same position in two different source lists";
        }
        return false;
    }
    if (inputs.target() != target.choice.path) {
        error = std::string("the assembled inputs were built for the ") +
                skin_target_type_name(inputs.target()) + " path, the target record proves " +
                skin_target_type_name(target.choice.path);
        return false;
    }
    if (!inputs.plan().target_hash) {
        error = "the assembled inputs name no target hash, so the generated texture "
                "would answer to nothing";
        return false;
    }
    const BodySkinRequest& request = inputs.request();
    if (request.width != target.width || request.height != target.height) {
        error = "the assembled inputs are " + std::to_string(request.width) + "x" +
                std::to_string(request.height) + ", the target is " +
                std::to_string(target.width) + "x" + std::to_string(target.height);
        return false;
    }
    if (target.width == 0 || target.height == 0) {
        error = "the target has a zero dimension, so no texture can be generated";
        return false;
    }
    const std::uint64_t pixels = std::uint64_t{target.width} * target.height;
    if (target.pixels.size() < pixels) {
        error = "the target surface is shorter than its own dimensions";
        return false;
    }

    try {
        BodySkin32 composed;
        if (!composite_body_skin_32(request, target.pixels, markers, composed, error)) {
            return false;
        }
        if (composed.pixels.size() != pixels) {
            error = "the composite produced " + std::to_string(composed.pixels.size()) +
                    " pixels, expected " + std::to_string(pixels);
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(pixels);
        out.rgba.resize(count * 4U);
        // The same mapping decode_level applies to an A8R8G8B8 level: red is
        // dword byte 2, green byte 1, blue byte 0, GLOSS byte 3.
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t pixel = composed.pixels[i];
            out.rgba[i * 4U + 0] = static_cast<unsigned char>((pixel >> 16U) & 0xFFU);
            out.rgba[i * 4U + 1] = static_cast<unsigned char>((pixel >> 8U) & 0xFFU);
            out.rgba[i * 4U + 2] = static_cast<unsigned char>(pixel & 0xFFU);
            out.rgba[i * 4U + 3] = static_cast<unsigned char>((pixel >> 24U) & 0xFFU);
        }
        out.width = composed.width;
        out.height = composed.height;
    } catch (const std::bad_alloc&) {
        out = RuntimeSkin{};
        error = "out of memory while composing the runtime skin";
        return false;
    } catch (const std::length_error&) {
        out = RuntimeSkin{};
        error = "the generated skin is larger than a vector can hold";
        return false;
    }
    out.target_hash = inputs.plan().target_hash.value_or(0);
    out.path = target.choice.path;
    out.layers = request.layers.size();
    return true;
}

bool bind_runtime_skin(
    const RuntimeSkin& skin,
    const std::vector<std::uint32_t>& hashes,
    std::size_t source_index,
    RenderTextureSet& set,
    RuntimeSkinBinding& binding,
    std::string& error) {
    binding = RuntimeSkinBinding{};
    error.clear();
    if (hashes.empty() || hashes.size() > kMaxRuntimeSkinHashes) {
        error = "a runtime skin binds 1 to " + std::to_string(kMaxRuntimeSkinHashes) +
                " hashes, got " + std::to_string(hashes.size());
        return false;
    }
    if (hashes[0] != skin.target_hash) {
        error = "the first bound hash must be the target hash " + hex32(skin.target_hash) +
                ", got " + hex32(hashes[0]);
        return false;
    }
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        for (std::size_t j = i + 1; j < hashes.size(); ++j) {
            if (hashes[i] == hashes[j]) {
                error = "the bound hashes repeat " + hex32(hashes[i]);
                return false;
            }
        }
    }
    if (skin.width == 0 || skin.height == 0) {
        error = "the generated skin has a zero dimension";
        return false;
    }
    const std::uint64_t want = std::uint64_t{skin.width} * skin.height * 4ULL;
    if (skin.rgba.size() != want) {
        error = "the generated skin holds " + std::to_string(skin.rgba.size()) +
                " bytes, expected " + std::to_string(want);
        return false;
    }

    try {
        const auto bound = [&hashes](std::uint32_t value) {
            return std::find(hashes.begin(), hashes.end(), value) != hashes.end();
        };

        // The claim list is rebuilt whole rather than edited in place: the
        // claims that stay, in the order they were in, then the unbound
        // claims of the records being removed. It replaces
        // set.unusable_claims through a noexcept swap below, so the live
        // list is never left half-rewritten and nothing reserves on it.
        //
        // It is built first because the texture scan below needs to know
        // which hashes it already covers.
        std::vector<UnusableClaim> kept_claims;
        std::vector<UnusableClaim> displaced_claims;
        for (const UnusableClaim& claim : set.unusable_claims) {
            if (bound(claim.hash)) {
                displaced_claims.push_back(claim);
            } else {
                kept_claims.push_back(claim);
            }
        }
        const auto already_claimed = [&kept_claims, &binding](std::uint32_t value) {
            for (const UnusableClaim& claim : kept_claims) {
                if (claim.hash == value) {
                    return true;
                }
            }
            for (const UnusableClaim& claim : binding.preserved_claims) {
                if (claim.hash == value) {
                    return true;
                }
            }
            return false;
        };

        // Collected before anything is changed, so a throw here leaves the
        // set exactly as it was.
        std::vector<std::size_t> remove_textures;
        std::vector<DisplacedTexture> displaced;
        for (std::size_t i = 0; i < set.textures.size(); ++i) {
            const RenderTexture& texture = set.textures[i];
            std::uint32_t claimed = 0;
            bool hit = false;
            for (const std::uint32_t hash : hashes) {
                if (texture.claims_hash(hash)) {
                    claimed = hash;
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                continue;
            }
            remove_textures.push_back(i);
            displaced.push_back(DisplacedTexture{claimed, texture.source, texture.pack,
                                                 texture.record, texture.usable()});
            // One bound hash takes the whole record away, and a record
            // whose three hash fields disagree claims all three. The hashes
            // this binding never named are kept as unusable claims, the
            // way the decode keeps the claims of a record it leaves out,
            // so that resolve_material still reports them `unusable`
            // instead of silently turning them into `unresolved`.
            std::array<std::uint32_t, kClaimSlots + 1U> answered{};
            std::size_t answered_count = 0;
            const auto remember = [&answered, &answered_count](std::uint32_t value) {
                for (std::size_t k = 0; k < answered_count; ++k) {
                    if (answered[k] == value) {
                        return;
                    }
                }
                answered[answered_count] = value;
                ++answered_count;
            };
            // RenderTexture::hash always counts as claimed, then the first
            // claim_count entries of claims; duplicates among them name one
            // hash, not two.
            remember(texture.hash);
            const std::size_t used = std::min(texture.claim_count, texture.claims.size());
            for (std::size_t k = 0; k < used; ++k) {
                remember(texture.claims[k]);
            }
            for (std::size_t k = 0; k < answered_count; ++k) {
                // One entry per hash, which is what the decode leaves: a
                // hash the retained list already covers goes on matching
                // without a second entry, and resolve_material counts the
                // claim list once however many entries hold that hash.
                if (!bound(answered[k]) && !already_claimed(answered[k])) {
                    binding.preserved_claims.push_back(UnusableClaim{
                        answered[k], texture.source, texture.pack, texture.record});
                }
            }
        }
        kept_claims.insert(kept_claims.end(), binding.preserved_claims.begin(),
                           binding.preserved_claims.end());

        RenderTexture generated;
        generated.hash = hashes[0];
        for (std::size_t i = 0; i < hashes.size(); ++i) {
            generated.claims[i] = hashes[i];
        }
        generated.claim_count = hashes.size();
        generated.width = skin.width;
        generated.height = skin.height;
        generated.rgba = skin.rgba;
        generated.valid = true;
        generated.source = source_index;
        generated.pack = 0;
        generated.record = 0;

        // The last allocation before the set is touched; the push_back
        // below allocates too, but with the strong guarantee.
        binding.hashes = hashes;

        // push_back has the strong guarantee, so a failed allocation here
        // still leaves the set untouched.
        set.textures.push_back(std::move(generated));

        // From here nothing allocates. The collected indexes are ascending
        // and all below the appended texture, so erasing from the back
        // keeps every remaining one valid.
        for (std::size_t k = remove_textures.size(); k > 0; --k) {
            const std::size_t i = remove_textures[k - 1];
            set.textures.erase(set.textures.begin() + static_cast<std::ptrdiff_t>(i));
        }
        // noexcept, and the only change the claim list sees.
        set.unusable_claims.swap(kept_claims);
        // Recomputed rather than adjusted, so the field keeps meaning
        // exactly what it is documented to mean.
        set.decoded_bytes = 0;
        for (const RenderTexture& texture : set.textures) {
            set.decoded_bytes += texture.rgba.size();
        }

        binding.texture_index = set.textures.size() - 1;
        binding.source = source_index;
        binding.bytes = want;
        binding.displaced = std::move(displaced);
        binding.displaced_claims = std::move(displaced_claims);
    } catch (const std::bad_alloc&) {
        binding = RuntimeSkinBinding{};
        error = "out of memory while binding the runtime skin";
        return false;
    } catch (const std::length_error&) {
        binding = RuntimeSkinBinding{};
        error = "the texture set is larger than a vector can hold";
        return false;
    }
    return true;
}

} // namespace nfsu
