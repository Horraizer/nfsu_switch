#pragma once

#include "nfsu/memory_arena.h"
#include "nfsu/scene.h"
#include "nfsu/track_draw_list.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nfsu {

// GPU upload bookkeeping for the prepared scenes: which resources a scene
// uploads, which texture formats may be uploaded at all, and when the memory
// of each placement may be used again. See docs/GPU_UPLOAD.md.
//
// This layer is the CPU half of a deko3d upload path, and it is the whole of
// what can be built and tested without a Switch. It calls no deko3d function
// and copies no byte. A backend that links it is left with a thin layer of
// its own: create the memory blocks the arenas stand for, copy each
// resource's bytes to the offset the ledger chose, record the texture copies,
// and tell the ledger which fences the GPU has completed.
//
// It sits beside the backend, at the level of nfsu_track_draw_list: it reads
// a PreparedCarScene or a PreparedTrackScene and nothing below them, so it
// never reaches the parsers, the texture decoder or the culler.

// ---------------------------------------------------------------------------
// deko3d constants
// ---------------------------------------------------------------------------

// Copied from include/deko3d.h of devkitPro/deko3d at commit
// 350f2b00a3e76ecd4f00191f8c5d6544ffbcb9db, which is not a dependency of
// this project; docs/GPU_UPLOAD.md gives the lines. They are restated rather
// than included so that the desktop build needs no devkitPro toolchain, and
// the deko3d backend should static_assert each against the real header.

// DK_MEMBLOCK_ALIGNMENT: a memory block's size must be a multiple of it.
constexpr std::uint64_t kDekoMemBlockAlignment = 0x1000;
// DK_IMAGE_LINEAR_STRIDE_ALIGNMENT, the alignment the deko3d sample
// framework gives the staging memory an image is copied from.
constexpr std::uint64_t kDekoImageLinearStrideAlignment = 32;
// DK_UNIFORM_BUF_ALIGNMENT. No resource in an upload table is a uniform
// buffer; it is here because docs/GPU_UPLOAD.md names it as the alignment
// per-frame constants will need.
constexpr std::uint64_t kDekoUniformBufferAlignment = 0x100;

// The largest single resource a deko3d memory block can hold:
// DkMemBlockMaker::size is a uint32_t and must be a multiple of
// kDekoMemBlockAlignment, so no block, and no placement inside one, is
// larger than this. DkBufExtents::size, which binds a vertex buffer, is a
// uint32_t as well.
constexpr std::uint64_t kMaxUploadResourceBytes = 0xFFFFF000ULL;

// ---------------------------------------------------------------------------
// The format gate
// ---------------------------------------------------------------------------

// RGBA8 is the desktop/default preparation path. GPU preparation may retain
// retail DXT1/DXT3 blocks (BC1/BC2), optionally with authored mip levels.
enum class TextureUploadFormat {
    // RenderTexture::rgba and RuntimeSkin::rgba: four bytes per texel in
    // R, G, B, A order, rows top to bottom, the top level only.
    rgba8,
    // Native DXT blocks are uploadable; raw A8R8G8B8 and P8 are not.
    dxt1,
    dxt3,
    a8r8g8b8,
    p8,
    unsupported,
};

constexpr std::size_t kTextureUploadFormatCount = 6;

// "RGBA8", and then exactly the pixel_format_name spelling of each pack
// format: "DXT1", "DXT3", "A8R8G8B8", "P8" and "unsupported". A test holds
// the two spellings together.
const char* texture_upload_format_name(TextureUploadFormat format) noexcept;

// What an accepted texture uploads.
struct TextureUploadShape {
    TextureUploadFormat format = TextureUploadFormat::rgba8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Use the existing alignment gap; upload tables contain thousands of shapes.
    std::uint32_t mip_count = 1;
    std::uint64_t texel_bytes = 0;
    std::uint64_t row_bytes = 0;
    std::uint64_t bytes = 0;
    std::uint64_t rows() const noexcept {
        return format == TextureUploadFormat::dxt1 || format == TextureUploadFormat::dxt3 ?
               (std::uint64_t{height} + 3) / 4 : height;
    }
};

