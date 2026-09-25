#include "nfsu/memory_arena.h"

#include <algorithm>
#include <new>
#include <utility>

namespace nfsu {

namespace {

bool is_power_of_two(std::uint64_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

// Callers keep value below kMaxArenaCapacity and alignment at most
// kMaxArenaAlignment, so the sum cannot overflow.
std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

} // namespace

const char* memory_category_name(MemoryCategory category) noexcept {
    switch (category) {
    case MemoryCategory::persistent:
        return "persistent";
    case MemoryCategory::streaming:
        return "streaming";
    case MemoryCategory::transient:
        return "transient";
    case MemoryCategory::gpu:
        return "gpu";
    }
    return "unknown";
}

const char* arena_refusal_name(ArenaRefusal refusal) noexcept {
    switch (refusal) {
    case ArenaRefusal::none:
        return "none";
    case ArenaRefusal::zero_size:
        return "zero size";
    case ArenaRefusal::bad_alignment:
        return "bad alignment";
    case ArenaRefusal::over_budget:
        return "over budget";
    case ArenaRefusal::no_fit:
        return "no fit";
    case ArenaRefusal::allocation_limit:
        return "allocation limit";
    case ArenaRefusal::unknown_handle:
        return "unknown handle";
    case ArenaRefusal::stale_handle:
        return "stale handle";
    }
    return "unknown";
}

bool MemoryArena::create(const ArenaConfig& config, std::string& error) {
    const std::string label = std::string(memory_category_name(config.category)) + " arena";
    if (config.capacity == 0 || config.capacity > kMaxArenaCapacity) {
        error = label + ": capacity " + std::to_string(config.capacity) +
                " is outside 1.." + std::to_string(kMaxArenaCapacity);
        return false;
    }
    if (!is_power_of_two(config.max_alignment) ||
        config.max_alignment > kMaxArenaAlignment) {
        error = label + ": max alignment " + std::to_string(config.max_alignment) +
                " is not a power of two in 1.." + std::to_string(kMaxArenaAlignment);
        return false;
    }
    if (config.base % config.max_alignment != 0) {
        error = label + ": base " + std::to_string(config.base) +
                " is not a multiple of the max alignment " +
                std::to_string(config.max_alignment);
        return false;
    }
    if (config.base > UINT64_MAX - config.capacity) {
        error = label + ": base " + std::to_string(config.base) + " plus capacity " +
                std::to_string(config.capacity) + " passes 2^64 - 1";
        return false;
    }
    if (config.max_allocations == 0 || config.max_allocations > kMaxArenaAllocations) {
        error = label + ": max allocations " + std::to_string(config.max_allocations) +
                " is outside 1.." + std::to_string(kMaxArenaAllocations);
        return false;
    }

    // Built aside and swapped in, so a failed allocation leaves the arena
    // exactly as it was. Live allocations separate free blocks, so there are
    // never more free blocks than live allocations plus one.
    std::vector<Block> free_blocks;
    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_slots;
    try {
        free_blocks.reserve(static_cast<std::size_t>(config.max_allocations) + 1U);
        slots.reserve(config.max_allocations);
        free_slots.reserve(config.max_allocations);
    } catch (const std::bad_alloc&) {
        error = label + ": out of memory reserving bookkeeping for " +
                std::to_string(config.max_allocations) + " allocations";
        return false;
    }
    free_blocks.push_back(Block{0, config.capacity});
    std::uint64_t floor = generation_floor_;
    for (const Slot& slot : slots_) {
        floor = std::max(floor, slot.generation + 1U);
    }

    config_ = config;
    generation_floor_ = floor;
    free_.swap(free_blocks);
    slots_.swap(slots);
    free_slots_.swap(free_slots);
    used_ = 0;
    live_ = 0;
    high_water_bytes_ = 0;
    high_water_extent_ = 0;
    period_high_water_bytes_ = 0;
    period_high_water_extent_ = 0;
    allocations_ = 0;
    releases_ = 0;
    resets_ = 0;
    refused_.fill(0);
    return true;
}

void MemoryArena::refuse(ArenaRefusal reason) noexcept {
    ++refused_[static_cast<std::size_t>(reason)];
}

std::uint64_t MemoryArena::current_extent() const noexcept {
    if (live_ == 0) {
        return 0;
    }
    // The last free block, when it reaches the end, is the only thing past
    // the highest live byte.
    if (!free_.empty() && free_.back().offset + free_.back().size == config_.capacity) {
        return free_.back().offset;
    }
    return config_.capacity;
}

ArenaRefusal MemoryArena::allocate(std::uint64_t size, std::uint64_t alignment,
                                   ArenaAllocation& out) noexcept {
    if (size == 0) {
        refuse(ArenaRefusal::zero_size);
        return ArenaRefusal::zero_size;
    }
    if (!is_power_of_two(alignment) || alignment > config_.max_alignment) {
        refuse(ArenaRefusal::bad_alignment);
        return ArenaRefusal::bad_alignment;
    }
    if (live_ >= config_.max_allocations) {
        refuse(ArenaRefusal::allocation_limit);
        return ArenaRefusal::allocation_limit;
    }
    // Compared as a difference, so a size near 2^64 cannot wrap into range.
    if (size > config_.capacity - used_) {
        refuse(ArenaRefusal::over_budget);
        return ArenaRefusal::over_budget;
    }

    for (std::size_t i = 0; i < free_.size(); ++i) {
        const Block block = free_[i];
        const std::uint64_t start = align_up(block.offset, alignment);
        const std::uint64_t gap = start - block.offset;
        if (gap > block.size || size > block.size - gap) {
            continue;
        }
        const std::uint64_t tail = block.size - gap - size;
        // Capacity for one more block was reserved by create(), so the
        // insert cannot reallocate.
        if (gap != 0 && tail != 0) {
            free_[i].size = gap;
            free_.insert(free_.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                         Block{start + size, tail});
        } else if (gap != 0) {
            free_[i].size = gap;
        } else if (tail != 0) {
            free_[i] = Block{start + size, tail};
        } else {
            free_.erase(free_.begin() + static_cast<std::ptrdiff_t>(i));
        }

        std::uint32_t slot = 0;
        if (!free_slots_.empty()) {
            slot = free_slots_.back();
            free_slots_.pop_back();
        } else {
            slot = static_cast<std::uint32_t>(slots_.size());
            Slot fresh;
            fresh.generation = generation_floor_;
            slots_.push_back(fresh);
        }
        Slot& record = slots_[slot];
        record.offset = start;
        record.size = size;
        record.live = true;

        used_ += size;
        ++live_;
        ++allocations_;
        const std::uint64_t extent = current_extent();
        high_water_bytes_ = std::max(high_water_bytes_, used_);
        high_water_extent_ = std::max(high_water_extent_, extent);
        period_high_water_bytes_ = std::max(period_high_water_bytes_, used_);
        period_high_water_extent_ = std::max(period_high_water_extent_, extent);

        out.offset = config_.base + start;
        out.size = size;
        out.alignment = alignment;
        out.handle = ArenaHandle{slot, record.generation};
        return ArenaRefusal::none;
    }
    refuse(ArenaRefusal::no_fit);
    return ArenaRefusal::no_fit;
}

ArenaRefusal MemoryArena::release(const ArenaHandle& handle) noexcept {
    if (handle.slot >= slots_.size()) {
        refuse(ArenaRefusal::unknown_handle);
        return ArenaRefusal::unknown_handle;
    }
    Slot& record = slots_[handle.slot];
    if (handle.generation > record.generation ||
        (handle.generation == record.generation && !record.live)) {
        // Released slots move to a new generation at once, so a slot that is
        // free at the handle's own generation was never issued under it.
        refuse(ArenaRefusal::unknown_handle);
        return ArenaRefusal::unknown_handle;
    }
    if (handle.generation < record.generation) {
        refuse(ArenaRefusal::stale_handle);
        return ArenaRefusal::stale_handle;
    }

    const std::uint64_t offset = record.offset;
    const std::uint64_t size = record.size;
    const auto next = std::upper_bound(
        free_.begin(), free_.end(), offset,
        [](std::uint64_t value, const Block& block) { return value < block.offset; });
    const bool join_previous =
        next != free_.begin() && (next - 1)->offset + (next - 1)->size == offset;
    const bool join_next = next != free_.end() && offset + size == next->offset;
    if (join_previous && join_next) {
        (next - 1)->size += size + next->size;
        free_.erase(next);
    } else if (join_previous) {
        (next - 1)->size += size;
    } else if (join_next) {
        next->offset = offset;
        next->size += size;
    } else {
        // Capacity for live + 1 blocks was reserved by create(), so the
        // insert cannot reallocate.
        free_.insert(next, Block{offset, size});
    }

    record.live = false;
    ++record.generation;
    free_slots_.push_back(handle.slot);
    used_ -= size;
    --live_;
    ++releases_;
    return ArenaRefusal::none;
}

void MemoryArena::reset() noexcept {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& record = slots_[i];
        if (record.live) {
            record.live = false;
            ++record.generation;
            free_slots_.push_back(static_cast<std::uint32_t>(i));
        }
    }
    free_.clear();
    if (config_.capacity != 0) {
        free_.push_back(Block{0, config_.capacity});
    }
    used_ = 0;
    live_ = 0;
    period_high_water_bytes_ = 0;
    period_high_water_extent_ = 0;
    ++resets_;
}

ArenaStats MemoryArena::stats() const noexcept {
    ArenaStats stats;
    stats.category = config_.category;
    stats.base = config_.base;
    stats.capacity = config_.capacity;
    stats.used_bytes = used_;
    stats.free_bytes = config_.capacity - used_;
    stats.live_allocations = live_;
    stats.free_blocks = free_.size();
    for (const Block& block : free_) {
        stats.largest_free_block = std::max(stats.largest_free_block, block.size);
    }
    if (stats.free_bytes != 0) {
        stats.fragmentation_permille = static_cast<std::uint32_t>(
            (1000U * (stats.free_bytes - stats.largest_free_block)) / stats.free_bytes);
    }
    stats.extent = current_extent();
    stats.high_water_bytes = high_water_bytes_;
    stats.high_water_extent = high_water_extent_;
    stats.period_high_water_bytes = period_high_water_bytes_;
    stats.period_high_water_extent = period_high_water_extent_;
    stats.allocations = allocations_;
    stats.releases = releases_;
    stats.resets = resets_;
    stats.refused = refused_;
    for (const std::uint64_t count : refused_) {
        stats.refusals += count;
    }
    return stats;
}

std::vector<ArenaRange> MemoryArena::free_ranges() const {
    std::vector<ArenaRange> ranges;
    ranges.reserve(free_.size());
    for (const Block& block : free_) {
        ranges.push_back(ArenaRange{config_.base + block.offset, block.size});
    }
    return ranges;
}

bool MemoryPlan::create(const MemoryBudget& budget, std::string& error) {
    std::array<MemoryArena, kMemoryCategoryCount> arenas;
    std::uint64_t cursor = 0;
    std::uint64_t cpu_bytes = 0;
    for (std::size_t i = 0; i < kMemoryCategoryCount; ++i) {
        const auto category = static_cast<MemoryCategory>(i);
        ArenaConfig config;
        config.category = category;
        config.capacity = budget.capacity[i];
        config.max_alignment = budget.max_alignment[i];
        config.max_allocations = budget.max_allocations[i];
        if (category != MemoryCategory::gpu) {
            // Rounded up here only when the alignment is one create() would
            // accept, so the rounding itself stays inside 2^64; anything else
            // is refused below with the arena's own message.
            if (is_power_of_two(config.max_alignment) &&
                config.max_alignment <= kMaxArenaAlignment) {
                if (cursor > UINT64_MAX - (config.max_alignment - 1U)) {
                    error = std::string(memory_category_name(category)) +
                            " arena: the CPU layout passes 2^64 - 1";
                    return false;
                }
                cursor = align_up(cursor, config.max_alignment);
            }
            config.base = cursor;
        }
        if (!arenas[i].create(config, error)) {
            return false;
        }
        if (category != MemoryCategory::gpu) {
            cursor = config.base + config.capacity;
            cpu_bytes = cursor;
        }
    }
    arenas_.swap(arenas);
    cpu_bytes_ = cpu_bytes;
    gpu_bytes_ = budget.capacity[static_cast<std::size_t>(MemoryCategory::gpu)];
    return true;
}

MemoryArena& MemoryPlan::arena(MemoryCategory category) noexcept {
    return arenas_[static_cast<std::size_t>(category)];
}

const MemoryArena& MemoryPlan::arena(MemoryCategory category) const noexcept {
    return arenas_[static_cast<std::size_t>(category)];
}

std::array<ArenaStats, kMemoryCategoryCount> MemoryPlan::stats() const noexcept {
    std::array<ArenaStats, kMemoryCategoryCount> all;
    for (std::size_t i = 0; i < kMemoryCategoryCount; ++i) {
        all[i] = arenas_[i].stats();
    }
    return all;
}

} // namespace nfsu
