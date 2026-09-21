#include "nfsu/track_collision.h"

#include "nfsu/chunk.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

namespace nfsu {
namespace {

static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float32 required");
static_assert(sizeof(float) == 4, "32-bit float required");

constexpr std::uint64_t kHeaderSize = 8;

static_assert(kCollisionPolygonScratchOffset + 2 == kCollisionPolygonSlot0Offset,
              "the scratch halfword ends where the first coordinate group starts");
static_assert(kCollisionPolygonSlot0Offset + 2 * kCollisionPolygonCorners ==
                  kCollisionPolygonSlot1Offset,
              "the coordinate groups are adjacent");
static_assert(kCollisionPolygonSlot1Offset + 2 * kCollisionPolygonCorners ==
                  kCollisionPolygonSlot2Offset,
              "the coordinate groups are adjacent");
static_assert(kCollisionPolygonSlot2Offset + 2 * kCollisionPolygonCorners ==
                  kCollisionPolygonRecordSize,
              "the third coordinate group ends the polygon record");
static_assert(kCollisionBoundsMaxOffset + 12 <= kCollisionBoundsRecordSize,
              "the maximum triple lies inside the bounds record");

std::uint16_t read_u16_le(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned int>(bytes[0]) |
        (static_cast<unsigned int>(bytes[1]) << 8U));
}

std::int16_t read_i16_le(const unsigned char* bytes) {
    return static_cast<std::int16_t>(read_u16_le(bytes));
}

std::uint32_t read_u32_le(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

float read_f32_le(const unsigned char* bytes) {
    const std::uint32_t bits = read_u32_le(bytes);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) {
        return false;
    }
    out = a + b;
    return true;
}

bool align_up(std::uint64_t value, std::uint64_t alignment, std::uint64_t& out) {
    if (alignment == 0) {
        return false;
    }
    const std::uint64_t remainder = value % alignment;
    if (remainder == 0) {
        out = value;
        return true;
    }
    return checked_add(value, alignment - remainder, out);
}

// A diagnostic must not depend on the process-global locale, exactly as in
// src/track.cpp and src/track_objects.cpp: two runs over the same bytes
// have to produce the same text.
class Message {
public:
    Message() { stream_.imbue(std::locale::classic()); }

    template <typename T>
    Message& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    std::string str() const { return stream_.str(); }

private:
    std::ostringstream stream_;
};

std::string hex_byte(unsigned char value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "0x";
    text.push_back(digits[(value >> 4U) & 0x0FU]);
    text.push_back(digits[value & 0x0FU]);
    return text;
}

struct RawChunk {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
};

// The five chunks, in the order the parser reports a missing one.
struct ChunkSlot {
    std::uint32_t id;
    const char* what;
};

constexpr ChunkSlot kSlots[] = {
    {kCollisionTreeId, "quadtree"},
    {kCollisionCellListId, "cell list"},
    {kCollisionPolygonListId, "polygon list"},
    {kCollisionIndexListId, "index list"},
    {kCollisionBoundsId, "bounds"},
};
constexpr std::size_t kSlotCount = sizeof(kSlots) / sizeof(kSlots[0]);

class CollisionParser {
public:
    CollisionParser(
        const unsigned char* data,
        std::uint64_t size,
        std::uint64_t base,
        const TrackCollisionLimits& limits)
        : data_(data), size_(size), base_(base), limits_(limits) {}

    TrackCollisionReport run() {
        report_.region.offset = base_;
        report_.region.size = size_;
        report_.cost.region_bytes = size_;

        if (size_ > limits_.max_region_bytes) {
            issue(base_, Message() << "collision image is " << size_
                                   << " bytes, over the limit of "
                                   << limits_.max_region_bytes);
            return std::move(report_);
        }
        if (!walk_top_level()) {
            return std::move(report_);
        }
        if (!require_slots()) {
            return std::move(report_);
        }
        const bool polygons = parse_polygons();
        const bool cells = parse_cells();
        const bool indices = parse_indices();
        const bool nodes = parse_tree_nodes();
        const bool bounds = parse_bounds();
        if (!(polygons && cells && indices && nodes && bounds)) {
            return std::move(report_);
        }
        // The tree is checked before the cells' contents: a leaf in the
        // wrong square makes every query over it wrong, so that is the
        // rule a caller has to hear about first.
        if (!walk_tree()) {
            return std::move(report_);
        }
        if (!check_cell_slices()) {
            return std::move(report_);
        }
        if (!check_bounds()) {
            return std::move(report_);
        }
        report_.valid = report_.issues.empty();
        return std::move(report_);
    }

private:
    void issue(std::uint64_t offset, const Message& text) {
        TrackIssue entry;
        entry.offset = offset;
        entry.message = text.str();
        report_.issues.push_back(std::move(entry));
    }

    const unsigned char* at(std::uint64_t absolute) const {
        return data_ + (absolute - base_);
    }