// Accepts nonzero RGBA8, DXT1 or DXT3 with exactly the requested mip-chain byte count
// and at most kMaxUploadResourceBytes. row_bytes is a row of texels for
// RGBA8 or blocks for DXT. texel_bytes is correspondingly 4, 8 or 16.
// Refusal leaves shape zeroed and error names the violated rule.
bool check_texture_upload_format(
    TextureUploadFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t byte_count,
    TextureUploadShape& shape,
    std::string& error, std::uint32_t mip_count = 1);

bool texture_upload_mip(const TextureUploadShape& chain, std::uint32_t level,
                        TextureUploadShape& shape, std::uint64_t& offset,
                        std::string& error);

// ---------------------------------------------------------------------------
// The resource table
// ---------------------------------------------------------------------------

enum class UploadResourceKind { vertex_buffer, index_buffer, texture };

// "vertex buffer", "index buffer" or "texture". Stable.
const char* upload_resource_kind_name(UploadResourceKind kind) noexcept;

enum class UploadLifetime {
    // Released together with the scene the table was built from: every
    // buffer and every texture decoded from a pack.
    scene,
    // A generated body skin (include/nfsu/runtime_skin.h). It is the one
    // resource no file holds: it is composed again whenever the
    // customization changes, so it is released and replaced on its own while
    // the rest of the scene stays resident, and it never outlives the scene.
    generated,
};

// "scene" or "generated". Stable.
const char* upload_lifetime_name(UploadLifetime lifetime) noexcept;

// Which kind of deko3d memory a resource is placed in. The two cannot share
// one memory block: a block's flags are fixed when it is made.
enum class UploadMemory {
    // Written in place by the CPU and read by the GPU: a block with the
    // deko3d default flags, CpuUncached | GpuCached, which need no cache
    // flush after a CPU write. Vertex and index buffers.
    buffer,
    // Sampled by the GPU in a layout of its own: a GpuCached | Image block,
    // reached only by a copy from staging memory. Textures.
    image,
};

// "buffer" or "image". Stable.
const char* upload_memory_name(UploadMemory memory) noexcept;

// The placement alignment of each resource kind. None is a guess, but only
// the texture one is a deko3d constant; docs/GPU_UPLOAD.md says which part
// of each is unverified on hardware.
//
// Vertex and index buffers take the alignment of their element type, which
// is what the deko3d sample framework passes for both (alignof(Vertex) and
// alignof(u16)); the header states no stricter rule for either.
constexpr std::uint64_t kCarVertexAlignment = alignof(MeshVertex);
constexpr std::uint64_t kTrackVertexAlignment = alignof(TrackVertex);
constexpr std::uint64_t kIndexAlignment = alignof(std::uint16_t);
// A texture's staging copy. The image placement itself is whatever the
// layout asks for; see ImageLayoutQuery.
constexpr std::uint64_t kTextureStagingAlignment = kDekoImageLinearStrideAlignment;

struct UploadResource {
    UploadResourceKind kind = UploadResourceKind::vertex_buffer;
    UploadLifetime lifetime = UploadLifetime::scene;
    UploadMemory memory = UploadMemory::buffer;
    // ASCII, unique within its table and stable across builds of the same
    // scene: what a leak report prints. It carries the prepared position, so
    // two meshes with one stored name stay apart. A stored mesh name is
    // copied with every byte outside 0x20..0x7E replaced by '?', and left
    // out when it is empty.
    std::string name;
    // The prepared mesh a buffer comes from, or the prepared texture.
    std::size_t source = 0;
    // The bytes the CPU copies. They are borrowed from the scene, so they are
    // valid only while that scene lives unchanged; a table must not outlive
    // it.
    const unsigned char* data = nullptr;
    std::uint64_t bytes = 0;
    // What the placement must be a multiple of. For a texture this is the
    // staging copy's; the image's comes from the layout.
    std::uint64_t alignment = 0;
    // One vertex, one u16 index, or one texel, and how many of them.
    std::uint64_t element_bytes = 0;
    std::uint64_t elements = 0;
    // Textures only: the hash the texture answers to, and what the format
    // gate accepted.
    std::uint32_t texture_hash = 0;
    TextureUploadShape texture;
};

