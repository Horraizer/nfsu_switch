#pragma once

#include "nfsu/body_skin.h"
#include "nfsu/scene.h"
#include "nfsu/skin_inputs.h"
#include "nfsu/texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace nfsu {

// Runtime body skin: composes the selected inputs into the texture the
// retail path writes, and binds the result where a prepared car scene will
// find it.
//
// This layer owns no selection of its own. It consumes the assembly layer
// in nfsu/skin_inputs.h, which resolves the target record and every source,
// and the pixel compositor in nfsu/body_skin.h, which does the per-pixel
// work. It parses nothing: every pack it reads was parsed by the caller and
// handed to it as a SkinInputSource, and no pack is modified.
//
// Three things it decides, each recorded here because each is a policy
// rather than a trace:
//
//   1. Which compositor path to run. It is taken from the real target
//      record, never from a caller flag; see choose_skin_target_path.
//   2. Which hashes the generated texture answers to. The caller states
//      them, the target hash first; see bind_runtime_skin.
//   3. That the generated texture outranks every pack source for those
//      hashes. See bind_runtime_skin.
//
// See docs/RUNTIME_SKIN.md for the evidence and the separation of observed,
// inferred and unknown behaviour.

// ---------------------------------------------------------------------------
// Compositor path
// ---------------------------------------------------------------------------

// Which composite routine a real target record proves, and why.
//
// The dispatch at 0x005AA262 reads byte +0x4A of the target *texture
// object*: 8 selects 0x005A8E90 and anything else selects 0x005A8A50, which
// returns at 0x005A8A7E unless the byte is 0x20. That byte is the pack
// record's format code at info +74, in place, except that the upload at
// 0x00407DB0 expands every D3DFORMAT-41 record through its own palette and
// then writes 0x20 over the byte at 0x00407EB1 (docs/BODY_SKIN.md).
//
// So a record decides the path, and on this build it can only ever decide
// the type-32 one: a non-paletted 0x20 record keeps its byte, and a
// paletted record has the byte rewritten to 0x20 before the dispatch reads
// it. No record reaches the dispatch with byte 8, which is why nothing here
// ever selects SkinTargetType::type8 and why composite_body_skin_8 is not
// reachable from this layer. That is not a limitation this layer chose: a
// type-8 composite produces palette entries whose lane order nothing
// proves (docs/BODY_SKIN.md, "What the retail data does not decide"), so
// there is no way to turn one into an RGBA image without guessing.
struct SkinTargetChoice {
    // False when no traced dispatch composites a record of this format. The
    // path and the byte are then meaningless and reason says why.
    bool proven = false;
    SkinTargetType path = SkinTargetType::type32;
    // The byte +0x4A the retail texture object would carry for this record.
    std::uint8_t object_type_byte = 0;
    // True when 0x00407EB1 rewrote that byte, which also means the surface
    // the compositor sees was expanded through the record's own palette.
    bool expanded_on_upload = false;
    // ASCII, always set, for diagnostics and for the refusal message.
    std::string reason;
};

// Derives the path from a resolved target record. Returns false, with
// choice.proven false and choice.reason set, for an unresolved target, an
// invalid record, or a format no traced dispatch composites.
bool choose_skin_target_path(const SkinTextureProvenance& target, SkinTargetChoice& choice);

// ---------------------------------------------------------------------------
// Target surface
// ---------------------------------------------------------------------------

// The untouched target, as the compositor's marker scan needs to see it.
//
// The pixels are in the target's lane order, that is the stored
// little-endian dword of an A8R8G8B8 record, or the palette entry copied
// whole for a record the upload expands. composite_body_skin_32 compares
// them against kSkinMarkerSurface, and the retail DUMMY_SKIN1 markers match
// that list read as little-endian u32 (docs/BODY_SKIN.md).
struct SkinTargetSurface {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> pixels;
    SkinTargetChoice choice;
};

// Reads level 0 of the record `target` resolved to, through the same
// texture_payload API the pack decoder uses, and converts it only where the
// retail upload proves the conversion. Only level 0 is read: neither
// composite routine downsamples and the retail target carries a single mip
// level.
//
// `sources` must be the same list the target provenance was resolved
// against, because the provenance is a position in it. Fails, leaving
// `out` empty, on an unproven format, a target the limits refuse, a payload
// that cannot be read, or an allocation that threw.
bool read_skin_target_surface(
    const std::vector<SkinInputSource>& sources,
    const SkinTextureProvenance& target,
    const SkinInputLimits& limits,
    SkinTargetSurface& out,
    std::string& error);

// ---------------------------------------------------------------------------
// Composition
// ---------------------------------------------------------------------------