    bool walk_top_level() {
        std::uint64_t cursor = 0;
        while (cursor < size_) {
            if (size_ - cursor < kHeaderSize) {
                issue(base_ + cursor,
                      Message() << "trailing " << (size_ - cursor)
                                << " bytes are shorter than a chunk header");
                return false;
            }
            RawChunk chunk;
            chunk.offset = base_ + cursor;
            chunk.id = read_u32_le(data_ + cursor);
            chunk.payload_size = read_u32_le(data_ + cursor + 4);
            chunk.payload_offset = chunk.offset + kHeaderSize;
            std::uint64_t end = 0;
            if (!checked_add(chunk.payload_offset, chunk.payload_size, end) ||
                end > base_ + size_) {
                issue(chunk.offset,
                      Message() << "chunk " << format_chunk_id(chunk.id)
                                << " claims a payload of " << chunk.payload_size
                                << " bytes, which reaches past the end of the "
                                   "image");
                return false;
            }
            ++report_.cost.chunks;
            if (report_.cost.chunks > limits_.max_chunks) {
                issue(chunk.offset, Message()
                                        << "image holds more than "
                                        << limits_.max_chunks << " chunks");
                return false;
            }
            for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
                if (kSlots[slot].id != chunk.id) {
                    continue;
                }
                if (found_[slot]) {
                    issue(chunk.offset,
                          Message() << "image holds more than one "
                                    << format_chunk_id(chunk.id) << " "
                                    << kSlots[slot].what << " chunk");
                    return false;
                }
                found_[slot] = true;
                slot_[slot] = chunk;
            }
            cursor = end - base_;
        }
        return true;
    }

