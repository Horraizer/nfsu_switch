#pragma once

#include "nfsu/body_skin.h"
#include "nfsu/texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nfsu {

// Body-skin input assembly: turns an explicit customization selection and a
// list of parsed, read-only texture packs into a BodySkinRequest whose every
// buffer this layer owns.
//
// This layer models the input gatherer at speed.exe 0x005A9CE0 (SHA-256
// C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7, image
// base 0x00400000). The per-pixel work it feeds is in nfsu/body_skin.h; the
// pack parsing it reads is in nfsu/texture.h. See docs/BODY_SKIN.md for the
// pixel evidence and docs/CAR_SCENE.md for the selection evidence.
//
// It invents no customization state. Every vinyl, vinyl layer, doorline
// variant, paint color and remap color is supplied by the caller. Where the
// gatherer itself substitutes a value (NUMCOLOURS for the non-vinyl-layer
// sources at 0x005A9FD4, a missing remap color at 0x005AA1A7, a missing
// special paint slot at 0x005A9DE0), the substitution is optional, named
// after the instruction that performs it, and reported in the diagnostics.
//
// It deliberately does not model two things. It does not read a part
// database: the caller states which parts are installed. And it does not
// reproduce retail texture lookup, which is a stateful most-recently-used
// bucket list (0x005461C0); see SkinInputSource for what it does instead.

// ---------------------------------------------------------------------------
// Target
// ---------------------------------------------------------------------------

// Which composite path the assembled request is for. 0x005AA262 dispatches
// on the target texture object's byte +0x4A: 0x20 selects 0x005A8A50
// ("type 32"), 8 selects 0x005A8E90 ("type 8").
enum class SkinTargetType { type32, type8 };

const char* skin_target_type_name(SkinTargetType type) noexcept;

// ---------------------------------------------------------------------------
// Roles, variants and names
// ---------------------------------------------------------------------------

// The gatherer's eight source indexes (0x005A9F00 dispatches on the index;
// indexes 6 and 7 are never filled). The enumerator value is that index.
enum class SkinSourceRole : std::uint32_t {
    hood_spoiler_vinyl = 0,
    vinyl_layer0 = 1,
    vinyl_layer1 = 2,
    vinyl_layer2 = 3,
    vinyl_layer3 = 4,
    doorline = 5,
};

const char* skin_source_role_name(SkinSourceRole role) noexcept;

// The doorline helper at 0x005A96B0 picks one of three names from the
// installed wide-body and body-kit parts. The caller states which one it
// wants; this layer never inspects a part database.
//
// The helper's own rule, for a caller that has a part database: wide_body
// when slot 6 WIDE_BODY is filled at all (presence only, 0x005A96E4);
// otherwise body_kit when any of slots 1 FRONT_BUMPER, 2 REAR_BUMPER or 5
// BODY is filled with upgrade level above 0 (0x005A974F, 0x005A9753);
// otherwise plain. It always returns a name, so the doorline source can
// only disappear through a failed lookup.
enum class SkinDoorlineVariant { plain, body_kit, wide_body };

const char* skin_doorline_variant_name(SkinDoorlineVariant variant) noexcept;

// "_DOORLINE", "_DOORLINE_KIT" or "_DOORLINE_WIDEBODY", the suffixes the
// helper appends to the car name.
const char* skin_doorline_suffix(SkinDoorlineVariant variant) noexcept;

// The hood/spoiler helper at 0x005A9930 appends one of three literals to
// the car name and reads no part attribute at all: slot 35 VINYL_HOOD and
// slot 36 VINYL_SPOILER are tested for presence only (0x005A996D,
// 0x005A9971, 0x005A998B), with no upgrade-level condition. Both filled
// gives hood_and_spoiler, one gives that one, and neither leaves source
// index 0 unfilled, which a caller expresses by not selecting it.
//
// The name therefore does not depend on which hood or spoiler vinyl part
// is installed, only on whether each slot is filled.
enum class SkinHoodSpoilerVariant { hood_and_spoiler, hood, spoiler };

const char* skin_hood_spoiler_variant_name(SkinHoodSpoilerVariant variant) noexcept;

// "_HOOD_SPOILER", "_HOOD" or "_SPOILER".
const char* skin_hood_spoiler_suffix(SkinHoodSpoilerVariant variant) noexcept;

