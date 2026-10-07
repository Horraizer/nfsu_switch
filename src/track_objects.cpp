#include "nfsu/track_objects.h"

#include "nfsu/chunk.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float32 required");
static_assert(sizeof(float) == 4, "32-bit float required");

constexpr std::uint64_t kHeaderSize = 8;

static_assert(kScenerySectionNumberOffset + 4 <= kSceneryHeaderRecordSize,
              "the section number lies inside the header record");
static_assert(kSceneryObjectRadiusOffset + 4 <= kSceneryObjectRecordSize,
              "the radius lies inside the object record");
static_assert(kSceneryInstancePaddingOffset + 2 == kSceneryInstanceRecordSize,
              "the padding ends the instance record");
static_assert(kSceneryInstanceMatrixOffset + 2 * kSceneryInstanceMatrixWords ==
                  kSceneryInstancePaddingOffset,
              "the matrix ends where the padding starts");
static_assert(kSceneryBoundNodeEntryOffset + 2 * kSceneryBoundNodeEntries ==
                  kSceneryBoundNodeRecordSize,
              "the entry slots end the bounding node record");

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
// src/track.cpp: two runs over the same bytes have to produce the same
// text.
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

std::string chunk_label(const RawChunk& chunk) {
    Message text;
    text << "chunk " << format_chunk_id(chunk.id) << " at offset "
         << chunk.offset;
    return text.str();
}

class TrackObjectParser {
public:
    TrackObjectParser(
        const unsigned char* data,
        std::uint64_t size,
        std::uint64_t base,
        const TrackObjectLimits& limits,
        TrackObjectReport& report)
        : data_(data),
          size_(size),
          base_(base),
          end_(base + size),
          limits_(limits),
          report_(report) {}

    void run() {
        if (size_ < kHeaderSize) {
            Message text;
            text << "region is " << size_ << " bytes; a chunk header needs "
                 << kHeaderSize;
            add_issue(base_, text.str());
            return;
        }

        std::vector<RawChunk> top;
        list_children(base_, end_, "region", top);

        bool found = false;
        for (const RawChunk& chunk : top) {
            if (chunk.id != kScenerySectionId) {
                continue;
            }
            found = true;
            if (report_.sections.size() >= limits_.max_sections) {
                Message text;
                text << "section limit " << limits_.max_sections
                     << " exceeded; remaining sections ignored";
                add_issue(chunk.offset, text.str());
                break;
            }
            parse_section(chunk);
        }
        if (!found) {
            add_issue(base_, "no scenery section chunk " +
                                 format_chunk_id(kScenerySectionId) +
                                 " in the region");
        }
        summarize();
    }

private:
    const unsigned char* at(std::uint64_t absolute) const {
        return data_ + (absolute - base_);
    }

    void add_issue(std::uint64_t offset, std::string message) {
        report_.issues.push_back(TrackIssue{offset, std::move(message)});
    }

    bool list_children(
        std::uint64_t begin,
        std::uint64_t end,
        const std::string& context,
        std::vector<RawChunk>& out) {
        if (begin < base_ || begin > end || end > end_) {
            add_issue(begin, context + ": invalid chunk range");
            return false;
        }

        std::uint64_t offset = begin;
        while (offset < end) {
            if (end - offset < kHeaderSize) {
                Message text;
                text << context << ": " << (end - offset)
                     << " trailing bytes are smaller than a chunk header";
                add_issue(offset, text.str());
                return false;
            }
            if (report_.cost.chunks >= limits_.max_chunks) {
                Message text;
                text << context << ": chunk limit " << limits_.max_chunks
                     << " exceeded";
                add_issue(offset, text.str());
                return false;
            }

            RawChunk chunk;
            chunk.offset = offset;
            chunk.id = read_u32_le(at(offset));
            chunk.payload_size = read_u32_le(at(offset) + 4);
            chunk.payload_offset = offset + kHeaderSize;
            if (chunk.payload_size > end - chunk.payload_offset) {
                Message text;
                text << context << ": " << chunk_label(chunk) << " declares "
                     << chunk.payload_size << " payload bytes but only "
                     << (end - chunk.payload_offset) << " remain in its parent";
                add_issue(offset, text.str());
                return false;
            }

            ++report_.cost.chunks;
            out.push_back(chunk);
            offset = chunk.payload_offset + chunk.payload_size;
        }
        return true;
    }

    // Every byte between a payload and its aligned record must be the
    // filler the retail writer uses. Positions are computed, never found by
    // scanning for the filler.
    bool check_filler(
        std::uint64_t from,
        std::uint64_t to,
        const std::string& context) {
        for (std::uint64_t offset = from; offset < to; ++offset) {
            const unsigned char byte = *at(offset);
            if (byte != kSceneryFillerByte) {
                Message text;
                text << context << ": alignment filler byte at offset "
                     << offset << " is " << hex_byte(byte) << "; expected "
                     << hex_byte(kSceneryFillerByte);
                add_issue(offset, text.str());
                return false;
            }
        }
        return true;
    }

