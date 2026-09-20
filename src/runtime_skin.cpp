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

std::uint32_t read_u32_le(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

// The record the provenance names, or null when the source list is not the
// one the provenance was resolved against.
const TextureRecord* record_at(
    const std::vector<SkinInputSource>& sources,
    const SkinTextureProvenance& target) noexcept {
    if (target.source_index >= sources.size()) {
        return nullptr;
    }
    const SkinInputSource& source = sources[target.source_index];
    if (source.bytes == nullptr || target.pack_index >= source.packs.size()) {
        return nullptr;
    }
    const TexturePack* pack = source.packs[target.pack_index];
    if (pack == nullptr) {
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
    // chosen from.
    if (found == nullptr || found->hash != target.record_hash ||
        found->info_offset != target.info_offset || found->format != target.format ||
        found->format_code != target.format_code ||
        found->d3d_format != target.d3d_format || found->width != target.width ||
        found->height != target.height || found->data_size != target.data_size ||
        found->storage != target.storage || found->blob_offset != target.blob_offset ||
        found->blob_size != target.blob_size ||
        found->mip_count != target.mip_count) {
        return nullptr;
    }
    return found;
}

} // namespace

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
    const TextureRecord* record = record_at(sources, target);
    if (record == nullptr) {
        error = "the target provenance does not name a record of these sources";
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
    return true;
}

bool compose_runtime_skin(
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
        }
        std::vector<std::size_t> remove_claims;
        std::vector<UnusableClaim> displaced_claims;
        for (std::size_t i = 0; i < set.unusable_claims.size(); ++i) {
            if (bound(set.unusable_claims[i].hash)) {
                remove_claims.push_back(i);
                displaced_claims.push_back(set.unusable_claims[i]);
            }
        }

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
        for (std::size_t k = remove_claims.size(); k > 0; --k) {
            const std::size_t i = remove_claims[k - 1];
            set.unusable_claims.erase(set.unusable_claims.begin() +
                                      static_cast<std::ptrdiff_t>(i));
        }
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
