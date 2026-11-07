#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nfsu {

// Memory arenas for the Switch renderer: one bounded offset allocator per
// memory category, with every refusal returned as a reason the caller can act
// on and every diagnostic returned as data. See docs/MEMORY_BUDGETS.md.
//
// WHAT AN ARENA IS
//
// An arena hands out byte ranges of one fixed range [base, base + capacity).
// It owns no memory and never touches the bytes it manages. What backs the
// range is the platform layer's: one block of CPU memory reserved at start-up
// for the three CPU categories, or a deko3d memory block for the GPU one. That
// is what lets a single implementation serve all four categories, and what
// lets it be tested on a desktop with nothing behind it at all.
//
// THE RULES IT KEEPS
//
//   * Every byte of the range is either free or held by exactly one live
//     allocation. The gap an alignment leaves in front of a placement stays
//     free and can be handed out again, so there is no hidden padding and
//     used_bytes + free_bytes is always the capacity.
//   * Placement is first fit in address order, so an arena nobody releases
//     from behaves exactly like a bump allocator, and a released block is
//     merged with both of its free neighbours at once.
//   * All bookkeeping is reserved by create(). allocate(), release() and
//     reset() never allocate and never throw, so running out of memory can
//     never leave an arena half updated.
//   * A refused request changes nothing but the refusal counters. It never
//     returns a range that reaches past base + capacity, which for the CPU
//     categories is the first byte of the next arena.
//   * Every guard is ordinary code, not an assertion, so a Release build with
//     NDEBUG keeps all of them.
//
// Nothing here is thread-safe; an arena has one owner.

// What a range of memory is for. The three CPU categories are told apart by
// lifetime, and the fourth by who reads it.
enum class MemoryCategory {
    // Loaded once and held for the session, such as the car a race draws.
    persistent,
    // Loaded and released as the camera moves: bounded track regions.
    streaming,
    // Built during one frame and released all at once when it ends: the
    // per-frame draw list. This is the frame budget.
    transient,
    // GPU-visible memory: vertex and index buffers, textures and the staging
    // memory textures are copied through.
    gpu,
};

constexpr std::size_t kMemoryCategoryCount = 4;

// "persistent", "streaming", "transient" or "gpu". Stable: tests compare
// against it.
const char* memory_category_name(MemoryCategory category) noexcept;

// Why a request was refused. Each reason says what the caller can do.
enum class ArenaRefusal {
    none,
    // A zero-byte request. Nothing is ever uploaded from an empty buffer.
    zero_size,
    // An alignment of zero, one that is not a power of two, or one above the
    // arena's max_alignment.
    bad_alignment,
    // More bytes than the arena has free in total. No layout could hold the
    // request, so the only remedy is to release something.
    over_budget,
    // Enough free bytes in total, but no single free block holds the request
    // at its alignment. Releasing a neighbour of the largest block, or
    // unloading and reloading in a different order, can cure this; asking
    // again as is cannot.
    no_fit,
    // As many live allocations as ArenaConfig::max_allocations allows.
    allocation_limit,
    // A handle this arena never issued.
    unknown_handle,
    // A handle whose allocation was already released: a double release, or a
    // release through a handle that reset() invalidated.
    stale_handle,
};

constexpr std::size_t kArenaRefusalCount = 8;

// "none", "zero size", "bad alignment", "over budget", "no fit",
// "allocation limit", "unknown handle" or "stale handle". Stable: tests
// compare against it.
const char* arena_refusal_name(ArenaRefusal refusal) noexcept;

// The largest range an arena may manage, and the largest alignment it may
// honour. Both keep every sum and product in the implementation far inside
// 64 bits: an offset plus an alignment, and 1000 times a byte count for the
// fragmentation figure, cannot overflow.
constexpr std::uint64_t kMaxArenaCapacity = 1ULL << 48;
constexpr std::uint64_t kMaxArenaAlignment = 1ULL << 20;
// The most live allocations one arena may be configured to track. Its
// bookkeeping is reserved up front: 52 bytes per allocation on a 64-bit
// build, a 32-byte slot, a 16-byte free block and a 4-byte free-slot entry.
constexpr std::uint32_t kMaxArenaAllocations = 1U << 22;