    // Resolves the start of a record array that the game aligns, and the
    // number of whole records between it and the end of the payload.
    bool aligned_array(
        const RawChunk& chunk,
        std::uint64_t alignment,
        std::uint64_t record_size,
        const std::string& context,
        std::uint64_t& data_offset,
        std::uint64_t& count) {
        const std::uint64_t payload_end =
            chunk.payload_offset + chunk.payload_size;
        if (!align_up(chunk.payload_offset, alignment, data_offset) ||
            data_offset > payload_end) {
            Message text;
            text << context << ": the " << alignment
                 << "-byte aligned record array starts past the end of "
                 << chunk_label(chunk);
            add_issue(chunk.offset, text.str());
            return false;
        }
        if (!check_filler(chunk.payload_offset, data_offset, context)) {
            return false;
        }
        const std::uint64_t data_size = payload_end - data_offset;
        if (record_size == 0 || data_size % record_size != 0) {
            Message text;
            text << context << ": " << data_size << " bytes after the "
                 << (data_offset - chunk.payload_offset)
                 << "-byte alignment prefix are not a whole number of "
                 << record_size << "-byte records";
            add_issue(chunk.offset, text.str());
            return false;
        }
        count = data_size / record_size;
        return true;
    }

    void parse_section(const RawChunk& section_chunk) {
        TrackSection section;
        section.index = report_.sections.size();
        section.offset = section_chunk.offset;

        const std::string context =
            "scenery section at offset " + std::to_string(section_chunk.offset);

        std::vector<RawChunk> children;
        const bool structure_ok =
            list_children(section_chunk.payload_offset,
                          section_chunk.payload_offset +
                              section_chunk.payload_size,
                          context, children);

        // Exactly one of each of the four known chunks, and nothing else.
        // speed.exe walks this container once and ignores what it does not
        // know; a decoder that did the same would silently drop a chunk it
        // has never seen, so an unknown id is refused instead.
        const RawChunk* header = nullptr;
        const RawChunk* objects = nullptr;
        const RawChunk* instances = nullptr;
        const RawChunk* tree = nullptr;
        bool shape_ok = structure_ok;
        for (const RawChunk& child : children) {
            const RawChunk** slot = nullptr;
            if (child.id == kSceneryHeaderId) {
                slot = &header;
            } else if (child.id == kSceneryObjectListId) {
                slot = &objects;
            } else if (child.id == kSceneryInstanceListId) {
                slot = &instances;
            } else if (child.id == kSceneryBoundTreeId) {
                slot = &tree;
            } else {
                add_issue(child.offset,
                          context + ": unexpected " + chunk_label(child));
                shape_ok = false;
                continue;
            }
            if (*slot != nullptr) {
                add_issue(child.offset,
                          context + ": a second " + chunk_label(child));
                shape_ok = false;
                continue;
            }
            *slot = &child;
        }
        struct Required {
            const RawChunk* chunk;
            std::uint32_t id;
        };
        const Required required[] = {{header, kSceneryHeaderId},
                                     {objects, kSceneryObjectListId},
                                     {instances, kSceneryInstanceListId},
                                     {tree, kSceneryBoundTreeId}};
        for (const Required& entry : required) {
            if (entry.chunk == nullptr) {
                add_issue(section_chunk.offset,
                          context + ": no " + format_chunk_id(entry.id) +
                              " chunk");
                shape_ok = false;
            }
        }
        if (!shape_ok) {
            report_.sections.push_back(std::move(section));
            return;
        }

        if (!parse_header(*header, context, section) ||
            !parse_objects(*objects, context, section) ||
            !parse_instances(*instances, context, section) ||
            !parse_tree(*tree, context, section)) {
            section.objects.clear();
            section.instances.clear();
            section.nodes.clear();
            report_.sections.push_back(std::move(section));
            return;
        }

        section.valid = true;
        report_.sections.push_back(std::move(section));
    }