// The generated body skin, as RGBA8.
//
// The bytes are the same mapping decode_level applies to an A8R8G8B8 level:
// red is dword byte 2, green byte 1, blue byte 0 and the fourth byte the
// GLOSS lane. That the fourth lane is an alpha channel is not proven
// (docs/BODY_SKIN.md); it is carried through because a texture in this
// project is RGBA8, and the viewer draws decoded RGB opaquely unless an
// alpha cutoff is asked for.
struct RuntimeSkin {
    std::uint32_t target_hash = 0;
    SkinTargetType path = SkinTargetType::type32;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // How many assembled layers the composite consumed.
    std::size_t layers = 0;
    // Rows as the target stores them, width * height * 4 bytes.
    std::vector<unsigned char> rgba;
};

// Composites `inputs` into `target` and converts the result to RGBA8.
//
// `markers` is the compositor's marker cache and is used exactly as
// composite_body_skin_32 documents: an invalid cache makes the untouched
// target be scanned and the cache filled, a valid one is trusted.
//
// Fails, leaving `out` empty, when the assembled inputs were not built for
// the path the target proves, when their dimensions are not the target's,
// or when the composite itself refuses.
bool compose_runtime_skin(
    const AssembledSkinInputs& inputs,
    const SkinTargetSurface& target,
    SkinMarkerCache& markers,
    RuntimeSkin& out,
    std::string& error);

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

// The most hashes one generated texture can answer to. It is the cap a pack
// record has, because RenderTexture::claims holds three.
constexpr std::size_t kMaxRuntimeSkinHashes = 3;

static_assert(
    kMaxRuntimeSkinHashes <= std::tuple_size<decltype(RenderTexture::claims)>::value,
    "a generated texture cannot claim more hashes than RenderTexture::claims holds");

// A decoded pack texture the generated skin displaced.
struct DisplacedTexture {
    // The bound hash it claimed. A texture claiming several of them is
    // reported once, under the first.
    std::uint32_t hash = 0;
    std::size_t source = 0;
    std::size_t pack = 0;
    std::size_t record = 0;
    bool was_usable = false;
};

struct RuntimeSkinBinding {
    // In binding order, the target hash first.
    std::vector<std::uint32_t> hashes;
    // Where the generated texture landed in RenderTextureSet::textures.
    std::size_t texture_index = 0;
    // The provenance source index the caller gave it.
    std::size_t source = 0;
    std::uint64_t bytes = 0;
    // Pack textures removed from the set, in set order.
    std::vector<DisplacedTexture> displaced;
    // RenderTextureSet::unusable_claims entries removed, in set order.
    std::vector<UnusableClaim> displaced_claims;
};

// Binds the generated skin into a decoded texture set, so that a material
// group naming any of `hashes` resolves to it.
//
// Priority, stated once because it is a policy and not a trace: the
// generated texture outranks every pack source for every hash it is bound
// to. Each entry of RenderTextureSet::textures that claims one of them is
// removed and reported in `binding.displaced`, and each matching
// RenderTextureSet::unusable_claims entry is removed and reported in
// `binding.displaced_claims`; only then is the generated texture appended.
// That is what makes it the single claimant, which is what resolve_material
// requires: two claimants of one hash are `unusable`, never a winner.
//
// The retail render path expresses the same priority the other way round.
// The material slots of the 240SX body name `240SX_SKIN1`, and the
// render-info setup at 0x00580C37..0x00580C67 uses RideInfo+0x444 instead
// whenever it is non-zero, which initialisation at 0x004C4230 sets to
// bin_hash("DUMMY_SKIN1"). Substituting the hash a slot resolves through
// and binding the generated texture under both hashes put the same pixels
// on the same groups; which hashes to bind is the caller's explicit
// statement, never a guess made here.
//
// RenderTextureSet::shadowed is left alone: it records what the decode did,
// and the decode is not changed by this. RenderTextureSet::decoded_bytes is
// recomputed as the sum of the retained RGBA sizes, which is what it is
// documented to be.
//
// `hashes` must hold 1..kMaxRuntimeSkinHashes distinct values with
// `skin.target_hash` first, and `source_index` is the opaque provenance
// source the generated texture carries; a caller that reports per-source
// counts should give it an index past its real sources so that it is not
// counted as one of them. No source pack is read or changed.
//
// Fails, leaving `set` untouched, on a malformed hash list, an unusable
// skin, or an allocation that threw.
bool bind_runtime_skin(
    const RuntimeSkin& skin,
    const std::vector<std::uint32_t>& hashes,
    std::size_t source_index,
    RenderTextureSet& set,
    RuntimeSkinBinding& binding,
    std::string& error);

} // namespace nfsu