struct ArenaConfig {
    MemoryCategory category = MemoryCategory::transient;
    // Absolute offset of the first byte, in whatever address space the
    // platform layer backs the category with. It must be a multiple of
    // max_alignment, which is what makes an aligned offset inside the arena
    // an aligned absolute address as well.
    std::uint64_t base = 0;
    // 1 .. kMaxArenaCapacity, and base + capacity must not overflow.
    std::uint64_t capacity = 0;
    // A power of two in 1 .. kMaxArenaAlignment. Requests above it are
    // refused rather than rounded.
    std::uint64_t max_alignment = 16;
    // 1 .. kMaxArenaAllocations.
    std::uint32_t max_allocations = 4096;
};

// Names one live allocation. A handle stays valid until its allocation is
// released, the arena is reset or the arena is created again, and never
// becomes valid again: a slot is reused under a new 64-bit generation, which
// does not wrap in practice, and generations carry on across create().
struct ArenaHandle {
    std::uint32_t slot = 0xFFFFFFFFU;
    std::uint64_t generation = 0;
};

struct ArenaAllocation {
    // Absolute, ArenaConfig::base included, and a multiple of alignment.
    std::uint64_t offset = 0;
    // Exactly the bytes requested. No rounding.
    std::uint64_t size = 0;
    // The alignment the request asked for and the placement honours.
    std::uint64_t alignment = 0;
    ArenaHandle handle;
};

// One free range, absolute.
struct ArenaRange {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

// Every diagnostic an arena keeps, as data. Nothing here is printed.
struct ArenaStats {
    MemoryCategory category = MemoryCategory::transient;
    std::uint64_t base = 0;
    std::uint64_t capacity = 0;
    // Bytes held by live allocations, and the rest. They always sum to the
    // capacity.
    std::uint64_t used_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t live_allocations = 0;
    // Fragmentation. The free bytes lie in free_blocks separate ranges, the
    // largest of which is largest_free_block; that is the biggest request
    // with alignment 1 the arena can take right now.
    std::uint64_t free_blocks = 0;
    std::uint64_t largest_free_block = 0;
    // 1000 * (free_bytes - largest_free_block) / free_bytes, rounded down,
    // and 0 when nothing is free: the share of the free bytes a single
    // request cannot reach. 0 means the free space is one block.
    std::uint32_t fragmentation_permille = 0;
    // One past the highest byte a live allocation holds, relative to base;
    // 0 when nothing is live. How far into the arena the live set reaches,
    // as opposed to how much of it is used.
    std::uint64_t extent = 0;
    // High-water marks since create(): the most bytes live at once, and the
    // furthest extent. high_water_extent minus high_water_bytes is what
    // fragmentation and alignment gaps cost at the worst moment.
    std::uint64_t high_water_bytes = 0;
    std::uint64_t high_water_extent = 0;
    // The same two marks since the last reset(), or since create() before
    // the first. For the transient arena, reset once per frame, this is the
    // frame's peak.
    std::uint64_t period_high_water_bytes = 0;
    std::uint64_t period_high_water_extent = 0;
    // Successful calls since create().
    std::uint64_t allocations = 0;
    std::uint64_t releases = 0;
    std::uint64_t resets = 0;
    // Refused calls since create(), by reason; refused[0] (none) stays 0.
    // refusals is their sum.
    std::uint64_t refusals = 0;
    std::array<std::uint64_t, kArenaRefusalCount> refused{};
};

class MemoryArena {
public:
    // An arena that was never created has capacity 0: it refuses every
    // well-formed allocation as over budget and every release as an unknown
    // handle.
    MemoryArena() = default;

    // Move-only. Two copies of one arena would hand out the same ranges.
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;
    MemoryArena(MemoryArena&&) = default;
    MemoryArena& operator=(MemoryArena&&) = default;

    // (Re)initialises the arena to an empty range. Every handle issued
    // before is refused afterwards, as stale or as unknown, because slot
    // generations continue from where they were. Fails, leaving the arena as
    // it was, on a configuration outside the documented ranges and when its
    // bookkeeping cannot be allocated; std::bad_alloc is caught.
    bool create(const ArenaConfig& config, std::string& error);

    // Places `size` bytes at the lowest address that is a multiple of
    // `alignment` and fits, and returns ArenaRefusal::none. Anything else is
    // a refusal, and then neither the arena nor `out` has changed.
    //
    // The checks run in this order, so a request that breaks several rules
    // is refused for the first: a zero size, a bad alignment, the allocation
    // limit, the free bytes, and then the search.
    ArenaRefusal allocate(std::uint64_t size, std::uint64_t alignment,
                          ArenaAllocation& out) noexcept;

