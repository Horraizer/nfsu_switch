#include "nfsu/memory_arena.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace {

// Every call of the replaceable global allocation function this program
// makes. test_arena_calls_never_allocate reads it around arena calls.
std::size_t g_heap_allocations = 0;

} // namespace

// The replacements below are kept out of line. Inlined into a delete
// expression, GCC 13 at -O3 sees std::free applied to a pointer from
// operator new and reports the pair as mismatched, which it is not: this
// operator new is std::malloc.
#if defined(__GNUC__)
#define NFSU_TEST_OUT_OF_LINE __attribute__((noinline))
#else
#define NFSU_TEST_OUT_OF_LINE
#endif

NFSU_TEST_OUT_OF_LINE void* operator new(std::size_t size) {
    ++g_heap_allocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

NFSU_TEST_OUT_OF_LINE void operator delete(void* memory) noexcept {
    std::free(memory);
}

NFSU_TEST_OUT_OF_LINE void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

// Every test below names the defect it exists to catch. An allocator test
// that allocates and never looks at the offset it got back, or that checks a
// figure is merely non-negative, passes against a broken arena, so every
// assertion here is an exact value: an offset, a byte count, a reason.
//
// Most of them compare the arena with an independent model rather than with
// itself. The model knows only the live allocations the arena returned; it
// derives the free ranges as the gaps between them, and it computes the
// first-fit placement of every request on its own. The two can agree only if
// every byte the arena reports free is really outside every live range, and
// every free range is as large as it can be.

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void check_eq(std::uint64_t got, std::uint64_t want, const std::string& what) {
    check(got == want, what + ": got " + std::to_string(got) + ", want " +
                           std::to_string(want));
}

void check_refusal(nfsu::ArenaRefusal got, nfsu::ArenaRefusal want, const std::string& what) {
    check(got == want, what + ": got " + nfsu::arena_refusal_name(got) + ", want " +
                           nfsu::arena_refusal_name(want));
}

std::size_t at(nfsu::ArenaRefusal refusal) {
    return static_cast<std::size_t>(refusal);
}

// SplitMix64. The standard distributions are not the same on every standard
// library, so a sequence drawn through them would not be the same sequence on
// MinGW and on Linux; this is.
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    std::uint64_t below(std::uint64_t bound) { return next() % bound; }

private:
    std::uint64_t state_;
};

template <typename T>
void shuffle(std::vector<T>& values, Random& random) {
    for (std::size_t i = values.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(random.below(i));
        std::swap(values[i - 1], values[j]);
    }
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1U) / alignment * alignment;
}

nfsu::MemoryArena make_arena(std::uint64_t base, std::uint64_t capacity,
                             std::uint64_t max_alignment,
                             std::uint32_t max_allocations = 4096,
                             nfsu::MemoryCategory category =
                                 nfsu::MemoryCategory::streaming) {
    nfsu::ArenaConfig config;
    config.category = category;
    config.base = base;
    config.capacity = capacity;
    config.max_alignment = max_alignment;
    config.max_allocations = max_allocations;
    nfsu::MemoryArena arena;
    std::string error;
    const bool mem_ok_1 = arena.create(config, error);
    check(mem_ok_1, "arena create: " + error);
    return arena;
}

// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------

struct Model {
    std::uint64_t base = 0;
    std::uint64_t capacity = 0;
    // Absolute offset -> size, of every allocation the arena returned and
    // that has not been released.
    std::map<std::uint64_t, std::uint64_t> live;

    std::uint64_t used() const {
        std::uint64_t total = 0;
        for (const auto& entry : live) {
            total += entry.second;
        }
        return total;
    }

    std::vector<nfsu::ArenaRange> gaps() const {
        std::vector<nfsu::ArenaRange> out;
        std::uint64_t cursor = base;
        for (const auto& entry : live) {
            if (entry.first > cursor) {
                out.push_back(nfsu::ArenaRange{cursor, entry.first - cursor});
            }
            cursor = entry.first + entry.second;
        }
        if (cursor < base + capacity) {
            out.push_back(nfsu::ArenaRange{cursor, base + capacity - cursor});
        }
        return out;
    }

    // The lowest aligned offset of a gap that holds the request.
    bool first_fit(std::uint64_t size, std::uint64_t alignment, std::uint64_t& offset) const {
        for (const nfsu::ArenaRange& gap : gaps()) {
            const std::uint64_t start = align_up(gap.offset, alignment);
            if (start + size <= gap.offset + gap.size) {
                offset = start;
                return true;
            }
        }
        return false;
    }
};

// Records one allocation the arena returned, after checking it is one the
// arena was allowed to return: inside the arena, aligned, of the requested
// size, overlapping nothing live, and exactly where first fit puts it.
void accept(Model& model, const nfsu::ArenaAllocation& allocation, std::uint64_t size,
            std::uint64_t alignment, const std::string& what) {
    std::uint64_t expected = 0;
    const bool fits = model.first_fit(size, alignment, expected);
    check(fits, what + ": the arena placed a request the model says cannot fit");
    check_eq(allocation.offset, expected, what + ": first-fit offset");
    check_eq(allocation.size, size, what + ": size");
    check_eq(allocation.alignment, alignment, what + ": alignment");
    check(allocation.offset % alignment == 0, what + ": offset is not aligned");
    check(allocation.offset >= model.base &&
              allocation.offset + allocation.size <= model.base + model.capacity,
          what + ": placement leaves the arena");
    const auto next = model.live.lower_bound(allocation.offset);
    if (next != model.live.end()) {
        check(allocation.offset + allocation.size <= next->first,
              what + ": placement overlaps the live range after it");
    }
    if (next != model.live.begin()) {
        const auto previous = std::prev(next);
        check(previous->first + previous->second <= allocation.offset,
              what + ": placement overlaps the live range before it");
    }
    model.live[allocation.offset] = allocation.size;
}

// Every figure the arena reports, recomputed from the model.
void compare(const nfsu::MemoryArena& arena, const Model& model, const std::string& what) {
    const nfsu::ArenaStats stats = arena.stats();
    const std::uint64_t used = model.used();
    check_eq(stats.used_bytes, used, what + ": used bytes");
    check_eq(stats.free_bytes, model.capacity - used, what + ": free bytes");
    check_eq(stats.live_allocations, model.live.size(), what + ": live allocations");
    const std::vector<nfsu::ArenaRange> gaps = model.gaps();
    const std::vector<nfsu::ArenaRange> ranges = arena.free_ranges();
    bool same = gaps.size() == ranges.size();
    for (std::size_t i = 0; same && i < gaps.size(); ++i) {
        same = gaps[i].offset == ranges[i].offset && gaps[i].size == ranges[i].size;
    }
    check(same, what + ": free ranges are not the gaps between the live ranges");
    std::uint64_t largest = 0;
    for (const nfsu::ArenaRange& gap : gaps) {
        largest = std::max(largest, gap.size);
    }
    check_eq(stats.free_blocks, gaps.size(), what + ": free blocks");
    check_eq(stats.largest_free_block, largest, what + ": largest free block");
    const std::uint64_t free_bytes = model.capacity - used;
    const std::uint64_t permille =
        free_bytes == 0 ? 0 : (1000U * (free_bytes - largest)) / free_bytes;
    check_eq(stats.fragmentation_permille, permille, what + ": fragmentation");
    const std::uint64_t extent =
        model.live.empty()
            ? 0
            : model.live.rbegin()->first + model.live.rbegin()->second - model.base;
    check_eq(stats.extent, extent, what + ": extent");
}

// Every field, counters and high-water marks included.
bool same_stats(const nfsu::ArenaStats& a, const nfsu::ArenaStats& b) {
    return a.category == b.category && a.base == b.base && a.capacity == b.capacity &&
           a.used_bytes == b.used_bytes && a.free_bytes == b.free_bytes &&
           a.live_allocations == b.live_allocations && a.free_blocks == b.free_blocks &&
           a.largest_free_block == b.largest_free_block &&
           a.fragmentation_permille == b.fragmentation_permille && a.extent == b.extent &&
           a.high_water_bytes == b.high_water_bytes &&
           a.high_water_extent == b.high_water_extent &&
           a.period_high_water_bytes == b.period_high_water_bytes &&
           a.period_high_water_extent == b.period_high_water_extent &&
           a.allocations == b.allocations && a.releases == b.releases &&
           a.resets == b.resets && a.refusals == b.refusals && a.refused == b.refused;
}