// Why a selected source did not become a layer. Machine-readable so that a
// caller can tell "the pack does not have it" from "no evidence converts
// it"; the matching text is in SkinInputLayerInfo::skip_detail.
enum class SkinSkipReason {
    none,
    source_not_found,
    mask_not_found,
    source_record_unusable,
    mask_record_unusable,
    source_dimension_mismatch,
    mask_dimension_mismatch,
    source_format_unproven,
    mask_format_unproven,
    source_payload_too_large,
    mask_payload_too_large,
};

const char* skin_skip_reason_name(SkinSkipReason reason) noexcept;

// The suffix appended to a source name to name its mask. The gatherer
// continues the same multiply-by-33 hash with these five bytes.
extern const char kSkinMaskSuffix[6];

// The names the gatherer builds and hashes, none of them case-folded. A
// vinyl layer is "<car_name>_<texture>" (0x005A9B50 copies the car name,
// appends "_" and appends the part's TEXTURE value); the doorline and the
// hood/spoiler source are the car name plus their literal suffix.
std::string skin_vinyl_source_name(const std::string& car_name, const std::string& texture);
std::string skin_doorline_source_name(const std::string& car_name, SkinDoorlineVariant variant);
std::string skin_hood_spoiler_source_name(
    const std::string& car_name,
    SkinHoodSpoilerVariant variant);

// The mask name. The gatherer does not build this string: 0x005AA009 takes
// the base hash already in record +0x00 and continues it with the five
// bytes of "_MASK" (0x005AA010 imul ecx, ecx, 0x21; 0x005AA016 add ecx,
// eax). That is the same value hashing the concatenation gives, because the
// hash is h = h * 33 + byte with no length term, and the retail build
// relies on the two agreeing: 0x005A9C10 and 0x005A97F0 build the full
// string and re-hash it from the seed to reach the same records.
std::string skin_mask_name(const std::string& source_name);

// Packs the four part attributes the caller at 0x005AA134..0x005AA19F reads
// into the lane order every BodySkinRequest field uses: lane 0 RED, lane 1
// GREEN, lane 2 BLUE, lane 3 GLOSS.
constexpr std::uint32_t skin_pack_paint(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue,
    std::uint8_t gloss) noexcept {
    return static_cast<std::uint32_t>(red) |
           (static_cast<std::uint32_t>(green) << 8U) |
           (static_cast<std::uint32_t>(blue) << 16U) |
           (static_cast<std::uint32_t>(gloss) << 24U);
}

// 0x005AA1A7 substitutes 0xFF << (8 * k) for remap color k when the layer's
// color slot is empty, which makes the remap an identity on lanes 0..2.
// Only 0..3 is meaningful; a larger index is reduced modulo 4.
constexpr std::uint32_t skin_default_remap_color(std::size_t index) noexcept {
    return 0xFFU << (8U * static_cast<unsigned int>(index & 3U));
}

// The NUMCOLOURS the hood/spoiler vinyl and the doorline always get. It is
// not a fallback: 0x005A9FD4 stores this constant into record +0x1C for
// source indexes 0 and 5 and never reads a part attribute for them, and the
// only other write to that field, 0x005A9F95, belongs to the vinyl-layer
// block. Those two sources therefore have no NUMCOLOURS to select, which is
// why their selection structs carry none.
constexpr std::uint32_t kSkinFixedPaletteSize = 16;

// The one place a stored palette becomes compositor colors: each 4-byte
// entry is loaded as a little-endian dword, so stored byte 0 becomes lane 0.
//
// That byte order is a fact of the file (docs/FORMATS.md). What each lane
// then *means* is not proven: docs/FORMATS.md records the channel meaning of
// the four palette bytes as outside its structural claim, and the A8R8G8B8
// channel mapping it cites comes from the public D3DFORMAT numbering, not
// from the pixels. The retail evidence that makes this load correct for the
// type-32 path is different and stronger: 0x00407E70..0x00407E7B copies a
// palette entry into a pixel whole, so an expanded surface has exactly the
// byte-lane order the palette had on disk. For the type-8 path, which takes
// a palette rather than an expanded surface, nothing proves the order; see
// docs/BODY_SKIN.md, "What the retail data does not decide".
SkinPalette skin_palette_from_p8(const P8Palette& palette) noexcept;

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

// The hood/spoiler vinyl, source index 0. It is a variant of the car name
// and nothing else: no part attribute reaches it, and its NUMCOLOURS is
// always kSkinFixedPaletteSize. The gatherer looks REMAP up for the four
// vinyl layers only (0x005AA0AC..0x005AA0BA), so this source never remaps
// and its record's four remap colors stay zeroed as 0x005A9ED3 left them.
struct SkinHoodSpoilerSelection {
    SkinHoodSpoilerVariant variant = SkinHoodSpoilerVariant::hood_and_spoiler;
};