    bool parse_header(
        const RawChunk& chunk,
        const std::string& context,
        TrackSection& section) {
        std::uint64_t record = 0;
        if (!align_up(chunk.payload_offset, kSceneryRecordAlignment, record)) {
            add_issue(chunk.offset, context + ": header alignment overflows");
            return false;
        }
        const std::uint64_t payload_end =
            chunk.payload_offset + chunk.payload_size;
        std::uint64_t record_end = 0;
        if (!checked_add(record, kSceneryHeaderRecordSize, record_end) ||
            record_end > payload_end) {
            Message text;
            text << context << ": " << chunk_label(chunk) << " holds "
                 << chunk.payload_size << " bytes, too few for a "
                 << kSceneryHeaderRecordSize << "-byte record at offset "
                 << record;
            add_issue(chunk.offset, text.str());
            return false;
        }
        // The record ends the payload in all 593 retail sections. Ignoring
        // whatever follows would let a chunk carry bytes this decoder has
        // never seen, which is exactly what refusing an unknown chunk id is
        // meant to prevent.
        if (record_end != payload_end) {
            Message text;
            text << context << ": " << chunk_label(chunk) << " holds "
                 << (payload_end - record_end) << " bytes after its "
                 << kSceneryHeaderRecordSize << "-byte record at offset "
                 << record;
            add_issue(chunk.offset, text.str());
            return false;
        }
        if (!check_filler(chunk.payload_offset, record, context)) {
            return false;
        }
        section.header_record_offset = record;
        section.header_prefix_bytes = record - chunk.payload_offset;
        section.number = read_u32_le(at(record + kScenerySectionNumberOffset));
        section.has_letter = track_section_letter(section.number, section.letter);
        return true;
    }

    // The running totals of TrackObjectLimits. A caller that caps a parse
    // of untrusted bytes gets the cap it asked for: the count is added
    // before the array is reserved, so the limit bounds what is allocated
    // rather than reporting it afterwards.
    bool take_region_total(
        std::uint64_t& total,
        std::uint64_t count,
        std::uint64_t limit,
        const char* what,
        const std::string& context,
        std::uint64_t offset) {
        std::uint64_t next = 0;
        if (!checked_add(total, count, next) || next > limit) {
            Message text;
            text << context << ": " << count << " " << what << " after "
                 << total << " already in the region exceed the limit of "
                 << limit;
            add_issue(offset, text.str());
            return false;
        }
        total = next;
        return true;
    }

