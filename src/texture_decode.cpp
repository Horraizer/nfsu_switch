#include "nfsu/texture_decode.h"

#include <algorithm>
#include <array>
#include <map>
#include <new>
#include <utility>

namespace nfsu {

namespace {

// The distinct hashes a record claims, in info, hash-table and blob-table
// order.
std::size_t claimed_hashes(const TextureRecord& record,
                           std::array<std::uint32_t, 3>& out) {
    std::size_t count = 0;
    const std::uint32_t candidates[3] = {
        record.hash, record.table_hash,
        static_cast<std::uint32_t>(record.blob_entry[0]) |
            (static_cast<std::uint32_t>(record.blob_entry[1]) << 8U) |
            (static_cast<std::uint32_t>(record.blob_entry[2]) << 16U) |
            (static_cast<std::uint32_t>(record.blob_entry[3]) << 24U)};
    const std::size_t used = record.storage == TextureStorage::jdlz ? 3 : 2;
    for (std::size_t i = 0; i < used; ++i) {
        if (std::find(out.begin(), out.begin() + count, candidates[i]) ==
            out.begin() + count) {
            out[count++] = candidates[i];
        }
    }
    return count;
}

// check_pairing is false only for decode_render_textures, whose callers may
// pass hand-built packs without a file size or parse source.
bool decode_sources(
    const std::vector<RenderTextureSource>& sources,
    bool check_pairing,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error) {
    set = RenderTextureSet{};
    RenderTextureSet result;
    // Hash -> earliest source that claims it.
    std::map<std::uint32_t, std::size_t> claims;
    try {
        bool blocked = false;
        for (std::size_t s = 0; s < sources.size(); ++s) {
            if (blocked) {
                result.blocked_sources.push_back(s);
                continue;
            }
            RenderTextureSource source = sources[s];
            if (limits.load_source && !limits.load_source(s, source, error)) return false;
            const bool label = sources.size() > 1 || source.packs.size() > 1;
            if (source.bytes == nullptr) {
                error = "texture source " + std::to_string(s) + " has no bytes";
                return false;
            }
            for (std::size_t p = 0; p < source.packs.size(); ++p) {
                const TexturePack* pack = source.packs[p];
                if (pack == nullptr ||
                    (check_pairing && (pack->parse_source != source.bytes->data() ||
                                       pack->file_size != source.bytes->size()))) {
                    error = "texture source " + std::to_string(s) + " pack " +
                            std::to_string(p) +
                            " was not parsed from the bytes of that source";
                    return false;
                }
            }
            bool complete = source.complete;
            std::vector<std::uint32_t> source_claims;
            for (std::size_t p = 0; p < source.packs.size(); ++p) {
                const TexturePack& pack = *source.packs[p];
                complete = complete && (!check_pairing || pack.tables_parsed);
                for (const TextureRecord& record : pack.textures) {
                    std::array<std::uint32_t, 3> hashes{};
                    const std::size_t count = claimed_hashes(record, hashes);
                    source_claims.insert(source_claims.end(), hashes.begin(),
                                         hashes.begin() + count);
                    bool shadowed = false;
                    for (std::size_t k = 0; k < count && !shadowed; ++k) {
                        const auto found = claims.find(hashes[k]);
                        if (found != claims.end()) {
                            result.shadowed.push_back(ShadowedTexture{
                                hashes[k], s, p, record.index, found->second});
                            shadowed = true;
                        }
                    }
                    bool wanted = true;
                    if (source.wanted != nullptr) {
                        wanted = false;
                        for (std::size_t k = 0; k < count && !wanted; ++k) {
                            wanted = std::binary_search(source.wanted->begin(),
                                                        source.wanted->end(), hashes[k]);
                        }
                    }
                    if (shadowed) {
                        // Claims no earlier source holds still block later
                        // sources, so keep them resolving as unusable.
                        for (std::size_t k = 0; k < count && wanted; ++k) {
                            if (claims.find(hashes[k]) == claims.end()) {
                                result.unusable_claims.push_back(
                                    UnusableClaim{hashes[k], s, p, record.index});
                            }
                        }
                        continue;
                    }
                    if (!wanted) {
                        ++result.not_wanted;
                        continue;
                    }
                    result.textures.emplace_back();
                    RenderTexture& texture = result.textures.back();
                    texture.hash = record.hash;
                    texture.claims = hashes;
                    texture.claim_count = count;
                    texture.source = s;
                    texture.pack = p;
                    texture.record = record.index;
                    // Info +81, +84, +85 and +86 are read before the
                    // record is checked, so they are carried even for a
                    // record that is rejected below.
                    texture.alpha_test_mode = record.alpha_test_mode;
                    texture.alpha_blend_mode = record.alpha_blend_mode;
                    texture.layer_order = record.layer_order;
                    texture.layer_group = record.layer_group;
                    const auto signed_word=[&](unsigned at) {
                        const unsigned value=unsigned(record.info[at])|(unsigned(record.info[at+1])<<8);
                        return static_cast<std::int16_t>(value<32768?int(value):int(value)-65536);
                    };
                    const unsigned mode=record.info[0x52];
                    texture.uv_animation.mode=static_cast<std::int8_t>(mode<128?int(mode):int(mode)-256);
                    texture.uv_animation.period=signed_word(0x58);
                    texture.uv_animation.u_rate=signed_word(0x5a);
                    texture.uv_animation.v_rate=signed_word(0x5c);
                    texture.uv_animation.u_offset=signed_word(0x5e);
                    texture.uv_animation.v_offset=signed_word(0x60);
                    if (!record.valid) {
                        texture.problem = record.problem.empty()
                                              ? "invalid texture record"
                                              : record.problem;
                        continue;
                    }
                    const bool compressed = limits.gpu_blocks &&
                        (record.format == PixelFormat::dxt1 || record.format == PixelFormat::dxt3);
                    std::uint64_t retained = std::uint64_t{record.width} * record.height * 4U;
                    if (compressed && !level_size(record.format, record.width, record.height, retained)) {
                        texture.problem = "invalid compressed texture size";
                        continue;
                    }
                    const auto levels=limits.retain_mips?record.mip_count:1U;
                    if(levels>1 && !mip_chain_size(compressed?record.format:PixelFormat::a8r8g8b8,
                            record.width,record.height,levels,retained)) {
                        texture.problem="invalid retained mip chain";continue;
                    }
                    if (retained > limits.max_total_bytes ||
                        result.decoded_bytes > limits.max_total_bytes - retained) {
                        error = "decoded textures would exceed " +
                                std::to_string(limits.max_total_bytes) +
                                " bytes at texture " + std::to_string(record.index) +
                                (label ? " of source " + std::to_string(s) +
                                             " pack " + std::to_string(p)
                                       : std::string()) +
                                " (" + std::to_string(result.decoded_bytes) +
                                " retained, " + std::to_string(retained) + " more)";
                        return false;
                    }
                    if (limits.before_allocate) {
                        limits.before_allocate(retained);
                    }
                    const std::vector<unsigned char>& bytes = *source.bytes;
                    std::vector<unsigned char> payload;
                    std::size_t unused = 0;
                    std::uint64_t top = 0;
                    std::string problem;
                    P8Palette palette{};
                    P8Palette* selected_palette = record.format == PixelFormat::p8 ? &palette : nullptr;
                    if (!texture_payload(bytes.data(), bytes.size(), record, payload,
                                         unused, problem, selected_palette) ||
                        !level_size(record.format, record.width, record.height, top) ||
                        top > payload.size() || (compressed && retained>payload.size()) ||
                        (!compressed && levels==1 && (!decode_level(record.format, record.width, record.height,
                                      payload.data(), static_cast<std::size_t>(top),
                                      texture.rgba, problem, selected_palette) ||
                        texture.rgba.size() != retained))) {
                        texture.rgba.clear();
                        texture.rgba.shrink_to_fit();
                        texture.problem =
                            problem.empty() ? "texture decode failed" : problem;
                        continue;
                    }
                    if (compressed) {
                        texture.storage = record.format == PixelFormat::dxt1 ?
                            RenderTextureStorage::dxt1 : RenderTextureStorage::dxt3;
                        texture.blocks.assign(payload.begin(), payload.begin() + static_cast<std::size_t>(retained));
                    } else if(levels>1) {
                        std::size_t offset=0;
                        auto width=record.width,height=record.height;
                        texture.rgba.reserve(static_cast<std::size_t>(retained));
                        for(unsigned level=0;level<levels;++level) {
                            std::uint64_t bytes=0;std::vector<unsigned char> pixels;
                            if(!level_size(record.format,width,height,bytes) || offset>payload.size() ||
                               bytes>payload.size()-offset ||
                               !decode_level(record.format,width,height,payload.data()+offset,
                                    static_cast<std::size_t>(bytes),pixels,problem,selected_palette))break;
                            texture.rgba.insert(texture.rgba.end(),pixels.begin(),pixels.end());
                            offset+=static_cast<std::size_t>(bytes);
                            width=std::max<unsigned>(1,width/2);height=std::max<unsigned>(1,height/2);
                        }
                        if(texture.rgba.size()!=retained) {
                            texture.rgba.clear();texture.problem="authored mip decode failed: "+problem;continue;
                        }
                    }
                    texture.mip_count=levels;
                    texture.width = record.width;
                    texture.height = record.height;
                    texture.valid = true;
                    result.decoded_bytes += retained;
                }
            }
            for (const std::uint32_t hash : source_claims) {
                claims.emplace(hash, s);
            }
            blocked = !complete;
            if (limits.after_source) limits.after_source(s);
        }
    } catch (const std::bad_alloc&) {
        error = "out of memory while decoding textures (" +
                std::to_string(result.decoded_bytes) + " bytes retained)";
        return false;
    }
    set = std::move(result);
    return true;
}

} // namespace

bool decode_render_textures(
    const std::vector<unsigned char>& bytes,
    const TexturePack& pack,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error) {
    set = RenderTextureSet{};
    std::vector<RenderTextureSource> sources;
    try {
        sources.resize(1);
        sources[0].bytes = &bytes;
        sources[0].packs.push_back(&pack);
    } catch (const std::bad_alloc&) {
        error = "out of memory while decoding textures (0 bytes retained)";
        return false;
    }
    return decode_sources(sources, false, limits, set, error);
}

bool decode_render_texture_sources(
    const std::vector<RenderTextureSource>& sources,
    const RenderTextureLimits& limits,
    RenderTextureSet& set,
    std::string& error) {
    return decode_sources(sources, true, limits, set, error);
}

} // namespace nfsu