    bool require_slots() {
        for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
            if (!found_[slot]) {
                issue(base_, Message()
                                 << "no " << format_chunk_id(kSlots[slot].id)
                                 << " " << kSlots[slot].what
                                 << " chunk in the image");
                return false;
            }
        }
        return true;
    }

    // The data of a chunk whose record array starts at the first 16-byte
    // boundary at or after its payload, with the filler checked.
    bool aligned_data(
        const RawChunk& chunk,
        const char* what,
        std::uint64_t& start,
        std::uint64_t& length) {
        if (!align_up(chunk.payload_offset, kCollisionRecordAlignment, start)) {
            issue(chunk.offset,
                  Message() << what << " payload offset " << chunk.payload_offset
                            << " cannot be aligned to "
                            << kCollisionRecordAlignment << " bytes");
            return false;
        }
        const std::uint64_t end = chunk.payload_offset + chunk.payload_size;
        if (start > end) {
            issue(chunk.offset,
                  Message() << what << " payload of " << chunk.payload_size
                            << " bytes is shorter than its "
                            << kCollisionRecordAlignment
                            << "-byte alignment prefix");
            return false;
        }
        for (std::uint64_t i = chunk.payload_offset; i < start; ++i) {
            const unsigned char byte = *at(i);
            if (byte != kCollisionFillerByte) {
                issue(i, Message() << what << " alignment filler at offset " << i
                                   << " is " << hex_byte(byte) << ", expected "
                                   << hex_byte(kCollisionFillerByte));
                return false;
            }
        }
        length = end - start;
        return true;
    }

    bool parse_polygons() {
        const RawChunk& chunk = slot_[2];
        // 0x004413E2: lea edx, [ecx + 8], the payload itself.
        const std::uint64_t start = chunk.payload_offset;
        const std::uint64_t length = chunk.payload_size;
        if (length % kCollisionPolygonRecordSize != 0) {
            issue(chunk.offset,
                  Message() << "polygon list payload of " << length
                            << " bytes is not a whole number of "
                            << kCollisionPolygonRecordSize << "-byte records");
            return false;
        }
        const std::uint64_t count = length / kCollisionPolygonRecordSize;
        if (count == 0) {
            issue(chunk.offset, Message() << "polygon list holds no record");
            return false;
        }
        if (count > limits_.max_polygons) {
            issue(chunk.offset, Message()
                                    << "polygon list holds " << count
                                    << " records, over the limit of "
                                    << limits_.max_polygons);
            return false;
        }
        report_.polygons.resize(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TrackCollisionPolygon& polygon =
                report_.polygons[static_cast<std::size_t>(i)];
            const std::uint64_t offset =
                start + i * kCollisionPolygonRecordSize;
            const unsigned char* record = at(offset);
            polygon.index = static_cast<std::size_t>(i);
            polygon.offset = offset;
            std::memcpy(polygon.unknown0, record, sizeof(polygon.unknown0));
            polygon.vertical_base = read_i16_le(record + 4);
            polygon.stored_scratch =
                read_u16_le(record + kCollisionPolygonScratchOffset);
            // 0x00441403 stamps this halfword over every record before
            // anything reads it. Most retail records already hold the
            // stamp and 29680 hold a value in 0x0000..0x03FF instead, so
            // the two populations are counted apart. Neither is decoded.
            if (polygon.stored_scratch == kCollisionPolygonScratchValue) {
                ++report_.cost.scratch_sentinel_polygons;
            }
            bool saturated = false;
            for (std::size_t c = 0; c < kCollisionPolygonCorners; ++c) {
                polygon.slot0[c] =
                    read_i16_le(record + kCollisionPolygonSlot0Offset + 2 * c);
                polygon.slot1[c] =
                    read_i16_le(record + kCollisionPolygonSlot1Offset + 2 * c);
                polygon.slot2[c] =
                    read_i16_le(record + kCollisionPolygonSlot2Offset + 2 * c);
                polygon.corner[c][0] =
                    static_cast<float>(polygon.slot0[c]) * kCollisionHorizontalUnit;
                polygon.corner[c][1] =
                    static_cast<float>(polygon.slot1[c]) * kCollisionHorizontalUnit;
                // 0x0043FD2E..0x0043FD71: the runtime forms
                // ((base << 8) + slot2) << 8 and multiplies by 1/65536,
                // which is base + slot2 / 256. Dropping the base is wrong
                // for the 240793 retail records whose base is non-zero.
                polygon.corner[c][2] =
                    static_cast<float>(polygon.vertical_base) +
                    static_cast<float>(polygon.slot2[c]) * kCollisionVerticalUnit;
                if (polygon.slot2[c] >= kCollisionVerticalSaturation ||
                    polygon.slot2[c] <= -kCollisionVerticalSaturation) {
                    saturated = true;
                }
            }
            if (saturated) {
                ++report_.cost.saturated_vertical_polygons;
            }
            bool degenerate = false;
            for (std::size_t c = 0; c < kCollisionPolygonCorners; ++c) {
                const std::size_t next = (c + 1) % kCollisionPolygonCorners;
                if (polygon.slot0[c] == polygon.slot0[next] &&
                    polygon.slot1[c] == polygon.slot1[next] &&
                    polygon.slot2[c] == polygon.slot2[next]) {
                    degenerate = true;
                }
            }
            if (degenerate) {
                ++report_.cost.degenerate_corner_polygons;
            }
        }
        report_.cost.polygons = count;
        report_.cost.source_polygon_bytes = length;
        return true;
    }

    bool parse_cells() {
        const RawChunk& chunk = slot_[1];
        std::uint64_t start = 0;
        std::uint64_t length = 0;
        if (!aligned_data(chunk, "cell list", start, length)) {
            return false;
        }
        if (length % kCollisionCellRecordSize != 0) {
            issue(chunk.offset,
                  Message() << "cell list holds " << length
                            << " bytes after its alignment prefix, which is "
                               "not a whole number of "
                            << kCollisionCellRecordSize << "-byte records");
            return false;
        }
        const std::uint64_t count = length / kCollisionCellRecordSize;
        if (count == 0) {
            issue(chunk.offset, Message() << "cell list holds no record");
            return false;
        }
        if (count > limits_.max_cells) {
            issue(chunk.offset, Message() << "cell list holds " << count
                                          << " records, over the limit of "
                                          << limits_.max_cells);
            return false;
        }
        report_.cells.resize(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TrackCollisionCell& cell = report_.cells[static_cast<std::size_t>(i)];
            const std::uint64_t offset = start + i * kCollisionCellRecordSize;
            const unsigned char* record = at(offset);
            cell.index = static_cast<std::size_t>(i);
            cell.offset = offset;
            cell.center[0] = read_f32_le(record);
            cell.center[1] = read_f32_le(record + 4);
            cell.first_index = read_u32_le(record + 8);
            cell.index_count = read_u32_le(record + 12);
            // The sentinel is checked too. It is exactly
            // (999999.0, 999999.0) in all 31 retail tracks, so nothing
            // real is refused, and exempting it would let a crafted image
            // carry a NaN through the parser unbounded.
            if (!std::isfinite(cell.center[0]) ||
                !std::isfinite(cell.center[1])) {
                issue(offset, Message() << "cell " << i
                                        << " stores a centre that is not finite");
                return false;
            }
        }
        report_.cost.cells = count;
        report_.cost.source_cell_bytes = length;
        return true;
    }

    bool parse_indices() {
        const RawChunk& chunk = slot_[3];
        // 0x00441423: lea edx, [ecx + 8], the payload itself, and
        // 0x0044142C divides its whole size by two.
        const std::uint64_t length = chunk.payload_size;
        if (length % 2 != 0) {
            issue(chunk.offset, Message()
                                    << "index list payload of " << length
                                    << " bytes is not a whole number of "
                                       "16-bit indices");
            return false;
        }
        const std::uint64_t count = length / 2;
        if (count == 0) {
            issue(chunk.offset, Message() << "index list holds no index");
            return false;
        }
        if (count > limits_.max_indices) {
            issue(chunk.offset, Message() << "index list holds " << count
                                          << " indices, over the limit of "
                                          << limits_.max_indices);
            return false;
        }
        report_.indices.resize(static_cast<std::size_t>(count));
        const std::uint64_t polygons = report_.cost.polygons;
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint32_t value =
                read_u16_le(at(chunk.payload_offset + i * 2));
            report_.indices[static_cast<std::size_t>(i)] = value;
            if (value >= polygons) {
                issue(chunk.payload_offset + i * 2,
                      Message() << "index " << i << " names polygon " << value
                                << ", but the polygon list holds " << polygons
                                << " records");
                return false;
            }
        }
        report_.cost.indices = count;
        report_.cost.source_index_bytes = length;
        return true;
    }

    bool parse_tree_nodes() {
        const RawChunk& chunk = slot_[0];
        std::uint64_t start = 0;
        std::uint64_t length = 0;
        if (!aligned_data(chunk, "quadtree", start, length)) {
            return false;
        }
        if (length % kCollisionTreeNodeSize != 0) {
            issue(chunk.offset,
                  Message() << "quadtree holds " << length
                            << " bytes after its alignment prefix, which is "
                               "not a whole number of "
                            << kCollisionTreeNodeSize << "-byte nodes");
            return false;
        }
        const std::uint64_t count = length / kCollisionTreeNodeSize;
        if (count == 0) {
            issue(chunk.offset, Message() << "quadtree holds no node");
            return false;
        }
        if (count > limits_.max_tree_nodes) {
            issue(chunk.offset, Message() << "quadtree holds " << count
                                          << " nodes, over the limit of "
                                          << limits_.max_tree_nodes);
            return false;
        }
        report_.nodes.resize(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TrackCollisionTreeNode& node =
                report_.nodes[static_cast<std::size_t>(i)];
            const std::uint64_t offset = start + i * kCollisionTreeNodeSize;
            const unsigned char* record = at(offset);
            node.index = static_cast<std::size_t>(i);
            node.offset = offset;
            for (std::size_t e = 0; e < kCollisionTreeNodeEntries; ++e) {
                node.entries[e] = read_u16_le(record + 2 * e);
            }
        }
        report_.cost.tree_nodes = count;
        report_.cost.source_tree_bytes = length;
        return true;
    }

    bool parse_bounds() {
        const RawChunk& chunk = slot_[4];
        std::uint64_t start = 0;
        std::uint64_t length = 0;
        if (!aligned_data(chunk, "bounds", start, length)) {
            return false;
        }
        if (length != kCollisionBoundsRecordSize) {
            issue(chunk.offset,
                  Message() << "bounds chunk holds " << length
                            << " bytes after its alignment prefix, expected "
                            << kCollisionBoundsRecordSize);
            return false;
        }
        const unsigned char* record = at(start);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            report_.cost.stored_min[axis] =
                read_f32_le(record + 4 * axis);
            report_.cost.stored_max[axis] =
                read_f32_le(record + kCollisionBoundsMaxOffset + 4 * axis);
            if (!std::isfinite(report_.cost.stored_min[axis]) ||
                !std::isfinite(report_.cost.stored_max[axis])) {
                issue(start, Message() << "bounds axis " << axis
                                       << " is not finite");
                return false;
            }
            if (report_.cost.stored_min[axis] > report_.cost.stored_max[axis]) {
                issue(start, Message()
                                 << "bounds axis " << axis << " has a minimum of "
                                 << report_.cost.stored_min[axis]
                                 << " above its maximum of "
                                 << report_.cost.stored_max[axis]);
                return false;
            }
        }
        report_.cost.has_bounds = true;
        return true;
    }

    // The cells slice the index list into contiguous runs, in cell order,
    // starting at 0 and leaving at most the two bytes of padding the chunk
    // needs to reach a multiple of four. Cell 0 is the sentinel and must
    // slice nothing.
    bool check_cell_slices() {
        const std::uint64_t index_count = report_.cost.indices;
        std::uint64_t cursor = 0;
        for (std::size_t i = 0; i < report_.cells.size(); ++i) {
            const TrackCollisionCell& cell = report_.cells[i];
            std::uint64_t end = 0;
            if (!checked_add(cell.first_index, cell.index_count, end) ||
                end > index_count) {
                issue(cell.offset,
                      Message() << "cell " << i << " slices indices "
                                << cell.first_index << " to " << end
                                << ", but the index list holds " << index_count);
                return false;
            }
            if (i == 0) {
                if (cell.first_index != 0 || cell.index_count != 0) {
                    issue(cell.offset,
                          Message() << "cell 0 is the empty sentinel and must "
                                       "slice nothing, but it slices "
                                    << cell.index_count << " indices at "
                                    << cell.first_index);
                    return false;
                }
                continue;
            }
            if (cell.first_index != cursor) {
                issue(cell.offset,
                      Message() << "cell " << i << " starts at index "
                                << cell.first_index
                                << ", but the cells before it end at " << cursor);
                return false;
            }
            cursor = end;
            report_.cost.cell_polygon_pairs += cell.index_count;
        }
        // Retail pads the chunk to a multiple of four bytes, which leaves
        // at most one unused index. Anything more is unaccounted data.
        const std::uint64_t spare = index_count - cursor;
        if (spare > 1) {
            issue(slot_[3].offset,
                  Message() << "the cells slice " << cursor << " of "
                            << index_count
                            << " indices, leaving " << spare
                            << " unaccounted for");
            return false;
        }
        if (spare == 1 && cursor % 2 == 0) {
            issue(slot_[3].offset,
                  Message() << "the index list has one spare index after "
                            << cursor
                            << " used ones, which is only padding when the used "
                               "count is odd");
            return false;
        }
        std::vector<bool> referenced(report_.polygons.size(), false);
        for (const TrackCollisionCell& cell : report_.cells) {
            for (std::uint32_t k = 0; k < cell.index_count; ++k) {
                const std::uint32_t polygon =
                    report_.indices[static_cast<std::size_t>(cell.first_index) + k];
                referenced[polygon] = true;
                if (!cell_contains_polygon(cell, report_.polygons[polygon])) {
                    issue(cell.offset,
                          Message() << "cell " << cell.index << " at ("
                                    << cell.center[0] << ", " << cell.center[1]
                                    << ") names polygon " << polygon
                                    << ", which does not overlap it");
                    return false;
                }
            }
        }
        for (bool seen : referenced) {
            if (!seen) {
                ++report_.cost.unreferenced_polygons;
            }
        }
        return true;
    }

    static bool cell_contains_polygon(
        const TrackCollisionCell& cell,
        const TrackCollisionPolygon& polygon) {
        const float half = kCollisionCellSize * 0.5F;
        for (std::size_t axis = 0; axis < 2; ++axis) {
            float low = polygon.corner[0][axis];
            float high = low;
            for (std::size_t c = 1; c < kCollisionPolygonCorners; ++c) {
                low = std::min(low, polygon.corner[c][axis]);
                high = std::max(high, polygon.corner[c][axis]);
            }
            if (high < cell.center[axis] - half || low > cell.center[axis] + half) {
                return false;
            }
        }
        return true;
    }

    // Walks the quadtree from node 0 over the fixed world square. Every
    // node must be reached exactly once, every cell but the sentinel
    // exactly once, and every leaf must land on the centre its record
    // stores.
    bool walk_tree() {
        std::vector<bool> node_seen(report_.nodes.size(), false);
        std::vector<bool> cell_seen(report_.cells.size(), false);
        struct Frame {
            std::size_t node;
            std::uint32_t depth;
            float center[2];
            float half;
        };
        std::vector<Frame> stack;
        Frame root;
        root.node = 0;
        root.depth = 0;
        root.center[0] = 0.0F;
        root.center[1] = 0.0F;
        root.half = kCollisionRootHalfExtent;
        stack.push_back(root);
        node_seen[0] = true;
        report_.nodes[0].reached = true;
        report_.nodes[0].center[0] = 0.0F;
        report_.nodes[0].center[1] = 0.0F;
        report_.nodes[0].half_extent = root.half;
        report_.nodes[0].depth = 0;

        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();
            const TrackCollisionTreeNode& node = report_.nodes[frame.node];
            const float quarter = frame.half * 0.5F;
            for (std::size_t slot = 0; slot < kCollisionTreeNodeEntries; ++slot) {
                const std::uint16_t entry = node.entries[slot];
                if (entry == 0) {
                    continue;
                }
                const float child_center[2] = {
                    frame.center[0] + ((slot & 1U) != 0 ? quarter : -quarter),
                    frame.center[1] + ((slot & 2U) != 0 ? quarter : -quarter)};
                if (frame.depth + 1 < kCollisionTreeDepth) {
                    if (entry >= report_.nodes.size()) {
                        issue(node.offset,
                              Message() << "quadtree node " << frame.node
                                        << " slot " << slot << " names node "
                                        << entry << ", but the quadtree holds "
                                        << report_.nodes.size() << " nodes");
                        return false;
                    }
                    if (node_seen[entry]) {
                        issue(node.offset,
                              Message() << "quadtree node " << frame.node
                                        << " slot " << slot << " names node "
                                        << entry
                                        << ", which is already part of the tree");
                        return false;
                    }
                    node_seen[entry] = true;
                    TrackCollisionTreeNode& child = report_.nodes[entry];
                    child.reached = true;
                    child.center[0] = child_center[0];
                    child.center[1] = child_center[1];
                    child.half_extent = quarter;
                    child.depth = frame.depth + 1;
                    Frame next;
                    next.node = entry;
                    next.depth = frame.depth + 1;
                    next.center[0] = child_center[0];
                    next.center[1] = child_center[1];
                    next.half = quarter;
                    stack.push_back(next);
                    continue;
                }
                if (entry >= report_.cells.size()) {
                    issue(node.offset,
                          Message() << "quadtree node " << frame.node << " slot "
                                    << slot << " names cell " << entry
                                    << ", but the cell list holds "
                                    << report_.cells.size() << " records");
                    return false;
                }
                if (cell_seen[entry]) {
                    issue(node.offset, Message()
                                           << "quadtree node " << frame.node
                                           << " slot " << slot << " names cell "
                                           << entry
                                           << ", which another leaf already names");
                    return false;
                }
                cell_seen[entry] = true;
                TrackCollisionCell& cell = report_.cells[entry];
                const float tolerance = 1.0F / 64.0F;
                if (std::fabs(cell.center[0] - child_center[0]) > tolerance ||
                    std::fabs(cell.center[1] - child_center[1]) > tolerance) {
                    issue(cell.offset,
                          Message() << "cell " << entry << " stores centre ("
                                    << cell.center[0] << ", " << cell.center[1]
                                    << ") but the quadtree places it at ("
                                    << child_center[0] << ", " << child_center[1]
                                    << ")");
                    return false;
                }
            }
        }

        for (std::size_t i = 0; i < node_seen.size(); ++i) {
            if (!node_seen[i]) {
                issue(report_.nodes[i].offset,
                      Message() << "quadtree node " << i
                                << " is not reachable from the root");
                return false;
            }
        }
        for (std::size_t i = 1; i < cell_seen.size(); ++i) {
            if (!cell_seen[i]) {
                issue(report_.cells[i].offset,
                      Message() << "cell " << i
                                << " is not named by any quadtree leaf");
                return false;
            }
        }
        // Cell 0 needs no check of its own: an entry of 0 is the empty
        // marker and is skipped above, so no leaf can name it. That is why
        // the loop over cell_seen starts at 1, and why the sentinel is
        // constrained by the slice rule in check_cell_slices instead.
        return true;
    }

    bool check_bounds() {
        bool first = true;
        for (const TrackCollisionPolygon& polygon : report_.polygons) {
            for (std::size_t c = 0; c < kCollisionPolygonCorners; ++c) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const float value = polygon.corner[c][axis];
                    if (first) {
                        report_.cost.corner_min[axis] = value;
                        report_.cost.corner_max[axis] = value;
                    } else {
                        report_.cost.corner_min[axis] =
                            std::min(report_.cost.corner_min[axis], value);
                        report_.cost.corner_max[axis] =
                            std::max(report_.cost.corner_max[axis], value);
                    }
                }
                first = false;
            }
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (report_.cost.corner_min[axis] < report_.cost.stored_min[axis] ||
                report_.cost.corner_max[axis] > report_.cost.stored_max[axis]) {
                issue(slot_[4].offset,
                      Message() << "the stored bounds on axis " << axis << " are "
                                << report_.cost.stored_min[axis] << " to "
                                << report_.cost.stored_max[axis]
                                << ", which does not contain the polygons' "
                                << report_.cost.corner_min[axis] << " to "
                                << report_.cost.corner_max[axis]);
                return false;
            }
        }
        return true;
    }

    const unsigned char* data_;
    std::uint64_t size_;
    std::uint64_t base_;
    TrackCollisionLimits limits_;
    TrackCollisionReport report_;
    bool found_[kSlotCount] = {false, false, false, false, false};
    RawChunk slot_[kSlotCount];
};