// One of VINYL_LAYER0..3, source indexes 1..4. This is the only source that
// carries a part-supplied name, NUMCOLOURS and REMAP.
//
// The gatherer also drops a layer whose installed part is the same object
// as an earlier layer's (0x005A9F25..0x005A9F3A compares the slot dwords,
// before any name is built). That is part identity, not the TEXTURE value,
// so two different parts sharing a TEXTURE value are both kept. This layer
// takes no part identity and therefore does not reproduce that rule; a
// caller with a part database applies it by not selecting the later layer.
struct SkinVinylLayerSelection {
    // Empty is a malformed selection: it would hash "<car_name>_".
    std::string texture;
    // Empty means "the part carried no NUMCOLOURS". Zero is the same thing
    // to the gatherer, whose attribute getter cannot tell them apart, and
    // either abandons the whole composite through the epilogue at
    // 0x005A9F9A..0x005A9FA4 rather than skipping one record. Both entry
    // points report it as an error instead of inventing a count.
    std::optional<std::uint32_t> palette_size;
    // Whether the part's REMAP attribute is present and non-zero
    // (0x005AA0E8 tests presence, 0x005AA10F the value).
    bool remap = false;
    // Empty entry k means "slot VINYL_COLOUR<layer>_<k> is empty", which
    // substitutes skin_default_remap_color(k) at 0x005AA1A7. Only the first
    // three ever reach a blend; the fourth is carried and never used.
    //
    // Read only when remap is true: 0x005AA10F leaves for 0x005AA1CC on a
    // zero REMAP, before the colour loop at 0x005AA134, so a layer that
    // does not remap carries four zeroed colours and is still counted.
    std::array<std::optional<std::uint32_t>, 4> remap_colors{};
};

// The doorline, source index 5. Like the hood/spoiler source it is a
// variant of the car name with a fixed NUMCOLOURS, and it never remaps.
struct SkinDoorlineSelection {
    SkinDoorlineVariant variant = SkinDoorlineVariant::plain;
};

// The complete explicit input state. Layer order is the gatherer's:
// hood/spoiler vinyl, VINYL_LAYER0..3, doorline.
struct BodySkinSelection {
    bool kr_vinyl=false; // Owner-provided whole-car cosmetic, separate from retail texture names.
    // The car name used as the source-name prefix, raw and never folded.
    std::string car_name;
    SkinTargetType target = SkinTargetType::type32;
    // The target's dimensions. Both must be non-zero: a zero-dimension
    // target cannot carry any of the layers this would assemble, and both
    // composite routines refuse a present layer with an empty view.
    // When target_hash names a usable record they may be left at zero and
    // are then taken from that record; when both are set they must agree
    // with it.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Optional: the name hash of the target texture, resolved through the
    // same sources for provenance and for the dimension cross-check above.
    // The retail path takes it from RideInfo+0x444, which initialisation at
    // 0x004C4230 sets to bin_hash("DUMMY_SKIN1"); nothing here picks it.
    std::optional<std::uint32_t> target_hash;

    std::optional<SkinHoodSpoilerSelection> hood_spoiler_vinyl;
    std::array<std::optional<SkinVinylLayerSelection>, 4> vinyl_layers{};
    std::optional<SkinDoorlineSelection> doorline;

    // The BASE_PAINT part's packed color; see skin_pack_paint.
    std::uint32_t base_color = 0;
    // PAINT_ENGINE, PAINT_SPOILER, PAINT_BRAKE, PAINT_EXHAUST. An empty
    // entry falls back to base_color, as 0x005A9DE0 does for an empty slot.
    std::array<std::optional<std::uint32_t>, 4> special_colors{};
};

// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------

// A caller-supplied label, copied verbatim into the provenance and never
// validated. It does not affect lookup: only source order does.
enum class SkinSourceKind { car_textures, car_vinyls, other };

const char* skin_source_kind_name(SkinSourceKind kind) noexcept;