    bool parse_objects(
        const RawChunk& chunk,
        const std::string& context,
        TrackSection& section) {
        // The object directory is taken at the payload itself, so it has no
        // alignment prefix to check.
        if (chunk.payload_size % kSceneryObjectRecordSize != 0) {
            Message text;
            text << context << ": " << chunk_label(chunk) << " holds "
                 << chunk.payload_size << " bytes, not a whole number of "
                 << kSceneryObjectRecordSize << "-byte object records";
            add_issue(chunk.offset, text.str());
            return false;
        }
        const std::uint64_t count =
            chunk.payload_size / kSceneryObjectRecordSize;
        if (count == 0) {
            add_issue(chunk.offset,
                      context + ": the object directory is empty");
            return false;
        }
        if (count > limits_.max_objects_per_section) {
            Message text;
            text << context << ": " << count << " objects exceed the limit of "
                 << limits_.max_objects_per_section;
            add_issue(chunk.offset, text.str());
            return false;
        }
        if (!take_region_total(region_objects_, count, limits_.max_objects,
                               "objects", context, chunk.offset)) {
            return false;
        }

        section.object_list_offset = chunk.payload_offset;
        section.objects.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t offset =
                chunk.payload_offset + i * kSceneryObjectRecordSize;
            TrackObject object;
            object.index = static_cast<std::size_t>(i);
            object.offset = offset;
            for (std::size_t slot = 0; slot < kSceneryObjectMeshSlots; ++slot) {
                object.mesh_hash[slot] =
                    read_u32_le(at(offset + 4 * static_cast<std::uint64_t>(slot)));
            }
            if (object.mesh_hash[0] == 0) {
                Message text;
                text << context << ": object " << i
                     << " names no mesh in slot 0";
                add_issue(offset, text.str());
                return false;
            }
            object.flags = read_u32_le(at(offset + kSceneryObjectFlagsOffset));
            object.flags2 = read_u32_le(at(offset + kSceneryObjectFlags2Offset));
            object.radius = read_f32_le(at(offset + kSceneryObjectRadiusOffset));
            if (!std::isfinite(object.radius)) {
                Message text;
                text << context << ": object " << i
                     << " has a radius that is not finite";
                add_issue(offset + kSceneryObjectRadiusOffset, text.str());
                return false;
            }
            section.objects.push_back(object);
        }
        return true;
    }

    bool parse_instances(
        const RawChunk& chunk,
        const std::string& context,
        TrackSection& section) {
        std::uint64_t data = 0;
        std::uint64_t count = 0;
        if (!aligned_array(chunk, kSceneryRecordAlignment,
                           kSceneryInstanceRecordSize, context, data, count)) {
            return false;
        }
        if (count == 0) {
            add_issue(chunk.offset, context + ": the instance array is empty");
            return false;
        }
        if (count > limits_.max_instances_per_section) {
            Message text;
            text << context << ": " << count
                 << " instances exceed the limit of "
                 << limits_.max_instances_per_section;
            add_issue(chunk.offset, text.str());
            return false;
        }
        if (!take_region_total(region_instances_, count, limits_.max_instances,
                               "instances", context, chunk.offset)) {
            return false;
        }

        section.instance_list_offset = data;
        section.instance_prefix_bytes = data - chunk.payload_offset;
        section.instances.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t offset = data + i * kSceneryInstanceRecordSize;
            TrackInstance instance;
            instance.index = static_cast<std::size_t>(i);
            instance.offset = offset;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const std::uint64_t step = 2 * static_cast<std::uint64_t>(axis);
                instance.bounds_min[axis] = read_i16_le(
                    at(offset + kSceneryInstanceBoundsMinOffset + step));
                instance.bounds_max[axis] = read_i16_le(
                    at(offset + kSceneryInstanceBoundsMaxOffset + step));
                if (instance.bounds_min[axis] > instance.bounds_max[axis]) {
                    Message text;
                    text << context << ": instance " << i << " has slot "
                         << axis << " bounds " << instance.bounds_min[axis]
                         << " .. " << instance.bounds_max[axis]
                         << ", which is empty";
                    add_issue(offset, text.str());
                    return false;
                }
            }
            instance.object_index =
                read_u16_le(at(offset + kSceneryInstanceObjectIndexOffset));
            if (instance.object_index >= section.objects.size()) {
                Message text;
                text << context << ": instance " << i << " names object "
                     << instance.object_index << "; the directory holds "
                     << section.objects.size();
                add_issue(offset + kSceneryInstanceObjectIndexOffset,
                          text.str());
                return false;
            }
            instance.flags = read_u16_le(at(offset + kSceneryInstanceFlagsOffset));
            for (std::size_t axis = 0; axis < 3; ++axis) {
                instance.position[axis] = read_f32_le(
                    at(offset + kSceneryInstancePositionOffset +
                       4 * static_cast<std::uint64_t>(axis)));
                if (!std::isfinite(instance.position[axis])) {
                    Message text;
                    text << context << ": instance " << i << " has a position "
                         << "slot that is not finite";
                    add_issue(offset + kSceneryInstancePositionOffset,
                              text.str());
                    return false;
                }
            }
            for (std::size_t word = 0; word < kSceneryInstanceMatrixWords;
                 ++word) {
                instance.matrix[word] = read_i16_le(
                    at(offset + kSceneryInstanceMatrixOffset +
                       2 * static_cast<std::uint64_t>(word)));
            }
            const std::int16_t padding =
                read_i16_le(at(offset + kSceneryInstancePaddingOffset));
            if (padding != 0) {
                Message text;
                text << context << ": instance " << i
                     << " has a non-zero padding word after its matrix";
                add_issue(offset + kSceneryInstancePaddingOffset, text.str());
                return false;
            }
            section.instances.push_back(instance);
        }
        return true;
    }

    bool parse_tree(
        const RawChunk& chunk,
        const std::string& context,
        TrackSection& section) {
        if (chunk.payload_size % kSceneryBoundNodeRecordSize != 0) {
            Message text;
            text << context << ": " << chunk_label(chunk) << " holds "
                 << chunk.payload_size << " bytes, not a whole number of "
                 << kSceneryBoundNodeRecordSize << "-byte bounding nodes";
            add_issue(chunk.offset, text.str());
            return false;
        }
        const std::uint64_t count =
            chunk.payload_size / kSceneryBoundNodeRecordSize;
        if (count == 0) {
            add_issue(chunk.offset, context + ": the bounding tree is empty");
            return false;
        }
        if (count > limits_.max_nodes_per_section) {
            Message text;
            text << context << ": " << count
                 << " bounding nodes exceed the limit of "
                 << limits_.max_nodes_per_section;
            add_issue(chunk.offset, text.str());
            return false;
        }
        if (!take_region_total(region_bound_nodes_, count,
                               limits_.max_bound_nodes, "bounding nodes",
                               context, chunk.offset)) {
            return false;
        }

        section.bound_tree_offset = chunk.payload_offset;
        section.nodes.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t offset =
                chunk.payload_offset + i * kSceneryBoundNodeRecordSize;
            TrackBoundNode node;
            node.index = static_cast<std::size_t>(i);
            node.offset = offset;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const std::uint64_t step = 2 * static_cast<std::uint64_t>(axis);
                node.bounds_min[axis] = read_i16_le(at(offset + step));
                node.bounds_max[axis] = read_i16_le(at(offset + 6 + step));
                if (node.bounds_min[axis] > node.bounds_max[axis]) {
                    Message text;
                    text << context << ": bounding node " << i << " has slot "
                         << axis << " bounds " << node.bounds_min[axis]
                         << " .. " << node.bounds_max[axis]
                         << ", which is empty";
                    add_issue(offset, text.str());
                    return false;
                }
            }
            node.entry_count =
                read_u16_le(at(offset + kSceneryBoundNodeEntryCountOffset));
            if (node.entry_count == 0 ||
                node.entry_count > kSceneryBoundNodeEntries) {
                Message text;
                text << context << ": bounding node " << i << " declares "
                     << node.entry_count << " entries; the record holds 1 to "
                     << kSceneryBoundNodeEntries;
                add_issue(offset + kSceneryBoundNodeEntryCountOffset,
                          text.str());
                return false;
            }
            for (std::size_t slot = 0; slot < kSceneryBoundNodeEntries; ++slot) {
                node.entries[slot] = read_i16_le(
                    at(offset + kSceneryBoundNodeEntryOffset +
                       2 * static_cast<std::uint64_t>(slot)));
            }
            section.nodes.push_back(node);
        }
        return validate_tree(context, section);
    }

    // The tree must be a tree: every node reached exactly once from the
    // root, every instance placed by exactly one leaf entry, and every
    // node's box containing the boxes below it. A shared node, a cycle and
    // an orphan all fail the "exactly once" rule, so none of them can reach
    // a caller that walks the tree to cull.
    bool validate_tree(const std::string& context, TrackSection& section) {
        const std::size_t node_count = section.nodes.size();
        const std::size_t instance_count = section.instances.size();
        std::vector<std::uint32_t> node_visits(node_count, 0);
        std::vector<std::uint32_t> instance_visits(instance_count, 0);

        struct Frame {
            std::size_t node = 0;
            std::size_t slot = 0;
            std::int32_t min[3] = {0, 0, 0};
            std::int32_t max[3] = {0, 0, 0};
        };
        std::vector<Frame> stack;
        Frame root;
        root.node = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            root.min[axis] = std::numeric_limits<std::int32_t>::max();
            root.max[axis] = std::numeric_limits<std::int32_t>::min();
        }
        node_visits[0] = 1;
        stack.push_back(root);
        std::uint32_t deepest = 0;

        while (!stack.empty()) {
            Frame& frame = stack.back();
            const TrackBoundNode& node = section.nodes[frame.node];
            if (frame.slot >= node.entry_count) {
                // Every child of this node has been folded in; check its
                // own box and hand the box it stores to its parent, which
                // is what a culler would walk.
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    if (node.bounds_min[axis] > frame.min[axis] ||
                        node.bounds_max[axis] < frame.max[axis]) {
                        Message text;
                        text << context << ": bounding node " << node.index
                             << " has slot " << axis << " bounds "
                             << node.bounds_min[axis] << " .. "
                             << node.bounds_max[axis]
                             << ", which does not contain " << frame.min[axis]
                             << " .. " << frame.max[axis] << " below it";
                        add_issue(node.offset, text.str());
                        return false;
                    }
                }
                stack.pop_back();
                if (stack.empty()) {
                    break;
                }
                Frame& parent = stack.back();
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    parent.min[axis] =
                        std::min<std::int32_t>(parent.min[axis],
                                               node.bounds_min[axis]);
                    parent.max[axis] =
                        std::max<std::int32_t>(parent.max[axis],
                                               node.bounds_max[axis]);
                }
                continue;
            }

            const std::int16_t entry = node.entries[frame.slot];
            ++frame.slot;
            if (entry < 0) {
                // -n is child node n; node 0 is the root and can never be a
                // child, so the negation is unambiguous.
                const std::int32_t child = -static_cast<std::int32_t>(entry);
                if (static_cast<std::size_t>(child) >= node_count) {
                    Message text;
                    text << context << ": bounding node " << node.index
                         << " names child node " << child << "; the tree holds "
                         << node_count;
                    add_issue(node.offset, text.str());
                    return false;
                }
                if (node_visits[static_cast<std::size_t>(child)] != 0) {
                    Message text;
                    text << context << ": bounding node " << node.index
                         << " names child node " << child
                         << ", which is already in the tree";
                    add_issue(node.offset, text.str());
                    return false;
                }
                if (stack.size() > limits_.max_tree_depth) {
                    Message text;
                    text << context << ": the bounding tree is deeper than "
                         << limits_.max_tree_depth;
                    add_issue(node.offset, text.str());
                    return false;
                }
                node_visits[static_cast<std::size_t>(child)] = 1;
                Frame next;
                next.node = static_cast<std::size_t>(child);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    next.min[axis] = std::numeric_limits<std::int32_t>::max();
                    next.max[axis] = std::numeric_limits<std::int32_t>::min();
                }
                stack.push_back(next);
                deepest = std::max<std::uint32_t>(
                    deepest, static_cast<std::uint32_t>(stack.size() - 1));
                continue;
            }

            const std::size_t leaf = static_cast<std::size_t>(entry);
            if (leaf >= instance_count) {
                Message text;
                text << context << ": bounding node " << node.index
                     << " names instance " << leaf << "; the section holds "
                     << instance_count;
                add_issue(node.offset, text.str());
                return false;
            }
            if (instance_visits[leaf] != 0) {
                Message text;
                text << context << ": bounding node " << node.index
                     << " names instance " << leaf
                     << ", which another node already holds";
                add_issue(node.offset, text.str());
                return false;
            }
            instance_visits[leaf] = 1;
            const TrackInstance& instance = section.instances[leaf];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                frame.min[axis] = std::min<std::int32_t>(frame.min[axis],
                                                         instance.bounds_min[axis]);
                frame.max[axis] = std::max<std::int32_t>(frame.max[axis],
                                                         instance.bounds_max[axis]);
            }
        }

        for (std::size_t i = 0; i < node_count; ++i) {
            if (node_visits[i] == 0) {
                Message text;
                text << context << ": bounding node " << i
                     << " is not reachable from the root";
                add_issue(section.nodes[i].offset, text.str());
                return false;
            }
        }
        for (std::size_t i = 0; i < instance_count; ++i) {
            if (instance_visits[i] == 0) {
                Message text;
                text << context << ": instance " << i
                     << " is in no bounding node";
                add_issue(section.instances[i].offset, text.str());
                return false;
            }
        }
        section.tree_depth = deepest;
        return true;
    }

    void summarize() {
        TrackObjectCost& cost = report_.cost;
        cost.region_bytes = size_;
        cost.sections = report_.sections.size();
        std::set<std::uint32_t> hashes;
        for (const TrackSection& section : report_.sections) {
            if (!section.valid) {
                continue;
            }
            ++cost.valid_sections;
            cost.objects += section.objects.size();
            cost.instances += section.instances.size();
            cost.bound_nodes += section.nodes.size();
            cost.largest_section_instances =
                std::max<std::uint64_t>(cost.largest_section_instances,
                                        section.instances.size());
            cost.deepest_tree =
                std::max<std::uint32_t>(cost.deepest_tree, section.tree_depth);
            for (const TrackObject& object : section.objects) {
                for (std::size_t slot = 0; slot < kSceneryObjectMeshSlots;
                     ++slot) {
                    if (object.mesh_hash[slot] == 0) {
                        continue;
                    }
                    ++cost.mesh_slots_used;
                    hashes.insert(object.mesh_hash[slot]);
                }
            }
            for (const TrackInstance& instance : section.instances) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const std::int32_t low = instance.bounds_min[axis];
                    const std::int32_t high = instance.bounds_max[axis];
                    if (!cost.has_bounds) {
                        cost.bounds_min[axis] = low;
                        cost.bounds_max[axis] = high;
                    } else {
                        cost.bounds_min[axis] =
                            std::min(cost.bounds_min[axis], low);
                        cost.bounds_max[axis] =
                            std::max(cost.bounds_max[axis], high);
                    }
                }
                cost.has_bounds = true;
            }
        }
        cost.distinct_mesh_hashes = hashes.size();
        cost.source_object_bytes = cost.objects * kSceneryObjectRecordSize;
        cost.source_instance_bytes = cost.instances * kSceneryInstanceRecordSize;
        cost.source_bound_node_bytes =
            cost.bound_nodes * kSceneryBoundNodeRecordSize;
    }

    const unsigned char* data_;
    std::uint64_t size_;
    std::uint64_t base_;
    std::uint64_t end_;
    TrackObjectLimits limits_;
    TrackObjectReport& report_;
    std::uint64_t region_objects_ = 0;
    std::uint64_t region_instances_ = 0;
    std::uint64_t region_bound_nodes_ = 0;
};

} // namespace