// ---------------------------------------------------------------------
// Query helpers
// ---------------------------------------------------------------------

bool finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

struct CellWalk {
    const TrackCollisionReport* report = nullptr;
    float min0 = 0.0F;
    float min1 = 0.0F;
    float max0 = 0.0F;
    float max1 = 0.0F;
    std::size_t budget = 0;
    std::vector<std::size_t>* out = nullptr;

    bool run() {
        struct Frame {
            std::size_t node;
            std::uint32_t depth;
            float center[2];
            float half;
        };
        std::vector<Frame> stack;
        Frame root;
        root.node = 0;
        root.depth = 0;
        root.center[0] = 0.0F;
        root.center[1] = 0.0F;
        root.half = kCollisionRootHalfExtent;
        stack.push_back(root);
        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();
            const TrackCollisionTreeNode& node = report->nodes[frame.node];
            const float quarter = frame.half * 0.5F;
            for (std::size_t slot = 0; slot < kCollisionTreeNodeEntries; ++slot) {
                const std::uint16_t entry = node.entries[slot];
                if (entry == 0) {
                    continue;
                }
                const float c0 =
                    frame.center[0] + ((slot & 1U) != 0 ? quarter : -quarter);
                const float c1 =
                    frame.center[1] + ((slot & 2U) != 0 ? quarter : -quarter);
                if (c0 + quarter < min0 || c0 - quarter > max0 ||
                    c1 + quarter < min1 || c1 - quarter > max1) {
                    continue;
                }
                if (frame.depth + 1 < kCollisionTreeDepth) {
                    // parse_track_collision proved this entry is in range
                    // before it set valid. The bound is repeated here
                    // because a caller may hand this function a report it
                    // built itself, and an out-of-range index would be an
                    // out-of-bounds read rather than a wrong answer.
                    if (entry >= report->nodes.size()) {
                        return false;
                    }
                    Frame next;
                    next.node = entry;
                    next.depth = frame.depth + 1;
                    next.center[0] = c0;
                    next.center[1] = c1;
                    next.half = quarter;
                    stack.push_back(next);
                    continue;
                }
                if (entry >= report->cells.size()) {
                    return false;
                }
                if (out->size() >= budget) {
                    return false;
                }
                out->push_back(entry);
            }
        }
        return true;
    }
};