    // Frees one live allocation and merges its range with its free
    // neighbours. A handle this arena never issued is unknown_handle; one it
    // issued whose allocation is gone is stale_handle, and the arena is
    // unchanged by either.
    ArenaRefusal release(const ArenaHandle& handle) noexcept;

    // Releases every live allocation at once, starts a new high-water
    // period, and makes every handle issued so far stale.
    void reset() noexcept;

    ArenaStats stats() const noexcept;
    const ArenaConfig& config() const noexcept { return config_; }

    // The free ranges, absolute and ascending: the fragmentation figures in
    // full. Apart from create(), it is the only member that allocates, and
    // the only one that may throw std::bad_alloc.
    std::vector<ArenaRange> free_ranges() const;

private:
    struct Block {
        std::uint64_t offset = 0;  // relative to base
        std::uint64_t size = 0;
    };

    struct Slot {
        std::uint64_t offset = 0;  // relative to base
        std::uint64_t size = 0;
        std::uint64_t generation = 0;
        bool live = false;
    };

    void refuse(ArenaRefusal reason) noexcept;
    std::uint64_t current_extent() const noexcept;

    ArenaConfig config_{};
    // The generation a new slot starts at. create() raises it past every
    // generation the old slots reached, so no handle issued before a
    // create() matches a slot made after it.
    std::uint64_t generation_floor_ = 0;
    // Ascending, never empty ranges, and never two that touch: a release
    // merges before it returns.
    std::vector<Block> free_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_slots_;
    std::uint64_t used_ = 0;
    std::uint64_t live_ = 0;
    std::uint64_t high_water_bytes_ = 0;
    std::uint64_t high_water_extent_ = 0;
    std::uint64_t period_high_water_bytes_ = 0;
    std::uint64_t period_high_water_extent_ = 0;
    std::uint64_t allocations_ = 0;
    std::uint64_t releases_ = 0;
    std::uint64_t resets_ = 0;
    std::array<std::uint64_t, kArenaRefusalCount> refused_{};
};

// ---------------------------------------------------------------------------
// The four arenas together
// ---------------------------------------------------------------------------

// How big each category's arena is. Indexed by MemoryCategory.
//
// This type carries numbers; it does not say where they come from. The
// numbers the Switch build starts from, and which of them are derived and
// which are placeholders, are in include/nfsu/memory_budget.h and
// docs/MEMORY_BUDGETS.md.
struct MemoryBudget {
    std::array<std::uint64_t, kMemoryCategoryCount> capacity{};
    std::array<std::uint64_t, kMemoryCategoryCount> max_alignment{16, 16, 16, 16};
    std::array<std::uint32_t, kMemoryCategoryCount> max_allocations{4096, 4096, 4096, 4096};
};

// The four arenas of one budget, laid out.
//
// The three CPU arenas share one address space, in category order:
// persistent from 0, then streaming, then transient, each base rounded up to
// that arena's max_alignment. cpu_bytes() is therefore the size of the one
// CPU block the platform layer reserves, and the end of each CPU arena is
// the base of the next, or the padding before it. The GPU arena has its own
// address space and starts at 0, because it is backed by device memory that
// shares no addresses with the CPU block.
class MemoryPlan {
public:
    MemoryPlan() = default;

    // Fails, leaving the plan as it was, when any arena's configuration is
    // refused or the CPU layout would pass 2^64 - 1.
    bool create(const MemoryBudget& budget, std::string& error);

    MemoryArena& arena(MemoryCategory category) noexcept;
    const MemoryArena& arena(MemoryCategory category) const noexcept;

    std::uint64_t cpu_bytes() const noexcept { return cpu_bytes_; }
    std::uint64_t gpu_bytes() const noexcept { return gpu_bytes_; }

    // One entry per category, in MemoryCategory order.
    std::array<ArenaStats, kMemoryCategoryCount> stats() const noexcept;

private:
    std::array<MemoryArena, kMemoryCategoryCount> arenas_{};
    std::uint64_t cpu_bytes_ = 0;
    std::uint64_t gpu_bytes_ = 0;
};

} // namespace nfsu
