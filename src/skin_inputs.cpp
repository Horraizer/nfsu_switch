#include "nfsu/skin_inputs.h"

#include <algorithm>
#include <new>
#include <utility>

namespace nfsu {

namespace {

// A built source name is bounded so that no selection can make this layer
// hash an unbounded string. Retail names are far shorter; the longest
// observed is 28 characters ("240SX_DOORLINE_WIDEBODY_MASK").
constexpr std::size_t kMaxSourceNameBytes = 240;

// The gatherer's array holds 8 records and this layer fills at most 6 of
// them, but the bound is checked rather than assumed.
constexpr std::size_t kMaxSelectedSources = 6;

// Enough for a car TEXTURES.BIN, a car VINYLS.BIN and any shared pack a
// caller adds, without letting a source list grow without bound.
constexpr std::size_t kMaxInputSources = 32;
constexpr std::size_t kMaxPacksPerSource = 256;

// composite_body_skin_8 merges layer palettes above the five entries that
// hold the base and the four special colors (0x005A90CE..0x005A90F8).
constexpr std::uint32_t kSkinReservedPaletteEntries = 5;

std::uint32_t read_u32_le(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

// One selected source index, normalised out of the three selection structs
// so that resolution and assembly do not repeat the role dispatch.
struct SelectedSource {
    SkinSourceRole role = SkinSourceRole::hood_spoiler_vinyl;
    std::string source_name;
    std::optional<std::uint32_t> palette_size;
    bool remap = false;
    std::array<std::optional<std::uint32_t>, 4> remap_colors{};
    // Only the four vinyl layers have VINYL_COLOUR slots to read, so only
    // they get the 0x005AA1A7 substitution. The other two records keep the
    // zeroed colors 0x005A9ED3 left.
    bool has_remap_colors = false;
};

bool check_texture_name(
    const std::string& car_name,
    const std::string& texture,
    const std::string& what,
    std::string& error) {
    if (texture.empty()) {
        error = what + " has an empty TEXTURE value";
        return false;
    }
    if (car_name.size() + 1 + texture.size() + sizeof(kSkinMaskSuffix) > kMaxSourceNameBytes) {
        error = what + " names a string longer than the supported bound";
        return false;
    }
    return true;
}

bool check_palette_size(
    const std::optional<std::uint32_t>& palette_size,
    const std::string& what,
    std::string& error) {
    if (palette_size && *palette_size > 256) {
        error = what + " declares more than 256 palette entries";
        return false;
    }
    return true;
}

// Turns the selection into the gatherer's compact source list, in its order.
bool gather_selection(
    const BodySkinSelection& selection,
    std::vector<SelectedSource>& out,
    std::string& error) {
    out.clear();
    if (selection.car_name.empty()) {
        error = "the selection has no car name";
        return false;
    }
    if (selection.car_name.size() > kMaxSourceNameBytes) {
        error = "the car name is longer than the supported bound";
        return false;
    }

    if (selection.hood_spoiler_vinyl) {
        const SkinHoodSpoilerSelection& vinyl = *selection.hood_spoiler_vinyl;
        if (skin_hood_spoiler_suffix(vinyl.variant) == nullptr) {
            error = "the hood or spoiler vinyl names an unknown variant";
            return false;
        }
        SelectedSource source;
        source.role = SkinSourceRole::hood_spoiler_vinyl;
        source.source_name = skin_hood_spoiler_source_name(selection.car_name, vinyl.variant);
        if (source.source_name.size() + sizeof(kSkinMaskSuffix) > kMaxSourceNameBytes) {
            error = "the hood or spoiler vinyl names a string longer than the supported bound";
            return false;
        }
        // 0x005A9FD4 stores the fixed count; there is nothing to select.
        out.push_back(std::move(source));
    }

    for (std::size_t k = 0; k < selection.vinyl_layers.size(); ++k) {
        if (!selection.vinyl_layers[k]) {
            continue;
        }
        const SkinVinylLayerSelection& layer = *selection.vinyl_layers[k];
        const std::string what = "vinyl layer " + std::to_string(k);
        if (!check_texture_name(selection.car_name, layer.texture, what, error)) {
            return false;
        }
        if (!check_palette_size(layer.palette_size, what, error)) {
            return false;
        }
        // 0x005A9F56 reads NUMCOLOURS and 0x005A9F98 leaves through the
        // epilogue at 0x005A9F9A..0x005A9FA4 when it is absent or zero,
        // which abandons the whole composite rather than one record. The
        // attribute getter cannot tell absent from zero, so neither can
        // this check.
        if (!layer.palette_size || *layer.palette_size == 0) {
            error = what + " has no non-zero NUMCOLOURS, which aborts the composite";
            return false;
        }
        SelectedSource source;
        source.role = static_cast<SkinSourceRole>(static_cast<std::uint32_t>(k) + 1U);
        source.source_name = skin_vinyl_source_name(selection.car_name, layer.texture);
        source.palette_size = layer.palette_size;
        source.remap = layer.remap;
        source.remap_colors = layer.remap_colors;
        source.has_remap_colors = true;
        out.push_back(std::move(source));
    }

    if (selection.doorline) {
        const SkinDoorlineSelection& doorline = *selection.doorline;
        if (skin_doorline_suffix(doorline.variant) == nullptr) {
            error = "the doorline selection names an unknown variant";
            return false;
        }
        SelectedSource source;
        source.role = SkinSourceRole::doorline;
        source.source_name = skin_doorline_source_name(selection.car_name, doorline.variant);
        if (source.source_name.size() + sizeof(kSkinMaskSuffix) > kMaxSourceNameBytes) {
            error = "the doorline names a string longer than the supported bound";
            return false;
        }
        // As for the hood/spoiler source, 0x005A9FD4 fixes the count.
        out.push_back(std::move(source));
    }

    if (out.size() > kMaxSelectedSources) {
        error = "more selected sources than the gatherer fills";
        return false;
    }
    return true;
}

bool check_sources(const std::vector<SkinInputSource>& sources, std::string& error) {
    if (sources.empty()) {
        error = "no texture source was given";
        return false;
    }
    if (sources.size() > kMaxInputSources) {
        error = "more texture sources than the supported bound";
        return false;
    }
    for (std::size_t s = 0; s < sources.size(); ++s) {
        const SkinInputSource& source = sources[s];
        const std::string label = "texture source " + std::to_string(s);
        if (source.bytes == nullptr) {
            error = label + " has no bytes";
            return false;
        }
        if (source.packs.empty()) {
            error = label + " has no pack";
            return false;
        }
        if (source.packs.size() > kMaxPacksPerSource) {
            error = label + " has more packs than the supported bound";
            return false;
        }
        for (std::size_t p = 0; p < source.packs.size(); ++p) {
            const TexturePack* pack = source.packs[p];
            const std::string where = label + " pack " + std::to_string(p);
            if (pack == nullptr) {
                error = where + " is null";
                return false;
            }
            if (pack->parse_source != source.bytes->data() ||
                pack->file_size != source.bytes->size()) {
                error = where + " was not parsed from the bytes of that source";
                return false;
            }
            if (!pack->tables_parsed) {
                error = where + " has no parsed record tables, so its hashes are unknown";
                return false;
            }
        }
    }
    return true;
}

std::uint32_t blob_table_hash(const TextureRecord& record) noexcept {
    if (record.storage != TextureStorage::jdlz) {
        return 0;
    }
    return read_u32_le(record.blob_entry.data());
}

void fill_provenance(
    const SkinInputSource& source,
    std::size_t source_index,
    std::size_t pack_index,
    const TextureRecord& record,
    std::uint32_t wanted,
    SkinTextureProvenance& out) {
    const TexturePack& pack = *source.packs[pack_index];
    std::vector<SkinHashClaim> claims = std::move(out.also_claimed_by);
    out = SkinTextureProvenance{};
    out.also_claimed_by = std::move(claims);
    out.resolved = true;
    out.source_index = source_index;
    out.pack_index = pack_index;
    out.source_kind = source.kind;
    out.source_path = source.path;
    out.pack_root_offset = pack.root_offset;
    out.pack_name = pack.pack_name;
    out.pack_hash = pack.pack_hash;
    out.record_index = record.index;
    out.record_hash = record.hash;
    out.table_hash = record.table_hash;
    out.blob_hash = blob_table_hash(record);
    out.stored_name = record.name;
    out.stored_name_full = record.name_field_full;
    out.stored_name_matches = bin_hash(record.name) == wanted;
    out.record_valid = record.valid;
    out.record_problem = record.problem;
    out.storage = record.storage;
    out.format = record.format;
    out.format_code = record.format_code;
    out.d3d_format = record.d3d_format;
    out.width = record.width;
    out.height = record.height;
    out.mip_count = record.mip_count;
    out.data_size = record.data_size;
    out.record_palette_bytes = record.palette_size;
    out.top_level_size = record.top_level_size;
    out.info_offset = record.info_offset;
    out.blob_offset = record.blob_offset;
    out.blob_size = record.blob_size;
}

// The first record that claims the hash wins, in source, pack and record
// order, even when that record is unusable, so a broken car texture is
// never replaced by a later one. Every other claimant is recorded.
bool resolve_hash(
    const std::vector<SkinInputSource>& sources,
    std::uint32_t hash,
    SkinTextureProvenance& out,
    const TextureRecord*& record_out) {
    out = SkinTextureProvenance{};
    record_out = nullptr;
    for (std::size_t s = 0; s < sources.size(); ++s) {
        const SkinInputSource& source = sources[s];
        for (std::size_t p = 0; p < source.packs.size(); ++p) {
            const TexturePack& pack = *source.packs[p];
            for (const TextureRecord& record : pack.textures) {
                if (!record_claims_hash(record, hash)) {
                    continue;
                }
                if (!out.resolved) {
                    fill_provenance(source, s, p, record, hash, out);
                    record_out = &record;
                } else {
                    out.also_claimed_by.push_back(SkinHashClaim{s, p, record.index});
                }
            }
        }
    }
    return out.resolved;
}

// What the target type accepts, and whether a P8 record must be expanded.
// See the header for the instruction evidence behind each case.
bool conversion_for(SkinTargetType target, PixelFormat format, bool& expand) {
    expand = false;
    if (target == SkinTargetType::type32) {
        if (format == PixelFormat::a8r8g8b8) {
            return true;
        }
        if (format == PixelFormat::p8) {
            expand = true;
            return true;
        }
        return false;
    }
    return format == PixelFormat::p8;
}

const char* conversion_detail(SkinTargetType target) noexcept {
    return target == SkinTargetType::type32
               ? "a type-32 target takes only A8R8G8B8 or P8 records"
               : "a type-8 target takes only P8 records";
}

struct Budget {
    std::uint64_t used = 0;
    std::uint64_t limit = 0;

    bool take(std::uint64_t bytes, std::string& error) {
        if (bytes > limit - used) {
            error = "the assembled inputs exceed the total byte budget";
            return false;
        }
        used += bytes;
        return true;
    }
};

// Reads one record's level 0 and, for P8, its palette. Never reads a level
// above 0: neither composite routine downsamples, and both the gatherer and
// the composites lock level 0 only (0x005AA045, 0x005AA07C, 0x005A8A95,
// 0x005A8EDC).
bool read_level0(
    const SkinInputSource& source,
    const TextureRecord& record,
    std::vector<unsigned char>& level0,
    SkinPalette& palette,
    bool& has_palette,
    std::string& error) {
    has_palette = false;
    std::uint64_t wanted = 0;
    if (!level_size(record.format, record.width, record.height, wanted)) {
        error = "the record has no computable level size";
        return false;
    }
    std::vector<unsigned char> payload;
    std::size_t unused_input = 0;
    P8Palette stored{};
    const bool p8 = record.format == PixelFormat::p8;
    if (!texture_payload(source.bytes->data(), source.bytes->size(), record, payload,
                         unused_input, error, p8 ? &stored : nullptr)) {
        return false;
    }
    if (payload.size() < wanted) {
        error = "the record payload is shorter than its level 0";
        return false;
    }
    level0.assign(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(wanted));
    if (p8) {
        palette = skin_palette_from_p8(stored);
        has_palette = true;
    }
    return true;
}

std::uint32_t highest_index_used(const std::vector<unsigned char>& indices) noexcept {
    if (indices.empty()) {
        return 0;
    }
    unsigned char top = 0;
    for (const unsigned char value : indices) {
        if (value > top) {
            top = value;
        }
    }
    return static_cast<std::uint32_t>(top) + 1U;
}

// Copies level 0 into the owned buffers of one layer, converting only where
// the retail path proves it.
bool store_surface(
    SkinTargetType target,
    const std::vector<unsigned char>& level0,
    const SkinPalette& palette,
    bool expand,
    std::size_t pixels,
    Budget& budget,
    std::vector<std::uint32_t>& out32,
    std::vector<std::uint8_t>& out8,
    SkinPalette& out_palette,
    std::uint64_t& taken,
    std::string& error) {
    // SkinLayerStorage carries a palette per surface inline whether or not
    // the target type uses it, so the budget charges it either way and
    // owned_bytes() reports what was really allocated.
    if (target == SkinTargetType::type32) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(pixels) * 4ULL +
                                    static_cast<std::uint64_t>(kP8PaletteSize);
        if (!budget.take(bytes, error)) {
            return false;
        }
        taken = bytes;
        out32.resize(pixels);
        if (expand) {
            // 0x00407E70..0x00407E7B copies the palette entry into the
            // pixel whole, so no lane is rearranged.
            for (std::size_t i = 0; i < pixels; ++i) {
                out32[i] = palette[level0[i]];
            }
        } else {
            for (std::size_t i = 0; i < pixels; ++i) {
                out32[i] = read_u32_le(level0.data() + i * 4U);
            }
        }
        return true;
    }
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(pixels) + static_cast<std::uint64_t>(kP8PaletteSize);
    if (!budget.take(bytes, error)) {
        return false;
    }
    taken = bytes;
    out8.assign(level0.begin(), level0.begin() + static_cast<std::ptrdiff_t>(pixels));
    out_palette = palette;
    return true;
}

// The records one selected source resolved to, kept so that assembly never
// has to look a record up again.
struct ResolvedRecords {
    const TextureRecord* source = nullptr;
    const TextureRecord* mask = nullptr;
    bool expand_source = false;
    bool expand_mask = false;
};

void set_skip(
    SkinInputLayerInfo& info,
    SkinSkipReason reason,
    const std::string& detail) {
    info.usable = false;
    info.skip_reason = reason;
    info.skip_detail = detail;
}

// Resolves every selected source and mask and records what was found.
void resolve_layers(
    const BodySkinSelection& selection,
    const std::vector<SkinInputSource>& sources,
    const std::vector<SelectedSource>& selected,
    const SkinInputLimits& limits,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<SkinInputLayerInfo>& inputs,
    std::vector<ResolvedRecords>& records) {
    inputs.clear();
    inputs.reserve(selected.size());
    records.clear();
    records.reserve(selected.size());
    std::size_t next_layer_index = 0;
    for (const SelectedSource& source : selected) {
        SkinInputLayerInfo info;
        info.role = source.role;
        info.source_name = source.source_name;
        // The gatherer does not build this string: 0x005AA009..0x005AA024
        // continues the base hash with "_MASK" instead. Both give the same
        // value, because the hash is h = h * 33 + byte, and the retail
        // build itself relies on that at 0x005A9C10 and 0x005A97F0.
        info.mask_name = skin_mask_name(source.source_name);
        info.source_hash = bin_hash(info.source_name);
        info.mask_hash = bin_hash(info.mask_name);
        info.palette_entries = source.palette_size.value_or(kSkinFixedPaletteSize);
        info.palette_entries_defaulted = !source.palette_size.has_value();
        info.remap = source.remap;
        for (std::size_t k = 0; k < info.remap_colors.size(); ++k) {
            // 0x005AA10F leaves for 0x005AA1CC when the REMAP value is
            // zero, which is before the colour loop at 0x005AA134 and
            // before the 0x005AA1A7 substitution, so a layer that does not
            // remap keeps the four zeroed colours 0x005A9ED3 left. It is
            // still counted: 0x005AA1CC falls into the record cursor.
            if (!source.has_remap_colors || !source.remap) {
                continue;
            }
            if (source.remap_colors[k]) {
                info.remap_colors[k] = *source.remap_colors[k];
            } else {
                info.remap_colors[k] = skin_default_remap_color(k);
                info.remap_color_defaulted[k] = true;
            }
        }

        const TextureRecord* source_record = nullptr;
        const TextureRecord* mask_record = nullptr;
        const bool found_source =
            resolve_hash(sources, info.source_hash, info.source, source_record);
        const bool found_mask = resolve_hash(sources, info.mask_hash, info.mask, mask_record);

        bool expand_source = false;
        bool expand_mask = false;
        if (!found_source) {
            set_skip(info, SkinSkipReason::source_not_found, "no source claims the source hash");
        } else if (!found_mask) {
            set_skip(info, SkinSkipReason::mask_not_found, "no source claims the mask hash");
        } else if (!source_record->valid) {
            set_skip(info, SkinSkipReason::source_record_unusable,
                     "the source record is unusable: " + source_record->problem);
        } else if (!mask_record->valid) {
            set_skip(info, SkinSkipReason::mask_record_unusable,
                     "the mask record is unusable: " + mask_record->problem);
        } else if (source_record->width != width || source_record->height != height) {
            set_skip(info, SkinSkipReason::source_dimension_mismatch,
                     "the source does not have the target dimensions");
        } else if (mask_record->width != width || mask_record->height != height) {
            set_skip(info, SkinSkipReason::mask_dimension_mismatch,
                     "the mask does not have the target dimensions");
        } else if (!conversion_for(selection.target, source_record->format, expand_source)) {
            set_skip(info, SkinSkipReason::source_format_unproven,
                     std::string("the source format has no proven conversion: ") +
                         conversion_detail(selection.target));
        } else if (!conversion_for(selection.target, mask_record->format, expand_mask)) {
            set_skip(info, SkinSkipReason::mask_format_unproven,
                     std::string("the mask format has no proven conversion: ") +
                         conversion_detail(selection.target));
        } else if (source_record->data_size > limits.max_payload_bytes) {
            set_skip(info, SkinSkipReason::source_payload_too_large,
                     "the source payload exceeds the supported bound");
        } else if (mask_record->data_size > limits.max_payload_bytes) {
            set_skip(info, SkinSkipReason::mask_payload_too_large,
                     "the mask payload exceeds the supported bound");
        } else {
            info.usable = true;
            info.layer_index = next_layer_index++;
        }

        ResolvedRecords resolved;
        if (info.usable) {
            resolved.source = source_record;
            resolved.mask = mask_record;
            resolved.expand_source = expand_source;
            resolved.expand_mask = expand_mask;
        }
        records.push_back(resolved);
        inputs.push_back(std::move(info));
    }
}

void fill_paints(
    const BodySkinSelection& selection,
    std::uint32_t& base_color,
    std::array<std::uint32_t, 4>& special_colors,
    std::array<bool, 4>& defaulted) {
    base_color = selection.base_color;
    for (std::size_t k = 0; k < special_colors.size(); ++k) {
        if (selection.special_colors[k]) {
            special_colors[k] = *selection.special_colors[k];
            defaulted[k] = false;
        } else {
            // 0x005A9DE0 falls back to the base color for an empty slot.
            special_colors[k] = selection.base_color;
            defaulted[k] = true;
        }
    }
}

// Resolves the optional target texture and settles the target dimensions.
bool resolve_target(
    const BodySkinSelection& selection,
    const std::vector<SkinInputSource>& sources,
    SkinTextureProvenance& target,
    std::uint32_t& width,
    std::uint32_t& height,
    std::string& error) {
    width = selection.width;
    height = selection.height;
    target = SkinTextureProvenance{};
    if (selection.target_hash) {
        const TextureRecord* record = nullptr;
        if (resolve_hash(sources, *selection.target_hash, target, record) && record->valid) {
            if (width == 0 && height == 0) {
                width = record->width;
                height = record->height;
            } else if (width != record->width || height != record->height) {
                error = "the selected dimensions do not match the target texture";
                return false;
            }
        }
    }
    if (width == 0 || height == 0) {
        error = "the target has a zero dimension";
        return false;
    }
    return true;
}

bool check_pixel_bound(
    SkinTargetType target,
    std::uint32_t width,
    std::uint32_t height,
    const SkinInputLimits& limits,
    std::size_t& pixels,
    std::string& error) {
    const std::uint64_t total =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    const std::uint64_t own = target == SkinTargetType::type8
                                  ? static_cast<std::uint64_t>(kSkinMaxType8Pixels)
                                  : static_cast<std::uint64_t>(kSkinMaxType32Pixels);
    const std::uint64_t bound = std::min<std::uint64_t>(limits.max_pixels, own);
    if (total > bound) {
        error = "the target pixel count exceeds the supported bound";
        return false;
    }
    pixels = static_cast<std::size_t>(total);
    return true;
}

void summarise(BodySkinInputPlan& plan) {
    plan.usable_inputs = 0;
    plan.skipped_inputs = 0;
    plan.merged_palette_entries = 0;
    plan.merged_palette_fits = true;
    const bool type8 = plan.target == SkinTargetType::type8;
    std::uint64_t merged = kSkinReservedPaletteEntries;
    for (const SkinInputLayerInfo& input : plan.inputs) {
        if (!input.usable) {
            ++plan.skipped_inputs;
            continue;
        }
        ++plan.usable_inputs;
        merged += input.palette_entries;
    }
    if (type8) {
        plan.merged_palette_entries = merged;
        plan.merged_palette_fits = merged <= 256;
    }
}

bool build_plan(
    const BodySkinSelection& selection,
    const std::vector<SkinInputSource>& sources,
    const SkinInputLimits& limits,
    BodySkinInputPlan& plan,
    std::vector<ResolvedRecords>& records,
    std::size_t& pixels,
    std::string& error) {
    plan = BodySkinInputPlan{};
    records.clear();
    std::vector<SelectedSource> selected;
    if (!gather_selection(selection, selected, error)) {
        return false;
    }
    if (!check_sources(sources, error)) {
        return false;
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    SkinTextureProvenance target;
    if (!resolve_target(selection, sources, target, width, height, error)) {
        return false;
    }
    if (!check_pixel_bound(selection.target, width, height, limits, pixels, error)) {
        return false;
    }

    plan.target = selection.target;
    plan.width = width;
    plan.height = height;
    plan.car_name = selection.car_name;
    plan.target_hash = selection.target_hash;
    plan.target_texture = std::move(target);
    fill_paints(selection, plan.base_color, plan.special_colors, plan.special_color_defaulted);
    resolve_layers(selection, sources, selected, limits, width, height, plan.inputs, records);
    summarise(plan);
    return true;
}

} // namespace

const char kSkinMaskSuffix[6] = "_MASK";

const char* skin_target_type_name(SkinTargetType type) noexcept {
    switch (type) {
        case SkinTargetType::type32:
            return "type32";
        case SkinTargetType::type8:
            return "type8";
    }
    return "unknown";
}

const char* skin_source_role_name(SkinSourceRole role) noexcept {
    switch (role) {
        case SkinSourceRole::hood_spoiler_vinyl:
            return "hood_spoiler_vinyl";
        case SkinSourceRole::vinyl_layer0:
            return "vinyl_layer0";
        case SkinSourceRole::vinyl_layer1:
            return "vinyl_layer1";
        case SkinSourceRole::vinyl_layer2:
            return "vinyl_layer2";
        case SkinSourceRole::vinyl_layer3:
            return "vinyl_layer3";
        case SkinSourceRole::doorline:
            return "doorline";
    }
    return "unknown";
}

const char* skin_doorline_variant_name(SkinDoorlineVariant variant) noexcept {
    switch (variant) {
        case SkinDoorlineVariant::plain:
            return "plain";
        case SkinDoorlineVariant::body_kit:
            return "body_kit";
        case SkinDoorlineVariant::wide_body:
            return "wide_body";
    }
    return "unknown";
}

const char* skin_doorline_suffix(SkinDoorlineVariant variant) noexcept {
    switch (variant) {
        case SkinDoorlineVariant::plain:
            return "_DOORLINE";
        case SkinDoorlineVariant::body_kit:
            return "_DOORLINE_KIT";
        case SkinDoorlineVariant::wide_body:
            return "_DOORLINE_WIDEBODY";
    }
    return nullptr;
}

const char* skin_hood_spoiler_variant_name(SkinHoodSpoilerVariant variant) noexcept {
    switch (variant) {
        case SkinHoodSpoilerVariant::hood_and_spoiler:
            return "hood_and_spoiler";
        case SkinHoodSpoilerVariant::hood:
            return "hood";
        case SkinHoodSpoilerVariant::spoiler:
            return "spoiler";
    }
    return "unknown";
}

const char* skin_hood_spoiler_suffix(SkinHoodSpoilerVariant variant) noexcept {
    switch (variant) {
        case SkinHoodSpoilerVariant::hood_and_spoiler:
            return "_HOOD_SPOILER"; // 0x006B7E80
        case SkinHoodSpoilerVariant::hood:
            return "_HOOD";         // 0x006B7E78
        case SkinHoodSpoilerVariant::spoiler:
            return "_SPOILER";      // 0x006B7E6C
    }
    return nullptr;
}

const char* skin_skip_reason_name(SkinSkipReason reason) noexcept {
    switch (reason) {
        case SkinSkipReason::none:
            return "none";
        case SkinSkipReason::source_not_found:
            return "source_not_found";
        case SkinSkipReason::mask_not_found:
            return "mask_not_found";
        case SkinSkipReason::source_record_unusable:
            return "source_record_unusable";
        case SkinSkipReason::mask_record_unusable:
            return "mask_record_unusable";
        case SkinSkipReason::source_dimension_mismatch:
            return "source_dimension_mismatch";
        case SkinSkipReason::mask_dimension_mismatch:
            return "mask_dimension_mismatch";
        case SkinSkipReason::source_format_unproven:
            return "source_format_unproven";
        case SkinSkipReason::mask_format_unproven:
            return "mask_format_unproven";
        case SkinSkipReason::source_payload_too_large:
            return "source_payload_too_large";
        case SkinSkipReason::mask_payload_too_large:
            return "mask_payload_too_large";
    }
    return "unknown";
}

const char* skin_source_kind_name(SkinSourceKind kind) noexcept {
    switch (kind) {
        case SkinSourceKind::car_textures:
            return "car_textures";
        case SkinSourceKind::car_vinyls:
            return "car_vinyls";
        case SkinSourceKind::other:
            return "other";
    }
    return "unknown";
}

std::string skin_vinyl_source_name(const std::string& car_name, const std::string& texture) {
    return car_name + "_" + texture;
}

std::string skin_doorline_source_name(const std::string& car_name, SkinDoorlineVariant variant) {
    const char* suffix = skin_doorline_suffix(variant);
    return suffix == nullptr ? car_name : car_name + suffix;
}

std::string skin_hood_spoiler_source_name(
    const std::string& car_name,
    SkinHoodSpoilerVariant variant) {
    const char* suffix = skin_hood_spoiler_suffix(variant);
    return suffix == nullptr ? car_name : car_name + suffix;
}

std::string skin_mask_name(const std::string& source_name) {
    return source_name + kSkinMaskSuffix;
}

SkinPalette skin_palette_from_p8(const P8Palette& palette) noexcept {
    SkinPalette out{};
    for (std::size_t entry = 0; entry < out.size(); ++entry) {
        out[entry] = read_u32_le(palette.data() + entry * 4U);
    }
    return out;
}

AssembledSkinInputs::AssembledSkinInputs(AssembledSkinInputs&& other) noexcept
    : request_(std::move(other.request_)),
      plan_(std::move(other.plan_)),
      storage_(std::move(other.storage_)),
      input_of_layer_(std::move(other.input_of_layer_)),
      owned_bytes_(other.owned_bytes_) {
    other.clear();
}

AssembledSkinInputs& AssembledSkinInputs::operator=(AssembledSkinInputs&& other) noexcept {
    if (this != &other) {
        request_ = std::move(other.request_);
        plan_ = std::move(other.plan_);
        storage_ = std::move(other.storage_);
        input_of_layer_ = std::move(other.input_of_layer_);
        owned_bytes_ = other.owned_bytes_;
        other.clear();
    }
    return *this;
}

void AssembledSkinInputs::clear() noexcept {
    // Only operations that cannot throw, so the move operations stay
    // noexcept and a moved-from object is empty rather than half-valid.
    request_.layers.clear();
    request_.width = 0;
    request_.height = 0;
    request_.base_color = 0;
    request_.special_colors = {};
    plan_.inputs.clear();
    plan_.car_name.clear();
    // Emptying the containers and clearing `resolved` is enough, and is
    // the only form that cannot throw: every other member of a provenance
    // is a scalar that is meaningless while `resolved` is false.
    plan_.target_texture.resolved = false;
    plan_.target_texture.source_path.clear();
    plan_.target_texture.pack_name.clear();
    plan_.target_texture.stored_name.clear();
    plan_.target_texture.record_problem.clear();
    plan_.target_texture.also_claimed_by.clear();
    plan_.target_hash.reset();
    plan_.target = SkinTargetType::type32;
    plan_.width = 0;
    plan_.height = 0;
    plan_.usable_inputs = 0;
    plan_.skipped_inputs = 0;
    plan_.base_color = 0;
    plan_.special_colors = {};
    plan_.special_color_defaulted = {};
    plan_.merged_palette_entries = 0;
    plan_.merged_palette_fits = true;
    storage_.clear();
    input_of_layer_.clear();
    owned_bytes_ = 0;
}

std::size_t AssembledSkinInputs::input_of_layer(std::size_t layer) const noexcept {
    if (layer >= input_of_layer_.size()) {
        return plan_.inputs.size();
    }
    return input_of_layer_[layer];
}

const SkinLayerStorage* AssembledSkinInputs::layer_storage(std::size_t layer) const noexcept {
    if (layer >= storage_.size()) {
        return nullptr;
    }
    return storage_[layer].get();
}

bool validate_assembled_skin_inputs(const AssembledSkinInputs& assembled, std::string& error) {
    error.clear();
    const BodySkinRequest& request = assembled.request();
    if (request.layers.size() != assembled.layer_count()) {
        error = "the layer count does not match the request";
        return false;
    }
    if (request.layers.size() > kSkinMaxLayers) {
        error = "more layers than the 8 the game's array holds";
        return false;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(request.width) *
                                 static_cast<std::uint64_t>(request.height);
    const bool type32 = assembled.target() == SkinTargetType::type32;
    for (std::size_t i = 0; i < request.layers.size(); ++i) {
        const BodySkinLayer& layer = request.layers[i];
        const SkinLayerStorage* storage = assembled.layer_storage(i);
        const std::string where = "layer " + std::to_string(i);
        if (storage == nullptr) {
            error = where + " has no owned storage";
            return false;
        }
        if (!layer.present) {
            error = where + " is not present";
            return false;
        }
        if (assembled.input_of_layer(i) >= assembled.plan().inputs.size()) {
            error = where + " does not name a plan input";
            return false;
        }
        struct Bound {
            const void* data;
            std::size_t size;
            const void* owner;
            std::size_t owner_size;
            const char* what;
        };
        const Bound bounds[4] = {
            {layer.source32.data, layer.source32.size, storage->source32.data(),
             storage->source32.size(), "source32"},
            {layer.mask32.data, layer.mask32.size, storage->mask32.data(),
             storage->mask32.size(), "mask32"},
            {layer.source8.data, layer.source8.size, storage->source8.data(),
             storage->source8.size(), "source8"},
            {layer.mask8.data, layer.mask8.size, storage->mask8.data(), storage->mask8.size(),
             "mask8"},
        };
        for (std::size_t b = 0; b < 4; ++b) {
            const bool wanted = (b < 2) == type32;
            const Bound& bound = bounds[b];
            if (!wanted) {
                if (bound.data != nullptr || bound.size != 0) {
                    error = where + " sets " + bound.what + ", which this target type never uses";
                    return false;
                }
                continue;
            }
            if (bound.data == nullptr) {
                error = where + " has no " + bound.what;
                return false;
            }
            if (bound.data != bound.owner) {
                error = where + " " + bound.what + " does not point into its own storage";
                return false;
            }
            if (bound.size != bound.owner_size ||
                static_cast<std::uint64_t>(bound.size) < pixels) {
                error = where + " " + bound.what + " is shorter than the target";
                return false;
            }
        }
        const bool wants_palettes = !type32;
        const bool has_palettes =
            layer.source_palette != nullptr && layer.mask_palette != nullptr;
        if (wants_palettes != has_palettes) {
            error = where + " palette pointers do not match the target type";
            return false;
        }
        if (wants_palettes && (layer.source_palette != &storage->source_palette ||
                               layer.mask_palette != &storage->mask_palette)) {
            error = where + " palettes do not point into its own storage";
            return false;
        }
    }
    return true;
}

bool plan_body_skin_inputs(
    const BodySkinSelection& selection,
    const std::vector<SkinInputSource>& sources,
    const SkinInputLimits& limits,
    BodySkinInputPlan& plan,
    std::string& error) {
    error.clear();
    plan = BodySkinInputPlan{};
    std::vector<ResolvedRecords> records;
    std::size_t pixels = 0;
    if (!build_plan(selection, sources, limits, plan, records, pixels, error)) {
        plan = BodySkinInputPlan{};
        return false;
    }
    return true;
}

bool assemble_body_skin_inputs(
    const BodySkinSelection& selection,
    const std::vector<SkinInputSource>& sources,
    const SkinInputLimits& limits,
    AssembledSkinInputs& out,
    std::string& error) {
    error.clear();
    out.clear();
    AssembledSkinInputs result;
    std::vector<ResolvedRecords> records;
    std::size_t pixels = 0;
    if (!build_plan(selection, sources, limits, result.plan_, records, pixels, error)) {
        return false;
    }
    // Refuse a palette merge the compositor would reject, before a single
    // pixel is copied, and name the sum so the caller can act on it.
    if (result.plan_.target == SkinTargetType::type8 && !result.plan_.merged_palette_fits) {
        error = "the merged layer palettes need " +
                std::to_string(result.plan_.merged_palette_entries) +
                " entries, more than the 256 the composite holds";
        return false;
    }

    result.request_.width = result.plan_.width;
    result.request_.height = result.plan_.height;
    result.request_.base_color = result.plan_.base_color;
    result.request_.special_colors = result.plan_.special_colors;

    Budget budget;
    budget.limit = limits.max_total_bytes;
    try {
        for (std::size_t i = 0; i < result.plan_.inputs.size(); ++i) {
            SkinInputLayerInfo& info = result.plan_.inputs[i];
            if (!info.usable) {
                continue;
            }
            const SkinInputSource& source_owner = sources[info.source.source_index];
            const SkinInputSource& mask_owner = sources[info.mask.source_index];
            const TextureRecord& source_record = *records[i].source;
            const TextureRecord& mask_record = *records[i].mask;

            SkinAssembledLayerInfo assembly;
            assembly.source_expanded = records[i].expand_source;
            assembly.mask_expanded = records[i].expand_mask;

            std::vector<unsigned char> source_level;
            std::vector<unsigned char> mask_level;
            SkinPalette source_palette{};
            SkinPalette mask_palette{};
            bool source_has_palette = false;
            bool mask_has_palette = false;
            if (!read_level0(source_owner, source_record, source_level, source_palette,
                             source_has_palette, error)) {
                error = std::string(skin_source_role_name(info.role)) +
                        " source could not be read: " + error;
                return false;
            }
            if (!read_level0(mask_owner, mask_record, mask_level, mask_palette,
                             mask_has_palette, error)) {
                error = std::string(skin_source_role_name(info.role)) +
                        " mask could not be read: " + error;
                return false;
            }

            if (source_has_palette) {
                assembly.source_indices_used = highest_index_used(source_level);
                assembly.source_indices_exceed_palette =
                    assembly.source_indices_used > info.palette_entries;
            }
            if (mask_has_palette) {
                assembly.mask_indices_used = highest_index_used(mask_level);
            }

            auto storage = std::unique_ptr<SkinLayerStorage>(new SkinLayerStorage());
            std::uint64_t source_bytes = 0;
            std::uint64_t mask_bytes = 0;
            if (!store_surface(result.plan_.target, source_level, source_palette,
                               assembly.source_expanded, pixels, budget, storage->source32,
                               storage->source8, storage->source_palette, source_bytes, error)) {
                return false;
            }
            if (!store_surface(result.plan_.target, mask_level, mask_palette,
                               assembly.mask_expanded, pixels, budget, storage->mask32,
                               storage->mask8, storage->mask_palette, mask_bytes, error)) {
                return false;
            }
            assembly.owned_bytes = source_bytes + mask_bytes;

            BodySkinLayer layer;
            layer.present = true;
            // The gatherer writes record +0x1C for every source it fills
            // (0x005A9F56); only the palette pointers at +0x10 and +0x18
            // are conditional (0x005AA054, 0x005AA08B).
            layer.palette_size = info.palette_entries;
            layer.remap = info.remap;
            layer.remap_colors = info.remap_colors;
            if (result.plan_.target == SkinTargetType::type32) {
                layer.source32 = SkinView32{storage->source32.data(), storage->source32.size()};
                layer.mask32 = SkinView32{storage->mask32.data(), storage->mask32.size()};
            } else {
                layer.source8 = SkinView8{storage->source8.data(), storage->source8.size()};
                layer.mask8 = SkinView8{storage->mask8.data(), storage->mask8.size()};
                layer.source_palette = &storage->source_palette;
                layer.mask_palette = &storage->mask_palette;
            }

            if (info.layer_index != result.request_.layers.size()) {
                error = "the assembled layer order does not match the resolved order";
                return false;
            }
            info.assembly = assembly;
            result.request_.layers.push_back(layer);
            result.input_of_layer_.push_back(i);
            result.storage_.push_back(std::move(storage));
        }
    } catch (const std::bad_alloc&) {
        error = "the assembled inputs could not be allocated";
        return false;
    }

    if (result.request_.layers.size() > kSkinMaxLayers) {
        error = "more assembled layers than the 8 the game's array holds";
        return false;
    }
    result.owned_bytes_ = budget.used;
    if (!validate_assembled_skin_inputs(result, error)) {
        return false;
    }
    out = std::move(result);
    return true;
}

} // namespace nfsu