bool ray_triangle(
    const float origin[3],
    const float direction[3],
    const float a[3],
    const float b[3],
    const float c[3],
    float max_distance,
    float& distance) {
    const float edge1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float edge2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float pvec[3] = {
        direction[1] * edge2[2] - direction[2] * edge2[1],
        direction[2] * edge2[0] - direction[0] * edge2[2],
        direction[0] * edge2[1] - direction[1] * edge2[0]};
    const float det =
        edge1[0] * pvec[0] + edge1[1] * pvec[1] + edge1[2] * pvec[2];
    // A degenerate triangle and a ray in the triangle's plane both land
    // here, and neither is a hit. The polygons are two-sided, so the sign
    // of the determinant is not used to cull.
    if (!(std::fabs(det) > 0.0F)) {
        return false;
    }
    const float inv = 1.0F / det;
    const float tvec[3] = {
        origin[0] - a[0], origin[1] - a[1], origin[2] - a[2]};
    const float u =
        (tvec[0] * pvec[0] + tvec[1] * pvec[1] + tvec[2] * pvec[2]) * inv;
    if (u < 0.0F || u > 1.0F) {
        return false;
    }
    const float qvec[3] = {
        tvec[1] * edge1[2] - tvec[2] * edge1[1],
        tvec[2] * edge1[0] - tvec[0] * edge1[2],
        tvec[0] * edge1[1] - tvec[1] * edge1[0]};
    const float v = (direction[0] * qvec[0] + direction[1] * qvec[1] +
                     direction[2] * qvec[2]) *
                    inv;
    if (v < 0.0F || u + v > 1.0F) {
        return false;
    }
    const float t =
        (edge2[0] * qvec[0] + edge2[1] * qvec[1] + edge2[2] * qvec[2]) * inv;
    if (!std::isfinite(t) || t < 0.0F || t > max_distance) {
        return false;
    }
    distance = t;
    return true;
}