// Every count and byte total of a table, by kind. Each is a sum over the
// resources, so a test can recompute all of them.
struct UploadTotals {
    std::uint64_t resources = 0;
    std::uint64_t vertex_buffers = 0;
    std::uint64_t index_buffers = 0;
    std::uint64_t textures = 0;
    // Textures with UploadLifetime::generated, also counted in textures.
    std::uint64_t generated_textures = 0;
    std::uint64_t vertex_bytes = 0;
    std::uint64_t index_bytes = 0;
    std::uint64_t texture_bytes = 0;
    std::uint64_t generated_texture_bytes = 0;
    // The single largest texture: the staging a backend needs when it copies
    // one texture at a time.
    std::uint64_t largest_texture_bytes = 0;
    // Prepared meshes whose vertex or index stream is empty. They upload
    // nothing, because nothing can be placed in zero bytes and nothing in
    // such a stream is drawn.
    std::uint64_t empty_buffers = 0;
};

struct UploadTable {
    // Mesh order, each mesh's vertex buffer before its index buffer, then
    // the textures in scene order.
    std::vector<UploadResource> resources;
    UploadTotals totals;
};

// The upload table of a car scene. validate_prepared_scene runs first, and a
// scene it refuses is refused here with its message. Every texture then goes
// through the format gate.
//
// A texture whose RenderTexture::source equals generated_texture_source gets
// UploadLifetime::generated. That is the opaque provenance source index
// bind_runtime_skin stamps on the texture it binds, and
// RuntimeSkinBinding::source reports it back; pass kNoHandle when the scene
// holds no generated skin. kNoHandle marks nothing generated, even a texture
// whose own source index is kNoHandle.
//
// Fails, with `table` left empty, on a refused scene, on a texture the gate
// refuses, on a resource above kMaxUploadResourceBytes, and on an allocation
// failure; std::bad_alloc is caught.
bool build_car_upload_table(
    const PreparedCarScene& scene,
    std::size_t generated_texture_source,
    UploadTable& table,
    std::string& error);

// The upload table of a placed track region. validate_prepared_track_scene
// runs first. The instances, the sections and the draw list are not
// uploaded: they are per-draw data a backend passes some other way, and
// docs/GPU_UPLOAD.md says why they are not in the table.
//
// The region already counts its upload in TrackDrawCost, and this table is
// that upload broken down, not a second accounting of it: its vertex, index
// and texture totals equal vertex_buffer_bytes, index_buffer_bytes and
// texture_bytes, because both multiply the same vectors by the same element
// sizes. The validator recomputes the cost, so the two cannot disagree for a
// scene it accepts; tests/gpu_upload_tests.cpp pins the equality so that a
// later change to either side has to change both.
//
// Fails as build_car_upload_table does.
bool build_track_upload_table(
    const PreparedTrackScene& scene,
    UploadTable& table,
    std::string& error);

// ---------------------------------------------------------------------------
// The ledger
// ---------------------------------------------------------------------------

// Where a ledger places things. One arena may serve several roles; a deko3d
// backend needs at least two, because images need a block of their own.
struct UploadArenas {
    // UploadMemory::buffer placements.
    MemoryArena* buffers = nullptr;
    // UploadMemory::image placements.
    MemoryArena* images = nullptr;
    // The staging copy of every texture, held until its copy completes.
    MemoryArena* staging = nullptr;
};

// The size and the alignment of a texture's GPU image. On deko3d that is
// dkImageLayoutGetSize and dkImageLayoutGetAlignment of the layout the
// backend builds for it, and nothing on the CPU side can know either. Return
// false with `error` set to refuse the texture.
using ImageLayoutQuery = std::function<bool(const UploadResource& texture,
                                            std::uint64_t& bytes,
                                            std::uint64_t& alignment,
                                            std::string& error)>;