std::size_t TrackObjectReport::valid_section_count() const noexcept {
    std::size_t count = 0;
    for (const TrackSection& section : sections) {
        if (section.valid) {
            ++count;
        }
    }
    return count;
}

bool track_section_letter(std::uint32_t number, char& letter) noexcept {
    const std::uint32_t code = 0x40U + number / 100U;
    if (code < static_cast<std::uint32_t>('A') ||
        code > static_cast<std::uint32_t>('Z')) {
        return false;
    }
    letter = static_cast<char>(code);
    return true;
}

TrackTransform track_instance_transform(const TrackInstance& instance) noexcept {
    TrackTransform transform;
    for (std::size_t word = 0; word < kSceneryInstanceMatrixWords; ++word) {
        transform.linear[word] = static_cast<float>(instance.matrix[word]) /
                                 static_cast<float>(kSceneryMatrixOne);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        transform.translation[axis] = instance.position[axis];
    }
    return transform;
}

void track_transform_point(
    const TrackTransform& transform,
    const float local[3],
    float world[3]) noexcept {
    for (std::size_t column = 0; column < 3; ++column) {
        float value = transform.translation[column];
        for (std::size_t row = 0; row < 3; ++row) {
            value += local[row] * transform.linear[row * 3 + column];
        }
        world[column] = value;
    }
}

