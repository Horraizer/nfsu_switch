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

// Which record a target surface was read from, to the precision the read
// and the composite depend on.
//
// It exists because pixels in a vector carry no provenance. Two valid
// records of the same format and the same size produce surfaces nothing
// downstream can tell apart, so without an identity travelling with them
// the pixels of one target could be composited, labelled and bound as
// another. compose_runtime_skin compares the surface it is handed against
// the record the assembled inputs resolved and refuses a pair that is not
// the same record.
//
// Every field is copied from a record, its pack or its source, never
// derived, so nothing has to be recomputed to compare two of them.
//
// The record fields are the ones that decide which bytes the read takes and
// how it reads them: where the record is, where its payload is, how it is
// stored, what format it is and how large. What SkinTextureProvenance also
// carries and this does not - the two other hashes the record claims, the
// stored name, the validity text, the pack and source labels - decides none
// of that. Neither do info +48 and +52, which texture_payload reads for a
// raw P8 palette and the provenance does not carry at all; within one file
// info_offset already names one record.
//
// `source_bytes` is what makes it more than a position. Every other field
// is an index into the caller's source list or a shape, and a re-encoded
// copy of a file, or a second list holding a pack of the same layout at the
// same index, has all of them. Only the buffer the pixels were actually
// read from separates those, which is why an identity is complete only when
// it was built against a source list.
struct SkinTargetIdentity {
    // False for a provenance that resolved to nothing. Such an identity
    // matches nothing, including another unresolved one.
    bool resolved = false;
    std::size_t source_index = 0;
    std::size_t pack_index = 0;
    std::uint64_t pack_root_offset = 0;
    std::uint32_t pack_hash = 0;
    std::size_t record_index = 0;
    std::uint32_t record_hash = 0; // info +36
    std::uint32_t table_hash = 0;  // hash table +0
    std::uint64_t info_offset = 0;
    TextureStorage storage = TextureStorage::jdlz;
    PixelFormat format = PixelFormat::unsupported;
    std::uint8_t format_code = 0;
    std::uint32_t d3d_format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip_count = 0;
    std::uint32_t data_size = 0;
    std::uint32_t palette_bytes = 0; // info +60
    std::uint32_t top_level_size = 0;
    std::uint64_t blob_offset = 0;
    std::uint32_t blob_size = 0;
    // The buffer the record's bytes come from: SkinInputSource::bytes of
    // the source that holds it, compared and never dereferenced, exactly as
    // TexturePack::parse_source is. It identifies that buffer only while
    // the caller keeps it alive and unmodified in size and address.
    const unsigned char* source_bytes = nullptr;
    std::size_t source_byte_size = 0;
};

// The identity of the record `target` names in `sources`. Resolved only
// when those sources hold that record, with the pack paired with its
// source's bytes and its record tables read, and with every identifier the
// provenance carries still matching - that is, under exactly the conditions
// read_skin_target_surface will read it. Otherwise `resolved` is false and
// the identity matches nothing.
SkinTargetIdentity skin_target_identity(
    const std::vector<SkinInputSource>& sources,
    const SkinTextureProvenance& target);

// True when both are resolved and every field matches: the same record of
// the same pack of the same source, with the same shape, read from the same
// buffer. An unresolved identity matches nothing.
bool same_skin_target(const SkinTargetIdentity& a, const SkinTargetIdentity& b) noexcept;

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
    // The record these pixels came from. Left unresolved by a failed read.
    SkinTargetIdentity identity;
};

// Reads level 0 of the record `target` resolved to, through the same
// texture_payload API the pack decoder uses, and converts it only where the
// retail upload proves the conversion. Only level 0 is read: neither
// composite routine downsamples and the retail target carries a single mip
// level.
//
// `sources` must be the same list the target provenance was resolved
// against, because the provenance is a position in it. The pack that
// position names must still be paired with its source's bytes - parsed from
// exactly that buffer, at exactly that size - which is the rule
// plan_body_skin_inputs and decode_render_texture_sources both enforce, and
// its record tables must have been read, which plan_body_skin_inputs
// requires and decode_render_texture_sources instead treats as an
// incomplete source. A *different* buffer put in the source's place, even
// one of exactly the same size, is otherwise read through offsets that
// describe a different file, under the provenance of a record that file
// does not hold.
//
// What that pairing cannot see is a caller overwriting the contents of the
// same live buffer. It is address and size, exactly as
// TexturePack::parse_source is, and that field's own contract already says
// it identifies a buffer only while the caller keeps it unmodified. Seeing
// through that would mean hashing the file, which nothing here has a reason
// to do.
//
// Fails, leaving `out` empty, on an unproven format, a provenance these
// sources do not pair with or do not hold, a target the limits refuse, a
// payload that cannot be read, or an allocation that threw.
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
// `sources` is the list `target` must have been read from, and must hold
// the record `inputs` resolved its target hash to. It is what makes the
// identity check below decide a file rather than a position, because two
// lists can hold a pack of the same layout at the same index.
//
// It is not checked to be the list `inputs` was assembled from, and does
// not need to be: only the target record is re-located in it, and every
// pixel the composite reads from `inputs` is in a buffer `inputs` owns, so
// a list that differs anywhere but the target changes nothing this
// produces. What has to agree is the surface and the target record, and
// that is what is compared.
//
// `markers` is the compositor's marker cache and is used exactly as
// composite_body_skin_32 documents: an invalid cache makes the untouched
// target be scanned and the cache filled, a valid one is trusted.
//
// Fails, leaving `out` empty, when `sources` does not hold the record
// `inputs` resolved its target hash to, when `target` was not read from
// that record - compared as a SkinTargetIdentity, because same-sized
// surfaces of the same format are otherwise interchangeable and the result
// would be one target's pixels labelled and bound as another's - when the
// assembled inputs were not built for the path the target proves, when
// their dimensions are not the target's, or when the composite itself
// refuses.
bool compose_runtime_skin(
    const std::vector<SkinInputSource>& sources,
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
    // Hashes a removed pack texture claimed that this binding did not name
    // and the retained claim list did not already cover, appended to
    // RenderTextureSet::unusable_claims so that they go on matching
    // something instead of silently matching nothing. One entry per hash,
    // in removal order, then claim order, the record's own hash first.
    std::vector<UnusableClaim> preserved_claims;
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
// Removal is per record, not per hash, and a record can claim up to three
// of them. One bound hash among those takes the whole record away, so the
// hashes this binding never named would stop matching anything at all. Each
// of them is therefore appended to RenderTextureSet::unusable_claims and
// reported in `binding.preserved_claims`, which is what the decode already
// does for a record it leaves out whose claims still block. One entry per
// hash, as the decode leaves: a hash the retained list already covers is
// not added again, and neither is one a record removed earlier in the same
// binding already contributed.
//
// For a set the decode produced, that leaves resolution for those hashes
// exactly as it was: the decode gives a record distinct claims only when
// its three hash fields disagree, which is also what makes it invalid, so
// every hash it claims already resolves as `unusable` and goes on doing
// so. The one
// case where this is not an identity is a hand-built texture that is usable
// and claims several hashes: its unbound hashes move from `resolved` to
// `unusable`, which is the honest report once the pixels they resolved to
// have left the set, and is still nearer than the `unresolved` that
// dropping them would give.
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