// The eight bytes every JDLZ image starts with. The decoder itself is
// nfsu::jdlz_decompress; this only decides whether a file carries one.
constexpr unsigned char kJdlzSignature[8] = {'J', 'D', 'L', 'Z',
                                             0x02, 0x10, 0x00, 0x00};

bool looks_like_jdlz(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < kJdlzHeaderSize) {
        return false;
    }
    return std::memcmp(bytes.data(), kJdlzSignature, sizeof(kJdlzSignature)) == 0;
}

} // namespace

TrackCollisionReport parse_track_collision(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackCollisionLimits& limits) {
    if (data == nullptr && size != 0) {
        TrackCollisionReport report;
        TrackIssue entry;
        entry.offset = base_offset;
        entry.message = "collision image pointer is null";
        report.issues.push_back(std::move(entry));
        return report;
    }
    CollisionParser parser(data, static_cast<std::uint64_t>(size), base_offset,
                           limits);
    return parser.run();
}

TrackCollisionReport load_track_collision(
    const std::filesystem::path& path,
    std::vector<unsigned char>& bytes,
    const TrackCollisionLimits& limits) {
    TrackCollisionReport report;
    bytes.clear();

    std::error_code code;
    const std::uintmax_t file_size = std::filesystem::file_size(path, code);
    if (code) {
        TrackIssue entry;
        entry.offset = 0;
        entry.message = "cannot read the size of " + path.string() + ": " +
                        code.message();
        report.issues.push_back(std::move(entry));
        return report;
    }
    report.file_size = static_cast<std::uint64_t>(file_size);
    if (static_cast<std::uint64_t>(file_size) > limits.max_packed_bytes) {
        TrackIssue entry;
        entry.offset = 0;
        Message text;
        text << path.string() << " is " << file_size
             << " bytes, over the limit of " << limits.max_packed_bytes;
        entry.message = text.str();
        report.issues.push_back(std::move(entry));
        return report;
    }

    std::vector<unsigned char> packed(static_cast<std::size_t>(file_size));
    if (file_size != 0) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            TrackIssue entry;
            entry.offset = 0;
            entry.message = "cannot open " + path.string();
            report.issues.push_back(std::move(entry));
            return report;
        }
        stream.read(reinterpret_cast<char*>(packed.data()),
                    static_cast<std::streamsize>(packed.size()));
        if (!stream) {
            TrackIssue entry;
            entry.offset = 0;
            entry.message = "cannot read " + path.string();
            report.issues.push_back(std::move(entry));
            return report;
        }
    }

    std::uint64_t unused_packed = 0;
    if (looks_like_jdlz(packed)) {
        std::string error;
        std::size_t unused_input = 0;
        const std::uint64_t cap =
            std::min<std::uint64_t>(limits.max_region_bytes,
                                    std::numeric_limits<std::uint32_t>::max());
        if (!jdlz_decompress(packed.data(), packed.size(),
                             static_cast<std::uint32_t>(cap), bytes,
                             unused_input, error)) {
            TrackIssue entry;
            entry.offset = 0;
            entry.message = path.string() + ": " + error;
            report.issues.push_back(std::move(entry));
            return report;
        }
        if (unused_input > kCollisionMaxUnusedPackedBytes) {
            // Up to two flag bytes are what a decompressor that stops on a
            // full output leaves behind. More than that is packed data
            // this decoder did not account for, and unaccounted data is a
            // refusal here for the same reason a trailing chunk fragment
            // is.
            TrackIssue entry;
            entry.offset = 0;
            Message text;
            text << path.string() << ": the JDLZ stream left " << unused_input
                 << " packed bytes unused, more than the "
                 << kCollisionMaxUnusedPackedBytes << " a flag byte pair needs";
            entry.message = text.str();
            report.issues.push_back(std::move(entry));
            return report;
        }
        unused_packed = unused_input;
    } else {
        bytes = packed;
    }

    TrackCollisionReport parsed =
        parse_track_collision(bytes.data(), bytes.size(), 0, limits);
    parsed.file_size = report.file_size;
    parsed.cost.packed_bytes = static_cast<std::uint64_t>(file_size);
    parsed.cost.unused_packed_bytes = unused_packed;
    return parsed;
}