// One read-only image and the packs parsed from it. bytes must still hold
// exactly the image every pack was parsed from; path is a label this layer
// never opens. A pack whose parse_source or file_size does not match, or
// whose record tables were not parsed, is a hard error rather than a
// silently weaker source.
//
// Lookup rule, stated once because it is a deliberate policy and not a
// trace: sources are searched in the order given, packs within a source in
// the order given, records within a pack in index order, and the first
// record that claims a hash wins, even when that record is unusable, so a
// broken car texture is never silently replaced. Every other claimant is
// recorded in SkinTextureProvenance::also_claimed_by.
//
// Retail does not work this way. docs/CAR_SCENE.md records that the lookup
// at 0x005461C0 walks a global bucket list and moves a hit to the front, so
// priority is most-recently-used after the first hits; the car TEXTURES.BIN
// is only *requested* before VINYLS.BIN. This layer therefore imposes a
// deterministic order over an explicit list, exactly as
// decode_render_texture_sources does, instead of guessing that MRU state.
struct SkinInputSource {
    const std::vector<unsigned char>* bytes = nullptr;
    std::vector<const TexturePack*> packs;
    SkinSourceKind kind = SkinSourceKind::other;
    std::string path;
};

// ---------------------------------------------------------------------------
// Limits
// ---------------------------------------------------------------------------

struct SkinInputLimits {
    // Lowering-only: the effective bound is the smaller of this and the
    // compositor's own cap for the target type (kSkinMaxType32Pixels or
    // kSkinMaxType8Pixels), so a larger value cannot produce a request the
    // compositor would then refuse.
    std::size_t max_pixels = kSkinMaxType32Pixels;
    // Upper bound on the declared payload bytes of one record. A record
    // over it is skipped with a reason by both entry points, never a hard
    // failure, so a plan and an assembly always agree.
    std::uint32_t max_payload_bytes = 64U * 1024U * 1024U;
    // Upper bound on the owned pixel and palette bytes of the whole
    // assembly, checked before each allocation.
    std::uint64_t max_total_bytes = 256ULL * 1024ULL * 1024ULL;
};

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

// One record that claims a hash, located exactly.
struct SkinHashClaim {
    std::size_t source = 0;
    std::size_t pack = 0;
    std::size_t record = 0;
};

// Everything known about the record a name hash resolved to. Raw identifiers
// are kept even when the record is unusable.
struct SkinTextureProvenance {
    bool resolved = false;
    std::size_t source_index = 0;
    std::size_t pack_index = 0;
    SkinSourceKind source_kind = SkinSourceKind::other;
    std::string source_path;
    std::uint64_t pack_root_offset = 0;
    std::string pack_name;
    std::uint32_t pack_hash = 0;
    std::size_t record_index = 0;
    // The three hashes a record can claim, preserved unchanged.
    std::uint32_t record_hash = 0; // info +36
    std::uint32_t table_hash = 0;  // hash table +0
    std::uint32_t blob_hash = 0;   // blob table +0, JDLZ storage only
    std::string stored_name;
    bool stored_name_full = false;    // the 23-character field is full
    bool stored_name_matches = false; // bin_hash(stored_name) == wanted hash
    bool record_valid = false;
    std::string record_problem;
    TextureStorage storage = TextureStorage::jdlz;
    PixelFormat format = PixelFormat::unsupported;
    std::uint8_t format_code = 0;
    std::uint32_t d3d_format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_count = 0;
    std::uint32_t data_size = 0;
    // info +60, a byte count (1024 for every observed P8 record). Not the
    // layer's NUMCOLOURS, which is SkinInputLayerInfo::palette_entries.
    std::uint32_t record_palette_bytes = 0;
    std::uint32_t top_level_size = 0;
    std::uint64_t info_offset = 0;
    std::uint64_t blob_offset = 0;
    std::uint32_t blob_size = 0;
    // Every other record that also claims this hash, in search order, from
    // the winner's own pack as well as from later sources.
    std::vector<SkinHashClaim> also_claimed_by;
};

// What assembly, and only assembly, learned about one layer.
struct SkinAssembledLayerInfo {
    // True when a P8 record was expanded through its own palette for a
    // type-32 target, as 0x00407DB0 does. False when the stored elements
    // were used unchanged.
    bool source_expanded = false;
    bool mask_expanded = false;
    // The highest palette index the decoded level-0 data uses, plus one,
    // and whether that exceeds palette_entries. The game does not check
    // this and composite_body_skin_8 reproduces its byte wrap, so it is
    // reported and never corrected. Zero when the record was not paletted.
    std::uint32_t source_indices_used = 0;
    bool source_indices_exceed_palette = false;
    std::uint32_t mask_indices_used = 0;
    // Owned bytes of this layer's source and mask together.
    std::uint64_t owned_bytes = 0;
};