TrackObjectReport parse_track_objects(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackObjectLimits& limits) {
    TrackObjectReport report;
    report.region.offset = base_offset;
    report.region.size = size;
    if (data == nullptr || size == 0) {
        report.issues.push_back(TrackIssue{base_offset, "region is empty"});
        report.cost.region_bytes = size;
        return report;
    }
    std::uint64_t region_end = 0;
    if (!checked_add(base_offset, size, region_end)) {
        report.issues.push_back(
            TrackIssue{base_offset, "region end overflows a 64-bit offset"});
        report.cost.region_bytes = size;
        return report;
    }
    static_cast<void>(region_end);

    TrackObjectParser parser(data, size, base_offset, limits, report);
    parser.run();
    return report;
}

TrackObjectReport load_track_objects(
    const std::filesystem::path& path,
    const TrackRegionSelector& region,
    std::vector<unsigned char>& bytes,
    const TrackObjectLimits& limits) {
    bytes.clear();
    TrackObjectReport report;
    report.region = region;

    std::error_code size_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
        report.issues.push_back(
            TrackIssue{region.offset, "failed to read file size"});
        return report;
    }
    report.file_size = static_cast<std::uint64_t>(file_size);

    if (region.size == 0) {
        report.issues.push_back(TrackIssue{region.offset, "region size is zero"});
        return report;
    }
    if (region.size > limits.max_region_bytes) {
        Message text;
        text << "region is " << region.size << " bytes; limit is "
             << limits.max_region_bytes;
        report.issues.push_back(TrackIssue{region.offset, text.str()});
        return report;
    }
    if (region.offset > report.file_size ||
        region.size > report.file_size - region.offset) {
        Message text;
        text << "region at offset " << region.offset << " with " << region.size
             << " bytes is outside the " << report.file_size << "-byte file";
        report.issues.push_back(TrackIssue{region.offset, text.str()});
        return report;
    }
    if (region.size > std::numeric_limits<std::size_t>::max() ||
        region.offset > static_cast<std::uint64_t>(
                            std::numeric_limits<std::streamoff>::max())) {
        report.issues.push_back(
            TrackIssue{region.offset, "region is too large for this build"});
        return report;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        report.issues.push_back(TrackIssue{region.offset, "failed to open file"});
        return report;
    }
    stream.seekg(static_cast<std::streamoff>(region.offset), std::ios::beg);
    if (!stream) {
        report.issues.push_back(
            TrackIssue{region.offset, "failed to seek to the region"});
        return report;
    }

    try {
        bytes.resize(static_cast<std::size_t>(region.size));
    } catch (const std::exception&) {
        bytes.clear();
        report.issues.push_back(
            TrackIssue{region.offset, "out of memory while reading the region"});
        return report;
    }
    if (!stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        report.issues.push_back(
            TrackIssue{region.offset, "failed to read the whole region"});
        return report;
    }

    TrackObjectReport parsed =
        parse_track_objects(bytes.data(), bytes.size(), region.offset, limits);
    parsed.file_size = report.file_size;
    return parsed;
}