// The lower-bound stand-in: the linear payload size and the staging
// alignment. A block-linear layout is at least that big, so a budget
// computed with it is a lower bound, never a measurement. It refuses a
// resource that is not a texture.
// Conservative bound for one-level, non-array 2D RGBA8/BC1/BC2 images.
// Maxwell GOBs are 64 bytes by 8 rows; a tile is at most 16 GOBs high.
// Round every image to a whole 64-byte by 128-row tile and align to 4096.
// The Switch still queries its actual layout before allocating.
bool bounded_image_layout(const UploadResource& texture,
                          std::uint64_t& bytes, std::uint64_t& alignment,
                          std::string& error);

bool linear_image_layout(const UploadResource& texture,
                         std::uint64_t& bytes,
                         std::uint64_t& alignment,
                         std::string& error);

// Names one tracked resource. Like ArenaHandle, it never becomes valid
// again once the resource is reclaimed, including after the ledger is
// created again.
struct UploadHandle {
    std::uint32_t slot = 0xFFFFFFFFU;
    std::uint64_t generation = 0;
};

// One tracked resource, as a report reads it.
struct UploadRecord {
    std::string name;
    UploadResourceKind kind = UploadResourceKind::vertex_buffer;
    UploadLifetime lifetime = UploadLifetime::scene;
    UploadMemory memory = UploadMemory::buffer;
    // In the buffers or the images arena.
    ArenaAllocation placement;
    // Textures only, and only until the copy's fence is retired.
    bool staged = false;
    ArenaAllocation staging;
    std::uint64_t upload_fence = 0;
    bool released = false;
    std::uint64_t release_fence = 0;
};

struct UploadLedgerStats {
    // Uploaded and not released.
    std::uint64_t resident = 0;
    std::uint64_t resident_bytes = 0;
    // Released, with memory the GPU may still read, so not yet reclaimed.
    std::uint64_t releasing = 0;
    std::uint64_t releasing_bytes = 0;
    // Staging copies not yet reclaimed.
    std::uint64_t staged = 0;
    std::uint64_t staged_bytes = 0;
    // Successful calls, and resources whose memory went back to its arena.
    std::uint64_t uploads = 0;
    std::uint64_t releases = 0;
    std::uint64_t reclaimed = 0;
    // Refused uploads and refused releases.
    std::uint64_t refused_uploads = 0;
    std::uint64_t refused_releases = 0;
    // Arena releases the arena itself refused while this ledger reclaimed:
    // a placement someone else released, or an arena someone reset, behind
    // the ledger's back. Nonzero means the arena and the ledger no longer
    // describe the same memory, which nothing can repair from here.
    std::uint64_t lost_placements = 0;
    // The highest fence retire() has been told the GPU completed.
    std::uint64_t completed_fence = 0;
};

// Tracks every placement from upload to reclaim.
//
// THE SYNCHRONIZATION RULES IT KEEPS, which are the whole reason it exists.
// A fence here is a number the backend assigns in submission order and the
// GPU completes in that order, which is what a deko3d fence signalled on one
// queue gives.
//
//   * A texture's staging copy is reclaimed only once the fence of the
//     submission that copies it has completed. Reusing it earlier would let
//     the copy read another texture's bytes.
//   * A released resource keeps its memory until the fence of the last
//     submission that reads it has completed, and until its own upload fence
//     has, so that neither a draw in flight nor a copy in flight reads memory
//     another upload has taken.
//   * Nothing is reclaimed anywhere but retire(), so a backend that never
//     learns a fence completed can never reuse memory the GPU still reads.
//
// Placement honours alignment through the arenas. Cache maintenance is not
// modelled: it depends on flags the backend picks when it makes a memory
// block, not on anything this ledger decides. With the CpuUncached buffer
// memory the deko3d samples use, a CPU write needs no
// dkMemBlockFlushCpuCache; a CpuCached block needs one over every placement
// written. A texture is copied by the GPU, so its image needs the barrier
// and the image cache invalidation before it is sampled instead.
// docs/GPU_UPLOAD.md gives the calls, and says which of this is unverified on
// hardware.
class UploadLedger {
public:
    UploadLedger() = default;