// The fields that describe what is in the arena now, as opposed to the
// counters of what happened to it.
bool same_contents(const nfsu::ArenaStats& a, const nfsu::ArenaStats& b) {
    return a.used_bytes == b.used_bytes && a.free_bytes == b.free_bytes &&
           a.live_allocations == b.live_allocations && a.free_blocks == b.free_blocks &&
           a.largest_free_block == b.largest_free_block &&
           a.fragmentation_permille == b.fragmentation_permille && a.extent == b.extent;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

// Defect: two reasons or two categories reported under one name, or a name
// that drifts from the one docs/MEMORY_BUDGETS.md and the ledger's messages
// print. A copied case label returns the same string twice and a caller can
// no longer tell an over-budget refusal from a fragmented one.
void test_names() {
    const char* categories[] = {"persistent", "streaming", "transient", "gpu"};
    for (std::size_t i = 0; i < nfsu::kMemoryCategoryCount; ++i) {
        check(std::string(nfsu::memory_category_name(
                  static_cast<nfsu::MemoryCategory>(i))) == categories[i],
              std::string("category name ") + categories[i]);
    }
    const char* reasons[] = {"none",           "zero size",        "bad alignment",
                             "over budget",    "no fit",           "allocation limit",
                             "unknown handle", "stale handle"};
    for (std::size_t i = 0; i < nfsu::kArenaRefusalCount; ++i) {
        check(std::string(nfsu::arena_refusal_name(static_cast<nfsu::ArenaRefusal>(i))) ==
                  reasons[i],
              std::string("refusal name ") + reasons[i]);
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Defect: a configuration accepted that the arithmetic cannot honour - a base
// that is not a multiple of max_alignment makes an aligned local offset a
// misaligned address, and a capacity that wraps base + capacity makes every
// bound check meaningless - or a refused create() that clobbers an arena
// already in use, silently dropping its live allocations or zeroing its
// counters. And the other side of every bound: the largest configuration
// that is accepted, including a range that ends exactly at 2^64 - 1, which
// does not wrap and must not be refused as if it did.
void test_create_refuses_bad_configurations() {
    struct Case {
        const char* what;
        std::uint64_t base;
        std::uint64_t capacity;
        std::uint64_t max_alignment;
        std::uint32_t max_allocations;
        const char* message;
    };
    const Case cases[] = {
        {"zero capacity", 0, 0, 16, 16, "capacity 0 is outside"},
        {"capacity above the cap", 0, nfsu::kMaxArenaCapacity + 1U, 16, 16,
         "is outside 1..281474976710656"},
        {"zero alignment", 0, 4096, 0, 16, "max alignment 0 is not a power of two"},
        {"alignment not a power of two", 0, 4096, 24, 16, "max alignment 24"},
        {"alignment above the cap", 0, 4096, nfsu::kMaxArenaAlignment * 2U, 16,
         "max alignment 2097152"},
        {"misaligned base", 8, 4096, 16, 16, "base 8 is not a multiple"},
        {"base plus capacity wraps", 0xFFFFFFFFFFFFF000ULL, 0x2000, 16, 16,
         "passes 2^64 - 1"},
        {"zero allocations", 0, 4096, 16, 0, "max allocations 0"},
        {"allocations above the cap", 0, 4096, 16, nfsu::kMaxArenaAllocations + 1U,
         "max allocations 4194305 is outside 1..4194304"},
    };

    nfsu::MemoryArena arena = make_arena(0, 4096, 16, 16);
    nfsu::ArenaAllocation held;
    check_refusal(arena.allocate(100, 16, held), nfsu::ArenaRefusal::none, "held allocation");
    nfsu::ArenaAllocation refused;
    check_refusal(arena.allocate(0, 16, refused), nfsu::ArenaRefusal::zero_size,
                  "one refusal, so that the counters have something to lose");
    const nfsu::ArenaStats before = arena.stats();

    for (const Case& c : cases) {
        nfsu::ArenaConfig config;
        config.base = c.base;
        config.capacity = c.capacity;
        config.max_alignment = c.max_alignment;
        config.max_allocations = c.max_allocations;
        std::string error;
        check(!arena.create(config, error), std::string("create accepted ") + c.what);
        const bool mem_ok_2 = error.find(c.message) != std::string::npos;
        check(mem_ok_2, std::string(c.what) + ": message '" + error + "' lacks '" + c.message + "'");
        check(same_stats(arena.stats(), before),
              std::string(c.what) + ": a refused create changed the arena");
    }
    // The allocation made before every refused create() is still the arena's.
    check_refusal(arena.release(held.handle), nfsu::ArenaRefusal::none,
                  "the held allocation survives refused creates");

    // The largest configuration that is accepted, at every bound at once.
    nfsu::ArenaConfig largest;
    largest.base = nfsu::kMaxArenaAlignment;
    largest.capacity = nfsu::kMaxArenaCapacity;
    largest.max_alignment = nfsu::kMaxArenaAlignment;
    largest.max_allocations = 1;
    std::string error;
    const bool mem_ok_3 = arena.create(largest, error);
    check(mem_ok_3, "the largest configuration: " + error);
    nfsu::ArenaAllocation whole;
    check_refusal(arena.allocate(nfsu::kMaxArenaCapacity, nfsu::kMaxArenaAlignment, whole),
                  nfsu::ArenaRefusal::none, "the whole largest arena");
    check_eq(whole.offset, nfsu::kMaxArenaAlignment, "the whole largest arena starts at base");

    // A range that ends exactly at 2^64 - 1 does not wrap.
    nfsu::ArenaConfig top;
    top.base = 0xFFFFFFFFFFFFF000ULL;
    top.capacity = 0xFFF;
    top.max_alignment = 16;
    top.max_allocations = 1;
    const bool mem_ok_4 = arena.create(top, error);
    check(mem_ok_4, "a range ending at 2^64 - 1: " + error);
    nfsu::ArenaAllocation last;
    check_refusal(arena.allocate(0xFFF, 1, last), nfsu::ArenaRefusal::none,
                  "the whole range ending at 2^64 - 1");
    check_eq(last.offset, 0xFFFFFFFFFFFFF000ULL, "it starts at its base");
}

// Defect: an arena nobody created handing out memory. Its capacity is 0, so
// a well-formed request must be over budget, not placed at offset 0.
void test_default_arena_refuses() {
    nfsu::MemoryArena arena;
    nfsu::ArenaAllocation out;
    check_refusal(arena.allocate(1, 1, out), nfsu::ArenaRefusal::over_budget,
                  "a default arena allocates nothing");
    check_refusal(arena.release(nfsu::ArenaHandle{}), nfsu::ArenaRefusal::unknown_handle,
                  "a default arena releases nothing");
    arena.reset();
    check_eq(arena.stats().free_blocks, 0, "a default arena has no free range after reset");
}

// ---------------------------------------------------------------------------
// Refusal before overflow
// ---------------------------------------------------------------------------

// Defect: an off-by-one in the budget comparison. The arena sits at 8192 with
// 4096 bytes, so byte 12288 is the first byte of whatever comes next; a `>=`
// where a `>` belongs refuses the exact fit, and the other way round places
// the largest request one byte into the neighbour.
void test_exact_fit_at_the_boundary() {
    nfsu::MemoryArena arena = make_arena(8192, 4096, 16);
    nfsu::ArenaAllocation whole;
    check_refusal(arena.allocate(4096, 1, whole), nfsu::ArenaRefusal::none,
                  "the largest allocation that fits");
    check_eq(whole.offset, 8192, "the exact fit starts at base");
    check_eq(whole.offset + whole.size, 12288, "the exact fit ends at base + capacity");

    const nfsu::ArenaStats full = arena.stats();
    check_eq(full.free_blocks, 0, "a full arena has no free range");
    check_eq(full.extent, 4096, "a full arena reaches its end");
    nfsu::ArenaAllocation more;
    more.offset = 777;
    check_refusal(arena.allocate(1, 1, more), nfsu::ArenaRefusal::over_budget,
                  "one byte more than a full arena holds");
    check_eq(more.offset, 777, "a refusal leaves its output untouched");
    check(same_contents(arena.stats(), full), "a refusal changed a full arena");

    check_refusal(arena.release(whole.handle), nfsu::ArenaRefusal::none, "release the fit");
    check_refusal(arena.allocate(4097, 1, more), nfsu::ArenaRefusal::over_budget,
                  "the smallest allocation that does not fit");
    check_eq(arena.stats().used_bytes, 0, "the refused 4097 took nothing");
    check_refusal(arena.allocate(4096, 1, more), nfsu::ArenaRefusal::none,
                  "the exact fit again after the refusal");
    check_eq(more.offset, 8192, "the exact fit again starts at base");
}

// Defect: alignment ignored, which returns offset 100 for a 256-aligned
// request; the gap in front of an aligned placement hidden as padding, which
// loses 156 bytes for good; and the boundary of an aligned request off by
// one, which either refuses 3840 bytes that fit exactly or places 3841 that
// run past the end.
void test_exact_fit_with_alignment() {
    nfsu::MemoryArena arena = make_arena(0, 4096, 256);
    Model model{0, 4096, {}};
    nfsu::ArenaAllocation first;
    check_refusal(arena.allocate(100, 1, first), nfsu::ArenaRefusal::none, "100 bytes");
    accept(model, first, 100, 1, "100 bytes");

    nfsu::ArenaAllocation aligned;
    check_refusal(arena.allocate(3841, 256, aligned), nfsu::ArenaRefusal::no_fit,
                  "the smallest 256-aligned request that does not fit");
    check_eq(arena.stats().refused[at(nfsu::ArenaRefusal::no_fit)], 1,
             "the refusal is counted as no fit");
    check_refusal(arena.allocate(3840, 256, aligned), nfsu::ArenaRefusal::none,
                  "the largest 256-aligned request that fits");
    accept(model, aligned, 3840, 256, "3840 bytes at 256");
    check_eq(aligned.offset, 256, "the aligned placement skips to 256");
    check_eq(aligned.offset + aligned.size, 4096, "and ends exactly at the end");
    compare(arena, model, "after the aligned fit");
    check_eq(arena.stats().free_bytes, 156, "the gap in front stays free");

    // The gap is reusable, exactly.
    nfsu::ArenaAllocation gap;
    check_refusal(arena.allocate(157, 1, gap), nfsu::ArenaRefusal::over_budget,
                  "one byte more than the gap");
    check_refusal(arena.allocate(156, 4, gap), nfsu::ArenaRefusal::none, "the whole gap");
    accept(model, gap, 156, 4, "the whole gap");
    check_eq(gap.offset, 100, "the gap is used from its start");
    compare(arena, model, "a full arena of three placements");
}

// Defect: arithmetic that wraps. A request near 2^64 added to what is used,
// or an alignment near 2^64 added to an offset, comes out small and passes a
// naive bound. Each is refused for the right reason, the refusal leaves the
// arena as it was, and the reasons come in the order the header promises.
void test_hostile_requests_are_refused() {
    nfsu::MemoryArena arena = make_arena(0, 4096, 256, 2);
    nfsu::ArenaAllocation held;
    check_refusal(arena.allocate(1000, 16, held), nfsu::ArenaRefusal::none, "held");
    const nfsu::ArenaStats before = arena.stats();

    struct Case {
        const char* what;
        std::uint64_t size;
        std::uint64_t alignment;
        nfsu::ArenaRefusal want;
    };
    const Case cases[] = {
        {"size 2^64 - 1", UINT64_MAX, 1, nfsu::ArenaRefusal::over_budget},
        {"size 2^64 - 1000", UINT64_MAX - 999U, 1, nfsu::ArenaRefusal::over_budget},
        {"free bytes plus one", 3097, 1, nfsu::ArenaRefusal::over_budget},
        {"zero size", 0, 16, nfsu::ArenaRefusal::zero_size},
        {"zero size before a bad alignment", 0, 3, nfsu::ArenaRefusal::zero_size},
        {"zero alignment", 16, 0, nfsu::ArenaRefusal::bad_alignment},
        {"alignment 3", 16, 3, nfsu::ArenaRefusal::bad_alignment},
        {"alignment 2^63", 16, 1ULL << 63U, nfsu::ArenaRefusal::bad_alignment},
        {"alignment above the arena's", 16, 512, nfsu::ArenaRefusal::bad_alignment},
        {"bad alignment before over budget", UINT64_MAX, 3,
         nfsu::ArenaRefusal::bad_alignment},
    };
    std::uint64_t refused = 0;
    for (const Case& c : cases) {
        nfsu::ArenaAllocation out;
        check_refusal(arena.allocate(c.size, c.alignment, out), c.want, c.what);
        ++refused;
        check(same_contents(arena.stats(), before), std::string(c.what) + " changed the arena");
    }
    check_eq(arena.stats().refusals, refused, "every hostile request is counted");

    // The allocation limit comes before the byte count.
    nfsu::ArenaAllocation second;
    check_refusal(arena.allocate(16, 16, second), nfsu::ArenaRefusal::none, "second of two");
    nfsu::ArenaAllocation third;
    check_refusal(arena.allocate(UINT64_MAX, 1, third), nfsu::ArenaRefusal::allocation_limit,
                  "the limit before over budget");
    check_refusal(arena.allocate(1, 1, third), nfsu::ArenaRefusal::allocation_limit,
                  "the limit with bytes to spare");
}

// ---------------------------------------------------------------------------
// Fragmentation diagnostics
// ---------------------------------------------------------------------------

// Defect: fragmentation figures that are wrong while the placements are
// right - the largest block taken from the tail only, free blocks counted
// before merging, the per-mille figure inverted - and a refusal reported for
// the wrong reason. A caller told "over budget" when the truth is "no fit"
// unloads a region it did not need to; told "no fit" when the truth is "over
// budget", it retries a request that can never succeed.
//
// It also pins first fit against best fit: the 100-byte request below has an
// exact-size hole at 500 that best fit would take, and first fit must not.
void test_fragmentation_figures() {
    nfsu::MemoryArena arena = make_arena(0, 1000, 16);
    Model model{0, 1000, {}};
    std::vector<nfsu::ArenaAllocation> blocks(8);
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        check_refusal(arena.allocate(100, 1, blocks[i]), nfsu::ArenaRefusal::none,
                      "block " + std::to_string(i));
        accept(model, blocks[i], 100, 1, "block " + std::to_string(i));
        check_eq(blocks[i].offset, 100U * i, "block " + std::to_string(i) + " offset");
    }
    for (const std::size_t i : {std::size_t{1}, std::size_t{3}, std::size_t{5}}) {
        check_refusal(arena.release(blocks[i].handle), nfsu::ArenaRefusal::none,
                      "release block " + std::to_string(i));
        model.live.erase(blocks[i].offset);
    }
    compare(arena, model, "three holes");
    nfsu::ArenaStats stats = arena.stats();
    check_eq(stats.free_bytes, 500, "three holes: free bytes");
    check_eq(stats.free_blocks, 4, "three holes: free blocks");
    check_eq(stats.largest_free_block, 200, "three holes: largest block");
    check_eq(stats.fragmentation_permille, 600, "three holes: fragmentation");
    check_eq(stats.extent, 800, "three holes: extent");

    nfsu::ArenaAllocation out;
    check_refusal(arena.allocate(250, 1, out), nfsu::ArenaRefusal::no_fit,
                  "250 bytes when 500 are free in blocks of at most 200");
    check_refusal(arena.allocate(501, 1, out), nfsu::ArenaRefusal::over_budget,
                  "501 bytes when 500 are free");

    // Releasing block 2 joins it to the holes on both sides.
    check_refusal(arena.release(blocks[2].handle), nfsu::ArenaRefusal::none, "release block 2");
    model.live.erase(blocks[2].offset);
    compare(arena, model, "joined hole");
    stats = arena.stats();
    check_eq(stats.free_blocks, 3, "joined hole: free blocks");
    check_eq(stats.largest_free_block, 300, "joined hole: largest block");
    check_eq(stats.fragmentation_permille, 500, "joined hole: fragmentation");

    check_refusal(arena.allocate(100, 1, out), nfsu::ArenaRefusal::none, "first fit");
    accept(model, out, 100, 1, "first fit");
    check_eq(out.offset, 100, "first fit takes the lowest hole, not the exact one at 500");
    nfsu::ArenaAllocation wide;
    check_refusal(arena.allocate(200, 1, wide), nfsu::ArenaRefusal::none, "200 bytes");
    accept(model, wide, 200, 1, "200 bytes");
    check_eq(wide.offset, 200, "200 bytes fill the rest of the joined hole");
    compare(arena, model, "after refilling");

    // Every byte free again is one block and no fragmentation at all.
    arena.reset();
    stats = arena.stats();
    check_eq(stats.free_blocks, 1, "reset: one free block");
    check_eq(stats.fragmentation_permille, 0, "reset: no fragmentation");
}

// Defect: a release that merges wrongly in one direction only. Each case
// frees a range whose neighbours are, in turn, both live, the free range
// before it, the free range after it, and both; each needs a different
// branch, and a branch that forgets to move a block's start or to drop the
// block it absorbed leaves the free list describing bytes that are live.
void test_release_merges_in_every_direction() {
    struct Case {
        const char* what;
        std::vector<std::size_t> order;
    };
    const Case cases[] = {
        {"neighbours live", {2}},
        {"merge with the previous", {1, 2}},
        {"merge with the next", {3, 2}},
        {"merge with both", {1, 3, 2}},
        {"merge at the start", {0}},
        {"merge at the end", {4}},
        {"everything, outside in", {0, 4, 1, 3, 2}},
        {"everything, inside out", {2, 1, 3, 0, 4}},
    };
    for (const Case& c : cases) {
        nfsu::MemoryArena arena = make_arena(4096, 500, 16);
        Model model{4096, 500, {}};
        std::vector<nfsu::ArenaAllocation> blocks(5);
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            check_refusal(arena.allocate(100, 4, blocks[i]), nfsu::ArenaRefusal::none,
                          std::string(c.what) + ": allocate");
            accept(model, blocks[i], 100, 4, c.what);
        }
        for (const std::size_t i : c.order) {
            check_refusal(arena.release(blocks[i].handle), nfsu::ArenaRefusal::none,
                          std::string(c.what) + ": release");
            model.live.erase(blocks[i].offset);
            compare(arena, model, std::string(c.what) + " after releasing " + std::to_string(i));
        }
        // A request that fits only in the merged range lands exactly there.
        std::uint64_t expected = 0;
        const std::uint64_t size = arena.stats().largest_free_block;
        if (size != 0 && model.first_fit(size, 1, expected)) {
            nfsu::ArenaAllocation merged;
            check_refusal(arena.allocate(size, 1, merged), nfsu::ArenaRefusal::none,
                          std::string(c.what) + ": the largest free block is usable");
            accept(model, merged, size, 1, std::string(c.what) + ": merged range");
            compare(arena, model, std::string(c.what) + ": after using the merged range");
        }
    }
}

// Defect: anything the targeted tests above did not think of. Twenty
// thousand requests of random size and alignment and releases in random
// order, and after every one of them the model must agree exactly: the
// offset first fit gives, the reason for every refusal, and every figure.
void test_random_operations_against_the_model() {
    const std::uint64_t base = 1ULL << 20;
    const std::uint64_t capacity = 64U * 1024U;
    nfsu::MemoryArena arena = make_arena(base, capacity, 4096, 256);
    Model model{base, capacity, {}};
    std::vector<nfsu::ArenaAllocation> live;
    Random random(0x6E667375ULL);
    const std::uint64_t alignments[] = {1, 2, 4, 8, 16, 32, 256, 4096};
    std::uint64_t placed = 0;
    std::uint64_t refused_budget = 0;
    std::uint64_t refused_fit = 0;
    std::uint64_t refused_limit = 0;
    for (int step = 0; step < 20000; ++step) {
        const std::string what = "random step " + std::to_string(step);
        const bool allocate = live.empty() || random.below(100) < 55;
        if (allocate) {
            // Mostly small, sometimes large, and one in twenty up to the
            // whole arena, so that over budget is reached as well as no fit.
            const std::uint64_t pick = random.below(20);
            const std::uint64_t size =
                1U + random.below(pick == 0 ? capacity : (pick < 5 ? 9000 : 700));
            const std::uint64_t alignment = alignments[random.below(8)];
            nfsu::ArenaAllocation out;
            const nfsu::ArenaRefusal got = arena.allocate(size, alignment, out);
            std::uint64_t expected = 0;
            const bool fits = model.first_fit(size, alignment, expected);
            if (live.size() >= 256) {
                check_refusal(got, nfsu::ArenaRefusal::allocation_limit, what);
                ++refused_limit;
            } else if (size > capacity - model.used()) {
                check_refusal(got, nfsu::ArenaRefusal::over_budget, what);
                ++refused_budget;
            } else if (!fits) {
                check_refusal(got, nfsu::ArenaRefusal::no_fit, what);
                ++refused_fit;
            } else {
                check_refusal(got, nfsu::ArenaRefusal::none, what);
                if (got == nfsu::ArenaRefusal::none) {
                    accept(model, out, size, alignment, what);
                    live.push_back(out);
                    ++placed;
                }
            }
        } else {
            const std::size_t pick = static_cast<std::size_t>(random.below(live.size()));
            check_refusal(arena.release(live[pick].handle), nfsu::ArenaRefusal::none, what);
            model.live.erase(live[pick].offset);
            live[pick] = live.back();
            live.pop_back();
        }
        compare(arena, model, what);
        if (failures > 50) {
            return;
        }
    }
    // A sequence that never refused, or never filled the arena, proves
    // nothing about the refusal paths, so the run must have reached each.
    check(placed > 5000, "the random run placed " + std::to_string(placed));
    check(refused_budget > 100, "the random run hit over budget " +
                                    std::to_string(refused_budget) + " times");
    check(refused_fit > 100, "the random run hit no fit " + std::to_string(refused_fit) +
                                 " times");
    const nfsu::ArenaStats stats = arena.stats();
    check_eq(stats.refused[at(nfsu::ArenaRefusal::over_budget)], refused_budget,
             "over-budget refusals counted");
    check_eq(stats.refused[at(nfsu::ArenaRefusal::no_fit)], refused_fit,
             "no-fit refusals counted");
    check_eq(stats.refused[at(nfsu::ArenaRefusal::allocation_limit)], refused_limit,
             "limit refusals counted");
}

// ---------------------------------------------------------------------------
// Load and unload cycles
// ---------------------------------------------------------------------------

struct Request {
    std::uint64_t size = 0;
    std::uint64_t alignment = 0;
};

std::vector<Request> scene_requests(std::uint64_t seed, std::size_t count) {
    Random random(seed);
    const std::uint64_t alignments[] = {1, 2, 4, 16, 32, 256, 4096};
    std::vector<Request> requests(count);
    for (Request& request : requests) {
        request.size = 1U + random.below(5000);
        request.alignment = alignments[random.below(7)];
    }
    return requests;
}

// Defect: state that leaks from one load and unload cycle into the next - a
// few bytes a merge drops, a free range left split, a slot never reused, a
// placement that creeps upward - which is exactly what a scene loaded and
// unloaded for a whole session exposes and one cycle never does.
//
// 2000 cycles each place the same 40 requests into an empty arena and
// release them in a different order, so every merge direction is exercised
// thousands of times. After every cycle the arena must be exactly as it was
// created: one free block of the whole capacity. Every cycle must place
// every request exactly where the first cycle did, and the high-water marks
// after 2000 cycles must be the first cycle's. max_allocations is exactly
// the scene's 40 and every handle's slot must stay below it: the limit
// counts live allocations, so a slot that is never reused would not be
// refused, only appended, and slot 40 in the second cycle is how it shows.
void test_load_and_unload_cycles_return_to_the_start() {
    const std::vector<Request> requests = scene_requests(0x51A7E, 40);
    nfsu::MemoryArena arena = make_arena(0, 1U << 20, 4096, 40);
    const nfsu::ArenaStats empty = arena.stats();
    std::vector<std::uint64_t> first_offsets;
    std::uint64_t first_high_water = 0;
    std::uint64_t first_high_extent = 0;
    Random random(0xC7C1E5ULL);
    const int cycles = 2000;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const std::string what = "cycle " + std::to_string(cycle);
        std::vector<nfsu::ArenaAllocation> placed(requests.size());
        for (std::size_t i = 0; i < requests.size(); ++i) {
            const nfsu::ArenaRefusal got =
                arena.allocate(requests[i].size, requests[i].alignment, placed[i]);
            check_refusal(got, nfsu::ArenaRefusal::none, what + " request " + std::to_string(i));
            if (got != nfsu::ArenaRefusal::none) {
                return;
            }
            if (placed[i].handle.slot >= 40U) {
                check(false, what + " handed out slot " +
                                 std::to_string(placed[i].handle.slot) + " of 40");
                return;
            }
            if (cycle == 0) {
                first_offsets.push_back(placed[i].offset);
            } else if (placed[i].offset != first_offsets[i]) {
                check(false, what + " placed request " + std::to_string(i) + " at " +
                                 std::to_string(placed[i].offset) + ", cycle 0 at " +
                                 std::to_string(first_offsets[i]));
                return;
            }
        }
        std::vector<std::size_t> order(requests.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        shuffle(order, random);
        for (const std::size_t i : order) {
            check_refusal(arena.release(placed[i].handle), nfsu::ArenaRefusal::none,
                          what + " release " + std::to_string(i));
        }
        const nfsu::ArenaStats stats = arena.stats();
        if (!same_contents(stats, empty)) {
            check(false, what + " did not return to the empty arena: used " +
                             std::to_string(stats.used_bytes) + ", free blocks " +
                             std::to_string(stats.free_blocks) + ", largest " +
                             std::to_string(stats.largest_free_block));
            return;
        }
        if (cycle == 0) {
            first_high_water = stats.high_water_bytes;
            first_high_extent = stats.high_water_extent;
        }
    }
    const nfsu::ArenaStats stats = arena.stats();
    check_eq(stats.high_water_bytes, first_high_water, "high water after 2000 cycles");
    check_eq(stats.high_water_extent, first_high_extent, "high-water extent after 2000 cycles");
    check_eq(stats.allocations, 40U * cycles, "allocations over every cycle");
    check_eq(stats.releases, 40U * cycles, "releases over every cycle");
    check_eq(stats.refusals, 0, "no refusal over every cycle");
    std::uint64_t total = 0;
    for (const Request& request : requests) {
        total += request.size;
    }
    check_eq(first_high_water, total, "the peak is the whole scene");
    check(first_high_extent > total, "alignment gaps make the extent exceed the bytes");
}

// Defect: the same leak, where only interleaving exposes it. Streaming never
// empties the arena: region B is loaded while A is resident, A is unloaded
// while B is, and C lands in the hole A left. Each cycle loads and unloads
// three regions that way and ends empty.
//
// The release order inside each region follows one of four fixed
// permutations, chosen by cycle number. Every cycle therefore starts from the
// empty arena and repeats a sequence an earlier cycle ran exactly, so once
// all four have been seen the lifetime high-water marks must never move
// again. A mark that moves after that is creep, by construction; before it,
// different orders may peak differently and nothing is asserted.
void test_interleaved_streaming_cycles_do_not_creep() {
    const std::vector<Request> a = scene_requests(0xA, 12);
    const std::vector<Request> b = scene_requests(0xB, 9);
    const std::vector<Request> c = scene_requests(0xC, 15);
    nfsu::MemoryArena arena = make_arena(0, 1U << 20, 4096, 64);
    const nfsu::ArenaStats empty = arena.stats();

    std::vector<std::vector<std::size_t>> orders;
    Random random(0x5EED);
    for (int k = 0; k < 4; ++k) {
        std::vector<std::size_t> order(15);
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        shuffle(order, random);
        orders.push_back(order);
    }

    const auto load = [&arena](const std::vector<Request>& region,
                               std::vector<nfsu::ArenaAllocation>& out) {
        out.assign(region.size(), nfsu::ArenaAllocation{});
        for (std::size_t i = 0; i < region.size(); ++i) {
            if (arena.allocate(region[i].size, region[i].alignment, out[i]) !=
                nfsu::ArenaRefusal::none) {
                return false;
            }
        }
        return true;
    };
    const auto unload = [&arena](const std::vector<nfsu::ArenaAllocation>& region,
                                 const std::vector<std::size_t>& order) {
        for (const std::size_t i : order) {
            if (i < region.size() &&
                arena.release(region[i].handle) != nfsu::ArenaRefusal::none) {
                return false;
            }
        }
        return true;
    };

    std::uint64_t settled_bytes = 0;
    std::uint64_t settled_extent = 0;
    for (int cycle = 0; cycle < 1200; ++cycle) {
        const std::vector<std::size_t>& order = orders[static_cast<std::size_t>(cycle % 4)];
        std::vector<nfsu::ArenaAllocation> ra;
        std::vector<nfsu::ArenaAllocation> rb;
        std::vector<nfsu::ArenaAllocation> rc;
        const bool ok = load(a, ra) && load(b, rb) && unload(ra, order) && load(c, rc) &&
                        unload(rb, order) && load(a, ra) && unload(rc, order) &&
                        unload(ra, order);
        check(ok, "streaming cycle " + std::to_string(cycle) + " was refused");
        if (!ok || !same_contents(arena.stats(), empty)) {
            check(false, "streaming cycle " + std::to_string(cycle) +
                             " did not end with the empty arena");
            return;
        }
        const nfsu::ArenaStats stats = arena.stats();
        if (cycle == 3) {
            settled_bytes = stats.high_water_bytes;
            settled_extent = stats.high_water_extent;
        } else if (cycle > 3 && (stats.high_water_bytes != settled_bytes ||
                                 stats.high_water_extent != settled_extent)) {
            check(false, "streaming cycle " + std::to_string(cycle) +
                             " moved the high-water marks to " +
                             std::to_string(stats.high_water_bytes) + " and " +
                             std::to_string(stats.high_water_extent));
            return;
        }
    }
    check(settled_extent > settled_bytes, "hole reuse is exercised: extent above bytes");
}

// Defect: figures that outlive the arena they describe. The header says
// every counter and every high-water mark counts "since create()", so a
// second create() must start all of them over; an arena re-created for a new
// session that kept the old peak, the old refusals or the old reset count
// would report the previous session's worst moment as its own.
void test_create_again_starts_every_figure_over() {
    nfsu::MemoryArena arena = make_arena(0, 4096, 256, 8);
    nfsu::ArenaAllocation first;
    nfsu::ArenaAllocation second;
    check_refusal(arena.allocate(100, 1, first), nfsu::ArenaRefusal::none, "first");
    check_refusal(arena.allocate(1, 256, second), nfsu::ArenaRefusal::none, "second");
    check_refusal(arena.release(first.handle), nfsu::ArenaRefusal::none, "release first");
    nfsu::ArenaAllocation none;
    check_refusal(arena.allocate(0, 1, none), nfsu::ArenaRefusal::zero_size, "a refusal");
    arena.reset();
    check_refusal(arena.allocate(50, 1, first), nfsu::ArenaRefusal::none, "after the reset");
    const nfsu::ArenaStats busy = arena.stats();
    check(busy.high_water_extent == 257 && busy.resets == 1 && busy.refusals == 1 &&
              busy.allocations == 3 && busy.releases == 1,
          "the figures are all nonzero before create()");
    std::string error;
    const bool mem_ok_5 = arena.create(arena.config(), error);
    check(mem_ok_5, "create again: " + error);
    const nfsu::ArenaStats fresh = arena.stats();
    const nfsu::ArenaStats empty = make_arena(0, 4096, 256, 8).stats();
    check(same_stats(fresh, empty), "create() again starts every figure over");
    check_eq(fresh.high_water_bytes, 0, "create again: high water");
    check_eq(fresh.high_water_extent, 0, "create again: high-water extent");
    check_eq(fresh.period_high_water_extent, 0, "create again: period extent");
    check_eq(fresh.allocations, 0, "create again: allocations");
    check_eq(fresh.releases, 0, "create again: releases");
    check_eq(fresh.resets, 0, "create again: resets");
    check_eq(fresh.refusals, 0, "create again: refusals");
    check_eq(fresh.refused[at(nfsu::ArenaRefusal::zero_size)], 0, "create again: by reason");
}

// Defect: a high-water mark that is not a peak. Each mark must be the most
// the arena ever reached, not the latest value: after the furthest
// allocation is released and a smaller one placed, the extent falls and the
// mark must not. The period extent is its own figure, not the period's
// bytes - one byte at 0 and one byte at 256 reach 257 with 2 bytes used -
// and reset() must start it over with the bytes.
void test_high_water_marks_are_peaks() {
    nfsu::MemoryArena arena = make_arena(0, 4096, 256, 8);
    nfsu::ArenaAllocation low;
    nfsu::ArenaAllocation high;
    nfsu::ArenaAllocation small;
    check_refusal(arena.allocate(1000, 1, low), nfsu::ArenaRefusal::none, "low");
    check_refusal(arena.allocate(1000, 1, high), nfsu::ArenaRefusal::none, "high");
    check_refusal(arena.release(high.handle), nfsu::ArenaRefusal::none, "release high");
    check_refusal(arena.allocate(10, 1, small), nfsu::ArenaRefusal::none, "small");
    nfsu::ArenaStats stats = arena.stats();
    check_eq(stats.extent, 1010, "the extent falls back");
    check_eq(stats.used_bytes, 1010, "the bytes fall back");
    check_eq(stats.high_water_extent, 2000, "the extent mark stays at the peak");
    check_eq(stats.high_water_bytes, 2000, "the byte mark stays at the peak");
    check_eq(stats.period_high_water_extent, 2000, "the period extent mark before a reset");

    arena.reset();
    stats = arena.stats();
    check_eq(stats.period_high_water_extent, 0, "reset starts the period extent over");
    check_eq(stats.period_high_water_bytes, 0, "reset starts the period bytes over");
    check_refusal(arena.allocate(1, 1, low), nfsu::ArenaRefusal::none, "one byte at 0");
    check_refusal(arena.allocate(1, 256, high), nfsu::ArenaRefusal::none, "one byte at 256");
    stats = arena.stats();
    check_eq(stats.period_high_water_extent, 257, "the period extent is an extent");
    check_eq(stats.period_high_water_bytes, 2, "the period bytes are bytes");
    check_eq(stats.high_water_extent, 2000, "the lifetime extent mark survives the reset");
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

// Defect: a frame that does not end cleanly. reset() must free every byte,
// start a new period so each frame's peak is its own, keep the lifetime peak,
// and make every handle of the frame stale; a release through an old handle
// after reset() would otherwise free a range the next frame now holds. A
// frame that asks for more than the budget is refused and the next frame is
// unaffected, which is the frame budget failing gracefully.
void test_frames_reset_the_transient_arena() {
    nfsu::MemoryArena arena = make_arena(0, 64U * 1024U, 16, 1024,
                                         nfsu::MemoryCategory::transient);
    Random random(0xF4A3E);
    std::uint64_t lifetime_peak = 0;
    std::vector<nfsu::ArenaAllocation> previous;
    for (int frame = 0; frame < 1000; ++frame) {
        const std::string what = "frame " + std::to_string(frame);
        std::vector<nfsu::ArenaAllocation> held;
        std::uint64_t frame_peak = 0;
        const std::uint64_t count = 1U + random.below(20);
        for (std::uint64_t k = 0; k < count; ++k) {
            nfsu::ArenaAllocation out;
            const std::uint64_t size = 1U + random.below(3000);
            if (arena.allocate(size, 16, out) == nfsu::ArenaRefusal::none) {
                held.push_back(out);
                frame_peak += size;
            }
        }
        // The first live range of the new frame starts where the previous
        // frame's first one did, and the previous frame's handle is stale.
        if (!previous.empty() && !held.empty()) {
            check_eq(held.front().offset, 0, what + ": the frame starts at the base");
            check_refusal(arena.release(previous.front().handle),
                          nfsu::ArenaRefusal::stale_handle, what + ": old handle");
            check_eq(arena.stats().live_allocations, held.size(),
                     what + ": the stale release freed nothing");
        }
        if (frame % 100 == 99) {
            // A frame over its budget: refused, and nothing else moves.
            nfsu::ArenaAllocation huge;
            const nfsu::ArenaStats before = arena.stats();
            check_refusal(arena.allocate(64U * 1024U + 1U, 16, huge),
                          nfsu::ArenaRefusal::over_budget, what + ": over the frame budget");
            check(same_contents(arena.stats(), before), what + ": the refusal changed the frame");
        }
        const nfsu::ArenaStats end = arena.stats();
        check_eq(end.period_high_water_bytes, frame_peak, what + ": the frame's own peak");
        lifetime_peak = std::max(lifetime_peak, frame_peak);
        check_eq(end.high_water_bytes, lifetime_peak, what + ": the lifetime peak");
        arena.reset();
        const nfsu::ArenaStats after = arena.stats();
        check_eq(after.used_bytes, 0, what + ": reset frees everything");
        check_eq(after.free_blocks, 1, what + ": reset leaves one block");
        check_eq(after.period_high_water_bytes, 0, what + ": reset starts a period");
        check_eq(after.period_high_water_extent, 0, what + ": and its extent");
        check_eq(after.high_water_bytes, lifetime_peak, what + ": reset keeps the lifetime peak");
        check_eq(after.resets, static_cast<std::uint64_t>(frame) + 1U, what + ": resets");
        previous = held;
        if (failures > 50) {
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Handles and limits
// ---------------------------------------------------------------------------

// Defect: a release that trusts its handle. A double release, a handle whose
// slot has been reused, and a handle that was never issued - including one at
// the current generation of a slot that is free - must each be refused
// without touching anything. Without the generation check a reused slot's
// old handle frees the allocation that now owns the slot, and the next
// request is placed on top of live data; without the free-slot check the
// same range is freed twice and the used byte count wraps below zero.
void test_handles_are_checked() {
    nfsu::MemoryArena arena = make_arena(0, 4096, 16, 4);
    Model model{0, 4096, {}};
    nfsu::ArenaAllocation first;
    check_refusal(arena.allocate(100, 16, first), nfsu::ArenaRefusal::none, "first");
    accept(model, first, 100, 16, "first");
    check_refusal(arena.release(first.handle), nfsu::ArenaRefusal::none, "release first");
    model.live.erase(first.offset);
    check_refusal(arena.release(first.handle), nfsu::ArenaRefusal::stale_handle,
                  "double release");
    // The released slot's own current generation was never issued: accepting
    // it would free the same range twice and wrap the byte count.
    nfsu::ArenaHandle unissued = first.handle;
    unissued.generation += 1U;
    check_refusal(arena.release(unissued), nfsu::ArenaRefusal::unknown_handle,
                  "the current generation of a free slot");
    compare(arena, model, "after the free slot's forged handle");

    nfsu::ArenaAllocation reuse;
    check_refusal(arena.allocate(200, 16, reuse), nfsu::ArenaRefusal::none, "reuse");
    accept(model, reuse, 200, 16, "reuse");
    check_eq(reuse.handle.slot, first.handle.slot, "the freed slot is reused");
    check(reuse.handle.generation != first.handle.generation,
          "a reused slot carries a new generation");
    check_refusal(arena.release(first.handle), nfsu::ArenaRefusal::stale_handle,
                  "release through the reused slot's old handle");
    compare(arena, model, "the reused slot's allocation survives its old handle");

    nfsu::ArenaHandle forged = reuse.handle;
    forged.generation += 1U;
    check_refusal(arena.release(forged), nfsu::ArenaRefusal::unknown_handle,
                  "a generation never issued");
    forged = reuse.handle;
    forged.slot = 3;
    check_refusal(arena.release(forged), nfsu::ArenaRefusal::unknown_handle,
                  "a slot never used");
    check_refusal(arena.release(nfsu::ArenaHandle{}), nfsu::ArenaRefusal::unknown_handle,
                  "a default handle");
    compare(arena, model, "after the refused releases");
    const nfsu::ArenaStats stats = arena.stats();
    check_eq(stats.refused[at(nfsu::ArenaRefusal::stale_handle)], 2, "stale refusals");
    check_eq(stats.refused[at(nfsu::ArenaRefusal::unknown_handle)], 4, "unknown refusals");
    check_refusal(arena.release(reuse.handle), nfsu::ArenaRefusal::none, "release reuse");
}

// Defect: a handle that outlives the arena it came from. create() starts an
// arena over, and slots made afterwards reuse the old slot numbers; if their
// generations started over too, a handle issued before create() would name
// an allocation made after it, and releasing through it would free memory it
// never owned.
void test_create_again_keeps_old_handles_stale() {
    static_assert(!std::is_copy_constructible<nfsu::MemoryArena>::value &&
                      !std::is_copy_assignable<nfsu::MemoryArena>::value,
                  "two copies of one arena would hand out the same ranges");
    nfsu::MemoryArena arena = make_arena(0, 4096, 16, 4);
    nfsu::ArenaAllocation before;
    check_refusal(arena.allocate(64, 16, before), nfsu::ArenaRefusal::none, "before");
    std::string error;
    const bool mem_ok_6 = arena.create(arena.config(), error);
    check(mem_ok_6, "create again: " + error);
    nfsu::ArenaAllocation after;
    check_refusal(arena.allocate(64, 16, after), nfsu::ArenaRefusal::none, "after");
    check_eq(after.handle.slot, before.handle.slot, "the slot number is reused");
    check(after.handle.generation > before.handle.generation,
          "under a generation no earlier handle has");
    check_refusal(arena.release(before.handle), nfsu::ArenaRefusal::stale_handle,
                  "a handle from before create()");
    check_eq(arena.stats().live_allocations, 1, "the allocation made after is untouched");
    check_refusal(arena.release(after.handle), nfsu::ArenaRefusal::none, "release after");
}

// Defect: the allocation limit off by one or not enforced. Past it the
// arena's bookkeeping would have to grow inside a noexcept call, which is
// the one way it could fail after create().
void test_allocation_limit() {
    nfsu::MemoryArena arena = make_arena(0, 4096, 16, 3);
    std::vector<nfsu::ArenaAllocation> held(3);
    for (std::size_t i = 0; i < held.size(); ++i) {
        check_refusal(arena.allocate(10, 16, held[i]), nfsu::ArenaRefusal::none,
                      "allocation " + std::to_string(i) + " of 3");
    }
    nfsu::ArenaAllocation fourth;
    check_refusal(arena.allocate(10, 16, fourth), nfsu::ArenaRefusal::allocation_limit,
                  "the fourth of three");
    check_refusal(arena.release(held[1].handle), nfsu::ArenaRefusal::none, "free one");
    check_refusal(arena.allocate(10, 16, fourth), nfsu::ArenaRefusal::none,
                  "the limit counts live allocations only");
    check_eq(fourth.offset, 16, "the freed range is reused");
}

// Defect: an arena call that allocates. create() reserves everything
// allocate(), release() and reset() can need, and those three are noexcept,
// so a single heap allocation inside one of them turns memory pressure into
// std::terminate instead of a refusal. This counts every call of the global
// allocation function made inside those calls and requires none, over three
// kinds of state:
//
//   * one more free block than live allocations, one below the limit;
//   * the only state that needs the whole reservation: a two-sided split at
//     the allocation limit, which leaves max_allocations + 1 free blocks. It
//     is the one insert the "+ 1" in create() exists for, and with the
//     reservation one block short it is where allocate() would reallocate;
//   * thousands of random calls, with a reset every thousand.
void test_arena_calls_never_allocate() {
    const std::uint32_t limit = 64;
    nfsu::MemoryArena arena = make_arena(0, 4096, 64, limit);
    std::vector<nfsu::ArenaAllocation> live;
    live.reserve(limit);
    std::size_t inside = 0;
    const auto allocate = [&](std::uint64_t size, std::uint64_t alignment) {
        nfsu::ArenaAllocation out;
        const std::size_t before = g_heap_allocations;
        const nfsu::ArenaRefusal got = arena.allocate(size, alignment, out);
        inside += g_heap_allocations - before;
        if (got == nfsu::ArenaRefusal::none) {
            live.push_back(out);
        }
        return got;
    };
    const auto release = [&](std::size_t index) {
        const std::size_t before = g_heap_allocations;
        const nfsu::ArenaRefusal got = arena.release(live[index].handle);
        inside += g_heap_allocations - before;
        live[index] = live.back();
        live.pop_back();
        return got;
    };

    // A one-byte filler at 0, then 63 blocks of 32 bytes on 64-byte
    // boundaries, each followed by a gap; freeing the filler leaves a free
    // range in front of every live one and one behind the last.
    check_refusal(allocate(1, 1), nfsu::ArenaRefusal::none, "the filler");
    for (std::uint32_t i = 1; i < limit; ++i) {
        check_refusal(allocate(32, 64), nfsu::ArenaRefusal::none, "a spaced block");
    }
    check_refusal(allocate(1, 1), nfsu::ArenaRefusal::allocation_limit, "past the limit");
    check_refusal(release(0), nfsu::ArenaRefusal::none, "free the filler");
    const nfsu::ArenaStats spaced = arena.stats();
    check_eq(spaced.live_allocations, limit - 1U, "spaced shape: live");
    check_eq(spaced.free_blocks, limit, "spaced shape: one more free block than live");
    // Back to the limit through the tail-only branch: the one-byte request
    // lands at the start of the leading block and replaces it in place, so
    // the free list does not grow here.
    check_refusal(allocate(1, 1), nfsu::ArenaRefusal::none, "back to the limit");
    check_eq(arena.stats().free_blocks, limit, "a tail-only placement at the limit");

    // The two-sided split at the limit, in an arena of three: free [0,4),
    // live [4,5), free [5,64), live [64,65), free [65,128). Ten bytes at
    // alignment 8 skip the four-byte leading block and land at 8 inside
    // [5,64), leaving a 3-byte gap in front and a 46-byte tail behind: four
    // free blocks, max_allocations + 1, with three allocations live.
    {
        nfsu::MemoryArena split = make_arena(0, 128, 64, 3);
        std::size_t split_inside = 0;
        const auto place = [&](std::uint64_t size, std::uint64_t alignment,
                               nfsu::ArenaAllocation& out) {
            const std::size_t before = g_heap_allocations;
            const nfsu::ArenaRefusal got = split.allocate(size, alignment, out);
            split_inside += g_heap_allocations - before;
            return got;
        };
        nfsu::ArenaAllocation leading;
        nfsu::ArenaAllocation left;
        nfsu::ArenaAllocation right;
        nfsu::ArenaAllocation middle;
        check_refusal(place(4, 1, leading), nfsu::ArenaRefusal::none, "split: leading");
        check_refusal(place(1, 1, left), nfsu::ArenaRefusal::none, "split: left");
        check_refusal(place(1, 64, right), nfsu::ArenaRefusal::none, "split: right");
        check_eq(right.offset, 64, "split: the right block is on its boundary");
        // Only the arena call is inside the window: a check builds its
        // message whether or not it fails, and that allocates.
        const std::size_t before = g_heap_allocations;
        const nfsu::ArenaRefusal freed = split.release(leading.handle);
        split_inside += g_heap_allocations - before;
        check_refusal(freed, nfsu::ArenaRefusal::none, "split: free the leading block");
        check_eq(split.stats().free_blocks, 3, "split: three free blocks before");
        check_refusal(place(10, 8, middle), nfsu::ArenaRefusal::none, "split: the middle");
        check_eq(middle.offset, 8, "split: first fit skips the four-byte block");
        const nfsu::ArenaStats worst = split.stats();
        check_eq(worst.live_allocations, 3, "split: at the limit");
        check_eq(worst.free_blocks, 4, "split: max_allocations + 1 free blocks");
        check_eq(split_inside, 0, "heap allocations inside a two-sided split at the limit");
    }

    Random random(0xA110C);
    for (int step = 0; step < 20000; ++step) {
        if (live.size() < limit && (live.empty() || random.below(2) == 0)) {
            allocate(1U + random.below(96), std::uint64_t{1} << random.below(7));
        } else {
            release(static_cast<std::size_t>(random.below(live.size())));
        }
        if (step % 1000 == 999) {
            const std::size_t before = g_heap_allocations;
            arena.reset();
            inside += g_heap_allocations - before;
            live.clear();
        }
    }
    check_eq(inside, 0, "heap allocations made inside arena calls");
    check(arena.stats().allocations > 5000, "the random part placed enough to mean something");
}

// ---------------------------------------------------------------------------
// The plan
// ---------------------------------------------------------------------------

// Defect: CPU arenas that overlap, a base that is not aligned, a
// cpu_bytes() short of the last arena's end, so that the one block the
// platform reserves is smaller than the plan, or an arena built with another
// category's allocation limit, which is why every category is given a
// different one here. Each CPU arena is filled to its last byte, and the next
// byte must be refused rather than placed in the arena after it. A refused
// budget must leave a working plan as it was.
void test_memory_plan_layout() {
    nfsu::MemoryBudget budget;
    budget.capacity = {1000, 3000, 500, 8192};
    budget.max_alignment = {16, 64, 16, 4096};
    budget.max_allocations = {8, 16, 32, 64};
    nfsu::MemoryPlan plan;
    std::string error;
    const bool mem_ok_7 = plan.create(budget, error);
    check(mem_ok_7, "plan create: " + error);
    const std::uint64_t bases[] = {0, 1024, 4032, 0};
    std::array<nfsu::ArenaAllocation, nfsu::kMemoryCategoryCount> wholes{};
    for (std::size_t i = 0; i < nfsu::kMemoryCategoryCount; ++i) {
        const auto category = static_cast<nfsu::MemoryCategory>(i);
        nfsu::MemoryArena& arena = plan.arena(category);
        const std::string what = std::string(nfsu::memory_category_name(category)) + " arena";
        check(arena.config().category == category, what + ": category");
        check_eq(arena.config().base, bases[i], what + ": base");
        check_eq(arena.config().capacity, budget.capacity[i], what + ": capacity");
        check_eq(arena.config().max_alignment, budget.max_alignment[i], what + ": alignment");
        check_eq(arena.config().max_allocations, budget.max_allocations[i],
                 what + ": allocation limit");
        nfsu::ArenaAllocation& whole = wholes[i];
        check_refusal(arena.allocate(budget.capacity[i], 1, whole), nfsu::ArenaRefusal::none,
                      what + ": the whole arena");
        check_eq(whole.offset, bases[i], what + ": starts at its base");
        nfsu::ArenaAllocation spill;
        check_refusal(arena.allocate(1, 1, spill), nfsu::ArenaRefusal::over_budget,
                      what + ": nothing past its end");
        if (i + 1 < 3) {
            check(whole.offset + whole.size <= bases[i + 1],
                  what + ": ends before the next CPU arena");
        }
    }
    check_eq(plan.cpu_bytes(), 4532, "cpu bytes end with the transient arena");
    check_eq(plan.gpu_bytes(), 8192, "gpu bytes");
    const std::array<nfsu::ArenaStats, nfsu::kMemoryCategoryCount> stats = plan.stats();
    for (std::size_t i = 0; i < nfsu::kMemoryCategoryCount; ++i) {
        check_eq(stats[i].used_bytes, budget.capacity[i], "plan stats " + std::to_string(i));
    }

    nfsu::MemoryBudget broken = budget;
    broken.max_alignment[2] = 48;
    check(!plan.create(broken, error), "a budget with a bad alignment is refused");
    const bool mem_ok_8 = error.find("transient arena") != std::string::npos;
    check(mem_ok_8, "the refusal names the arena: " + error);
    check_eq(plan.arena(nfsu::MemoryCategory::streaming).stats().used_bytes, 3000,
             "a refused budget leaves the plan as it was");
    check_eq(plan.cpu_bytes(), 4532, "a refused budget leaves cpu bytes");
    // Figures can survive an arena whose bookkeeping was taken away; a
    // release cannot, so each arena must still own what it handed out.
    for (std::size_t i = 0; i < nfsu::kMemoryCategoryCount; ++i) {
        check_refusal(plan.arena(static_cast<nfsu::MemoryCategory>(i)).release(wholes[i].handle),
                      nfsu::ArenaRefusal::none,
                      "arena " + std::to_string(i) + " still owns its allocation after a "
                      "refused budget");
    }

    nfsu::MemoryBudget wraps = budget;
    wraps.capacity[0] = nfsu::kMaxArenaCapacity;
    wraps.capacity[1] = nfsu::kMaxArenaCapacity;
    wraps.capacity[2] = nfsu::kMaxArenaCapacity;
    const bool mem_ok_9 = plan.create(wraps, error);
    check(mem_ok_9, "three maximal CPU arenas fit in 64 bits: " + error);
    check_eq(plan.cpu_bytes(), 3U * nfsu::kMaxArenaCapacity, "three maximal arenas end-to-end");
}

struct TestCase {
    const char* name;
    void (*run)();
};

const TestCase kTests[] = {
    {"names", test_names},
    {"create_refuses_bad_configurations", test_create_refuses_bad_configurations},
    {"default_arena_refuses", test_default_arena_refuses},
    {"exact_fit_at_the_boundary", test_exact_fit_at_the_boundary},
    {"exact_fit_with_alignment", test_exact_fit_with_alignment},
    {"hostile_requests_are_refused", test_hostile_requests_are_refused},
    {"fragmentation_figures", test_fragmentation_figures},
    {"release_merges_in_every_direction", test_release_merges_in_every_direction},
    {"random_operations_against_the_model", test_random_operations_against_the_model},
    {"load_and_unload_cycles_return_to_the_start", test_load_and_unload_cycles_return_to_the_start},
    {"interleaved_streaming_cycles_do_not_creep", test_interleaved_streaming_cycles_do_not_creep},
    {"frames_reset_the_transient_arena", test_frames_reset_the_transient_arena},
    {"handles_are_checked", test_handles_are_checked},
    {"create_again_keeps_old_handles_stale", test_create_again_keeps_old_handles_stale},
    {"create_again_starts_every_figure_over", test_create_again_starts_every_figure_over},
    {"high_water_marks_are_peaks", test_high_water_marks_are_peaks},
    {"allocation_limit", test_allocation_limit},
    {"arena_calls_never_allocate", test_arena_calls_never_allocate},
    {"memory_plan_layout", test_memory_plan_layout},
};

} // namespace

// With no argument every test runs, which is how CTest runs this program.
// With one, only the test of that name runs - the name without its "test_"
// prefix - so that a failure can be attributed to the test that made it. A
// name no test has is itself a failure: a misspelt filter must not pass by
// running nothing.
int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    bool ran = false;
    for (const TestCase& test : kTests) {
        if (only.empty() || only == test.name) {
            test.run();
            ran = true;
        }
    }
    if (!ran) {
        std::cerr << "FAIL: no test is named " << only << '\n';
        ++failures;
    }

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all memory arena tests passed\n";
    return 0;
}