std::vector<std::uint32_t> track_object_mesh_hashes(
    const TrackObjectReport& report) {
    std::set<std::uint32_t> hashes;
    for (const TrackSection& section : report.sections) {
        if (!section.valid) {
            continue;
        }
        for (const TrackObject& object : section.objects) {
            for (std::size_t slot = 0; slot < kSceneryObjectMeshSlots; ++slot) {
                if (object.mesh_hash[slot] != 0) {
                    hashes.insert(object.mesh_hash[slot]);
                }
            }
        }
    }
    return std::vector<std::uint32_t>(hashes.begin(), hashes.end());
}

bool retain_instanced_track_meshes(TrackRegionReport& geometry,
                                  const TrackObjectReport& objects,
                                  std::string& error) {
    if (!geometry.ok() || !objects.ok()) {
        error = "cannot reduce invalid track geometry or objects"; return false;
    }
    std::set<std::uint32_t> wanted;
    for (const auto& section : objects.sections) {
        if (!section.valid) continue;
        for (const auto& instance : section.instances) {
            if (instance.object_index >= section.objects.size()) {
                error = "track instance object index outside section"; return false;
            }
            for (auto hash : section.objects[instance.object_index].mesh_hash)
                if (hash) wanted.insert(hash);
        }
    }
    geometry.meshes.erase(std::remove_if(geometry.meshes.begin(),geometry.meshes.end(),
        [&](const TrackMesh& mesh) { return wanted.count(mesh.mesh_hash)==0; }),geometry.meshes.end());
    for (std::size_t i=0; i<geometry.meshes.size(); ++i) geometry.meshes[i].index=i;
    error.clear(); return true;
}