    // Move-only. Two copies would each reclaim the same placements.
    UploadLedger(const UploadLedger&) = delete;
    UploadLedger& operator=(const UploadLedger&) = delete;
    UploadLedger(UploadLedger&&) = default;
    UploadLedger& operator=(UploadLedger&&) = default;

    // Fails when buffers or images is null, when the layout query is empty,
    // when max_resources is outside 1..kMaxArenaAllocations, and when the
    // bookkeeping cannot be allocated. staging may be null only for a ledger
    // that will never be handed a texture. Every figure starts over, the
    // completed fence included: a new ledger has seen no fence complete, so
    // a backend may number its fences from 1 again.
    bool create(const UploadArenas& arenas,
                ImageLayoutQuery image_layout,
                std::uint32_t max_resources,
                std::string& error);

    // Places one resource and records it as resident: its buffer or image
    // placement and, for a texture, a staging placement of exactly its bytes,
    // reclaimed by the first retire() that completes `fence`. Both
    // placements or neither. Refuses, with `error` naming the resource, the
    // arena and the arena's reason, a resource an arena refuses; one larger
    // than kMaxUploadResourceBytes; a texture whose layout query fails; and
    // the resource limit.
    bool upload(const UploadResource& resource,
                std::uint64_t fence,
                UploadHandle& handle,
                std::string& error);

    // Every resource of a table, all or nothing: on a refusal every
    // placement this call made is returned at once, since nothing was
    // submitted yet, and `handles` is left empty. The slots go back newest
    // first, which leaves the ledger's free slots exactly as the call found
    // them, so a retry is given the same slots in the same order.
    bool upload_table(const UploadTable& table,
                      std::uint64_t fence,
                      std::vector<UploadHandle>& handles,
                      std::string& error);

    // Releases one resource. Its memory is reclaimed by the first retire()
    // that completes both `last_use_fence` and its upload fence. Refuses a
    // handle already released, reclaimed or not, as a double release naming
    // the resource; and a handle this ledger never issued.
    bool release(const UploadHandle& handle,
                 std::uint64_t last_use_fence,
                 std::string& error);

    // Releases each handle in order and stops at the first refusal, which is
    // reported; the handles before it stay released.
    bool release_all(const std::vector<UploadHandle>& handles,
                     std::uint64_t last_use_fence,
                     std::string& error);

    // Tells the ledger the GPU has completed every fence up to and including
    // `completed`, and reclaims what that makes safe. A value below the last
    // one changes nothing: fences only move forward.
    void retire(std::uint64_t completed) noexcept;

    // Resources uploaded and never released, by name, in slot order. This is
    // the leak report: empty when every upload was released.
    std::vector<std::string> unreleased() const;
    // Resources released and not yet reclaimed, by name, in slot order.
    std::vector<std::string> releasing() const;

    // The record a live handle names. False for a stale or unknown handle.
    bool find(const UploadHandle& handle, UploadRecord& record) const;

    UploadLedgerStats stats() const noexcept;

private:
    struct Slot {
        UploadRecord record;
        std::uint64_t generation = 0;
        bool live = false;
    };

    bool place(const UploadResource& resource, std::uint64_t fence, Slot& slot,
               std::string& error);
    void reclaim(std::uint32_t index) noexcept;
    std::uint32_t take_slot() noexcept;

    UploadArenas arenas_{};
    ImageLayoutQuery image_layout_;
    // The generation a new slot starts at, raised by create() past every
    // generation the old slots reached, exactly as MemoryArena does.
    std::uint64_t generation_floor_ = 0;
    std::uint32_t max_resources_ = 0;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_slots_;
    std::uint64_t live_ = 0;
    std::uint64_t uploads_ = 0;
    std::uint64_t releases_ = 0;
    std::uint64_t reclaimed_ = 0;
    std::uint64_t refused_uploads_ = 0;
    std::uint64_t refused_releases_ = 0;
    std::uint64_t lost_placements_ = 0;
    std::uint64_t completed_ = 0;
};

} // namespace nfsu