// One selected source index, in gatherer order.
struct SkinInputLayerInfo {
    SkinSourceRole role = SkinSourceRole::hood_spoiler_vinyl;
    std::string source_name;
    std::string mask_name;
    std::uint32_t source_hash = 0;
    std::uint32_t mask_hash = 0;
    SkinTextureProvenance source;
    SkinTextureProvenance mask;
    // The layer's NUMCOLOURS, an entry count, and whether it is the fixed
    // kSkinFixedPaletteSize rather than a part's. It is carried for both target
    // types because the gatherer writes record +0x1C unconditionally
    // (0x005A9F56); only the palette pointers at +0x10 and +0x18 are
    // conditional (0x005AA054, 0x005AA08B), and a type-32 assembly leaves
    // those null as the gatherer does.
    std::uint32_t palette_entries = 0;
    bool palette_entries_defaulted = false;
    bool remap = false;
    std::array<std::uint32_t, 4> remap_colors{};
    // Which of the four remap colors came from skin_default_remap_color.
    std::array<bool, 4> remap_color_defaulted{};
    // True when this source resolved, paired with its mask, matched the
    // target dimensions and had a proven conversion, so it becomes
    // request().layers[layer_index].
    bool usable = false;
    // The position this source takes in request().layers. A plan predicts
    // it from the same order an assembly uses; an assembly confirms it.
    std::size_t layer_index = 0;
    SkinSkipReason skip_reason = SkinSkipReason::none;
    // ASCII detail for skip_reason. Empty when usable.
    std::string skip_detail;
    // Filled by assembly only.
    std::optional<SkinAssembledLayerInfo> assembly;
};

// ---------------------------------------------------------------------------
// Plan: metadata only, no pixel data is read
// ---------------------------------------------------------------------------

struct BodySkinInputPlan {
    SkinTargetType target = SkinTargetType::type32;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string car_name;
    // The target hash the selection named, preserved so that a target that
    // resolved to nothing is visible rather than silently absent.
    std::optional<std::uint32_t> target_hash;
    // Resolved only when target_hash named a record in the sources.
    SkinTextureProvenance target_texture;
    // One entry per selected source index, in gatherer order, including
    // the ones that will not become layers.
    std::vector<SkinInputLayerInfo> inputs;
    std::size_t usable_inputs = 0;
    std::size_t skipped_inputs = 0;
    std::uint32_t base_color = 0;
    std::array<std::uint32_t, 4> special_colors{};
    std::array<bool, 4> special_color_defaulted{};
    // Type-8 only: 5 plus the NUMCOLOURS of every usable input, and whether
    // that fits the 256 entries composite_body_skin_8 merges into. A plan
    // reports an overflow; an assembly refuses it before allocating, so a
    // caller is never handed megabytes the compositor will reject. Zero and
    // true for a type-32 target, whose layers carry no palette.
    std::uint64_t merged_palette_entries = 0;
    bool merged_palette_fits = true;
};

// Resolves every selected source and mask against the sources and records
// what was found, without reading, decompressing or decoding pixel data.
// plan is reset on entry and left empty on failure.
//
// Returns false only for a malformed selection, a malformed source list or a
// bound the selection itself violates; a source that simply does not resolve
// is reported in inputs[i] with usable == false and a skip reason.
bool plan_body_skin_inputs(
    const BodySkinSelection& selection,
    const std::vector<SkinInputSource>& sources,
    const SkinInputLimits& limits,
    BodySkinInputPlan& plan,
    std::string& error);

// ---------------------------------------------------------------------------
// Assembly: owned buffers behind a BodySkinRequest
// ---------------------------------------------------------------------------

// The pixel and palette storage of one assembled layer. Heap-allocated and
// never moved, so the SkinViews in the request stay valid for the lifetime
// of the AssembledSkinInputs that owns it, including across a move.
struct SkinLayerStorage {
    std::vector<std::uint32_t> source32;
    std::vector<std::uint32_t> mask32;
    std::vector<std::uint8_t> source8;
    std::vector<std::uint8_t> mask8;
    SkinPalette source_palette{};
    SkinPalette mask_palette{};
};

// An assembled request together with everything it points at.
//
// The storage is private and the request is handed out by const reference,
// so the ownership rule is a compile-time fact rather than a convention:
// copying is deleted, because a copy could not own the buffers the copied
// request points at; moving is safe, because the storage lives on the heap
// behind unique_ptr and no view is invalidated; and a moved-from object is
// left empty rather than keeping a request whose layers are gone. The
// request also stays valid after the texture packs and their bytes are
// destroyed: nothing borrowed from them survives assembly.
class AssembledSkinInputs {
public:
    AssembledSkinInputs() = default;
    ~AssembledSkinInputs() = default;
    AssembledSkinInputs(const AssembledSkinInputs&) = delete;
    AssembledSkinInputs& operator=(const AssembledSkinInputs&) = delete;
    AssembledSkinInputs(AssembledSkinInputs&& other) noexcept;
    AssembledSkinInputs& operator=(AssembledSkinInputs&& other) noexcept;