bool query_track_collision_cells(
    const TrackCollisionReport& report,
    float min_slot0,
    float min_slot1,
    float max_slot0,
    float max_slot1,
    std::vector<std::size_t>& out,
    const TrackCollisionQueryLimits& limits) {
    out.clear();
    if (!report.valid || report.nodes.empty()) {
        return false;
    }
    if (!std::isfinite(min_slot0) || !std::isfinite(min_slot1) ||
        !std::isfinite(max_slot0) || !std::isfinite(max_slot1)) {
        return false;
    }
    if (min_slot0 > max_slot0 || min_slot1 > max_slot1) {
        return false;
    }
    if (limits.max_cells == 0) {
        return false;
    }
    CellWalk walk;
    walk.report = &report;
    walk.min0 = min_slot0;
    walk.min1 = min_slot1;
    walk.max0 = max_slot0;
    walk.max1 = max_slot1;
    walk.budget = limits.max_cells;
    walk.out = &out;
    if (!walk.run()) {
        out.clear();
        return false;
    }
    return true;
}

bool query_track_collision_box(
    const TrackCollisionReport& report,
    const float min[3],
    const float max[3],
    std::vector<std::size_t>& out,
    const TrackCollisionQueryLimits& limits) {
    out.clear();
    if (min == nullptr || max == nullptr) {
        return false;
    }
    if (!finite3(min) || !finite3(max)) {
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (min[axis] > max[axis]) {
            return false;
        }
    }
    std::vector<std::size_t> cells;
    if (!query_track_collision_cells(report, min[0], min[1], max[0], max[1],
                                     cells, limits)) {
        return false;
    }
    std::vector<bool> seen(report.polygons.size(), false);
    for (std::size_t cell_index : cells) {
        const TrackCollisionCell& cell = report.cells[cell_index];
        // Bounds the parser already proved, repeated for the same reason
        // as in CellWalk: this is a public entry point.
        if (static_cast<std::uint64_t>(cell.first_index) + cell.index_count >
            report.indices.size()) {
            out.clear();
            return false;
        }
        for (std::uint32_t k = 0; k < cell.index_count; ++k) {
            const std::uint32_t polygon_index =
                report.indices[static_cast<std::size_t>(cell.first_index) + k];
            if (polygon_index >= report.polygons.size()) {
                out.clear();
                return false;
            }
            if (seen[polygon_index]) {
                continue;
            }
            const TrackCollisionPolygon& polygon = report.polygons[polygon_index];
            bool overlaps = true;
            for (std::size_t axis = 0; axis < 3 && overlaps; ++axis) {
                float low = polygon.corner[0][axis];
                float high = low;
                for (std::size_t c = 1; c < kCollisionPolygonCorners; ++c) {
                    low = std::min(low, polygon.corner[c][axis]);
                    high = std::max(high, polygon.corner[c][axis]);
                }
                overlaps = !(high < min[axis] || low > max[axis]);
            }
            if (!overlaps) {
                continue;
            }
            seen[polygon_index] = true;
            if (out.size() >= limits.max_results) {
                out.clear();
                return false;
            }
            out.push_back(polygon_index);
        }
    }
    std::sort(out.begin(), out.end());
    return true;
}