TrackObjectResolution resolve_track_objects(
    const TrackObjectReport& objects,
    const TrackRegionReport& region) {
    TrackObjectResolution resolution;

    // The lowest valid mesh index per hash, so a region with two meshes of
    // the same name binds the same one on every run.
    std::map<std::uint32_t, std::size_t> meshes;
    for (std::size_t i = 0; i < region.meshes.size(); ++i) {
        const TrackMesh& mesh = region.meshes[i];
        if (!mesh.valid) {
            continue;
        }
        meshes.emplace(mesh.mesh_hash, i);
    }

    std::set<std::uint32_t> unresolved;
    for (const TrackSection& section : objects.sections) {
        if (!section.valid) {
            continue;
        }
        for (const TrackObject& object : section.objects) {
            for (std::size_t slot = 0; slot < kSceneryObjectMeshSlots; ++slot) {
                ++resolution.slots_total;
                const std::uint32_t hash = object.mesh_hash[slot];
                if (hash == 0) {
                    ++resolution.slots_empty;
                    continue;
                }
                TrackObjectBinding binding;
                binding.section_index = section.index;
                binding.object_index = object.index;
                binding.slot = slot;
                binding.hash = hash;
                const std::map<std::uint32_t, std::size_t>::const_iterator found =
                    meshes.find(hash);
                if (found == meshes.end()) {
                    ++resolution.slots_unresolved;
                    unresolved.insert(hash);
                } else {
                    binding.resolved = true;
                    binding.mesh_index = found->second;
                    ++resolution.slots_resolved;
                }
                resolution.bindings.push_back(binding);
            }
        }

        // The cross-check: the box an instance stores must hold the mesh it
        // places, after that instance's own transform. Only slot 0 is
        // checked, because it is the highest level of detail and the only
        // slot every retail object fills.
        for (const TrackInstance& instance : section.instances) {
            const TrackObject& object = section.objects[instance.object_index];
            const std::map<std::uint32_t, std::size_t>::const_iterator found =
                meshes.find(object.mesh_hash[0]);
            if (found == meshes.end()) {
                continue;
            }
            const TrackMesh& mesh = region.meshes[found->second];
            if (mesh.vertices.empty()) {
                continue;
            }
            const TrackTransform transform = track_instance_transform(instance);
            float low[3] = {0.0F, 0.0F, 0.0F};
            float high[3] = {0.0F, 0.0F, 0.0F};
            bool first = true;
            for (const TrackVertex& vertex : mesh.vertices) {
                // Back into the raw slot order the file stores, which is
                // the order the instance position, matrix and box use:
                // TrackVertex names file slot 1 pz and file slot 2 py.
                const float local[3] = {vertex.px, vertex.pz, vertex.py};
                float world[3] = {0.0F, 0.0F, 0.0F};
                track_transform_point(transform, local, world);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    if (first) {
                        low[axis] = world[axis];
                        high[axis] = world[axis];
                    } else {
                        low[axis] = std::min(low[axis], world[axis]);
                        high[axis] = std::max(high[axis], world[axis]);
                    }
                }
                first = false;
            }
            ++resolution.instances_checked;
            bool contained = true;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (low[axis] < static_cast<float>(instance.bounds_min[axis]) ||
                    high[axis] > static_cast<float>(instance.bounds_max[axis])) {
                    contained = false;
                }
                if (!resolution.has_bounds) {
                    resolution.bounds_min[axis] = low[axis];
                    resolution.bounds_max[axis] = high[axis];
                } else {
                    resolution.bounds_min[axis] =
                        std::min(resolution.bounds_min[axis], low[axis]);
                    resolution.bounds_max[axis] =
                        std::max(resolution.bounds_max[axis], high[axis]);
                }
            }
            resolution.has_bounds = true;
            if (contained) {
                ++resolution.instances_bounds_contained;
            }
        }
    }

    resolution.unresolved_hashes.assign(unresolved.begin(), unresolved.end());
    return resolution;
}

} // namespace nfsu