    // Valid for as long as this object lives and is not moved from.
    const BodySkinRequest& request() const noexcept { return request_; }
    const BodySkinInputPlan& plan() const noexcept { return plan_; }
    SkinTargetType target() const noexcept { return plan_.target; }
    std::size_t layer_count() const noexcept { return request_.layers.size(); }
    // Index into plan().inputs of request().layers[layer], or
    // plan().inputs.size() when layer is out of range.
    std::size_t input_of_layer(std::size_t layer) const noexcept;
    // The storage request().layers[layer] points into, or null.
    const SkinLayerStorage* layer_storage(std::size_t layer) const noexcept;
    std::uint64_t owned_bytes() const noexcept { return owned_bytes_; }
    bool empty() const noexcept { return request_.layers.empty(); }
    // Drops every buffer and leaves an empty object.
    void clear() noexcept;

private:
    friend bool assemble_body_skin_inputs(
        const BodySkinSelection&,
        const std::vector<SkinInputSource>&,
        const SkinInputLimits&,
        AssembledSkinInputs&,
        std::string&);

    BodySkinRequest request_;
    BodySkinInputPlan plan_;
    std::vector<std::unique_ptr<SkinLayerStorage>> storage_;
    std::vector<std::size_t> input_of_layer_;
    std::uint64_t owned_bytes_ = 0;
};

// Re-checks the ownership invariant from the outside: one storage per
// request layer, every view pointing into that storage, every view at least
// width * height long, and only the fields the target type uses set. Meant
// for tests and for a caller that built a request from several places.
bool validate_assembled_skin_inputs(const AssembledSkinInputs& assembled, std::string& error);

// Resolves as plan_body_skin_inputs does, then reads, decompresses and
// copies the level-0 data of every usable pair into owned buffers and builds
// the request. out is reset on entry and left empty on failure. Only level 0
// is used: neither composite routine contains a downsample loop, and the
// retail target carries a single mip level (docs/BODY_SKIN.md).
//
// Conversion happens only where the retail path proves it:
//
//   type-32 target, A8R8G8B8 source (format code 0x20, D3DFORMAT 21)
//     The stored little-endian dwords are used unchanged. The upload at
//     0x00407DB0 leaves a non-paletted record alone, and 0x005A8A50 reads
//     four bytes per source and mask element.
//
//   type-32 target, P8 source (format codes 0x08, 0x80, 0x81, D3DFORMAT 41)
//     Each index is expanded through the record's own palette and the entry
//     is copied whole, as 0x00407E70..0x00407E7B does before 0x00407EB1
//     rewrites the object's type byte to 0x20. This is the path every
//     retail vinyl and vinyl mask actually takes, which is why one request
//     can hold a P8 vinyl and an A8R8G8B8 doorline at once.
//
//   type-8 target, P8 source
//     Index bytes are passed through and the palette is loaded by
//     skin_palette_from_p8, which is what layer record fields +0x0C, +0x10,
//     +0x14 and +0x18 describe. On the reference build nothing reaches this
//     path, because every paletted texture is expanded before it could be a
//     source, so it is covered by synthetic fixtures only.
//
// Every other pairing is refused. In particular a 32-bit surface is never
// reduced to indices for a type-8 target: no traced instruction does that.
//
// A source whose record is unusable, whose mask is missing, whose dimensions
// are not the target's, whose payload is over the limit, or whose format the
// target cannot take is skipped with a reason, as the gatherer's failure
// paths skip a record. It returns false only for
//   - a malformed selection or source list,
//   - a bound the selection itself violates (max_pixels, the target type's
//     own pixel cap, a NUMCOLOURS over 256),
//   - a type-8 palette merge that cannot fit,
//   - a level-0 read that failed,
//   - SkinInputLimits::max_total_bytes running out, or
//   - an allocation that threw.
bool assemble_body_skin_inputs(
    const BodySkinSelection& selection,
    const std::vector<SkinInputSource>& sources,
    const SkinInputLimits& limits,
    AssembledSkinInputs& out,
    std::string& error);

} // namespace nfsu