bool query_track_collision_ray(
    const TrackCollisionReport& report,
    const float origin[3],
    const float direction[3],
    float max_distance,
    TrackCollisionHit& hit,
    const TrackCollisionQueryLimits& limits) {
    if (origin == nullptr || direction == nullptr) {
        return false;
    }
    if (!finite3(origin) || !finite3(direction) ||
        !std::isfinite(max_distance) || !(max_distance > 0.0F)) {
        return false;
    }
    const float length_squared = direction[0] * direction[0] +
                                 direction[1] * direction[1] +
                                 direction[2] * direction[2];
    if (!(length_squared > 0.0F)) {
        return false;
    }
    // The cell grid is two-dimensional, so the swept box of the bounded ray
    // is what selects the cells and the triangle test does the rest.
    float min_slot[2];
    float max_slot[2];
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const float other = origin[axis] + direction[axis] * max_distance;
        min_slot[axis] = std::min(origin[axis], other);
        max_slot[axis] = std::max(origin[axis], other);
        if (!std::isfinite(min_slot[axis]) || !std::isfinite(max_slot[axis])) {
            return false;
        }
    }
    std::vector<std::size_t> cells;
    if (!query_track_collision_cells(report, min_slot[0], min_slot[1],
                                     max_slot[0], max_slot[1], cells, limits)) {
        return false;
    }

    bool found = false;
    TrackCollisionHit best;
    float best_distance = max_distance;
    for (std::size_t cell_index : cells) {
        const TrackCollisionCell& cell = report.cells[cell_index];
        if (static_cast<std::uint64_t>(cell.first_index) + cell.index_count >
            report.indices.size()) {
            return false;
        }
        for (std::uint32_t k = 0; k < cell.index_count; ++k) {
            const std::uint32_t polygon_index =
                report.indices[static_cast<std::size_t>(cell.first_index) + k];
            if (polygon_index >= report.polygons.size()) {
                return false;
            }
            const TrackCollisionPolygon& polygon = report.polygons[polygon_index];
            static const std::size_t kTriangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (std::size_t t = 0; t < 2; ++t) {
                float distance = 0.0F;
                if (!ray_triangle(origin, direction,
                                  polygon.corner[kTriangles[t][0]],
                                  polygon.corner[kTriangles[t][1]],
                                  polygon.corner[kTriangles[t][2]],
                                  best_distance, distance)) {
                    continue;
                }
                // A tie between two polygons is broken by the lower index,
                // so the same ray answers the same way on every run.
                if (found && !(distance < best_distance) &&
                    !(distance == best_distance && polygon_index < best.polygon)) {
                    continue;
                }
                found = true;
                best_distance = distance;
                best.polygon = polygon_index;
                best.triangle = t;
                best.distance = distance;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    best.point[axis] = origin[axis] + direction[axis] * distance;
                }
            }
        }
    }
    if (!found) {
        return false;
    }
    hit = best;
    return true;
}

} // namespace nfsu
