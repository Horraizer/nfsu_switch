#pragma once

#include "nfsu/track.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

// Bounded decoding of the scenery sections of a track STREAM container:
// the per-section object directory, the instances that place those objects
// in the world, and the bounding tree that indexes the instances.
//
// docs/TRACK.md decodes the geometry a section draws. This header decodes
// what decides where that geometry goes. The two are separate chunks under
// separate top-level ids, so a caller may parse either alone; a region that
// holds both resolves an object's mesh hashes against the meshes of
// docs/TRACK.md through resolve_track_objects below.
//
// Nothing here scans a directory, follows a streaming table or reads a
// whole installation. A caller names one file, one absolute byte offset and
// one byte length, and exactly those bytes are read and parsed. See
// docs/TRACK_OBJECTS.md for the byte evidence and for what is still
// unknown.

// A scenery section. speed.exe 0x004457C0 accepts this container by id
// alone and walks its children once, so the four ids below are the whole
// format: anything else inside it is refused here rather than ignored.
constexpr std::uint32_t kScenerySectionId = 0x80034100U;
// 0x004457F7: the section header, whose aligned record becomes the runtime
// section object itself.
constexpr std::uint32_t kSceneryHeaderId = 0x00034101U;
// 0x00445859: the object directory.
constexpr std::uint32_t kSceneryObjectListId = 0x00034102U;
// 0x00445876: the instance array.
constexpr std::uint32_t kSceneryInstanceListId = 0x00034103U;
// 0x004458A3: the bounding tree over those instances.
constexpr std::uint32_t kSceneryBoundTreeId = 0x00034104U;

// 0x004457FE and 0x0044587D: lea reg, [chunk + 0x17] / and reg, 0xFFFFFFF0,
// with chunk the 8-byte header, so the header record and the instance array
// start at the first 16-byte boundary at or after their payload. The object
// directory and the bounding tree are taken at the payload itself
// (0x00445860 and 0x004458AA, lea reg, [chunk + 8]) and are never aligned.
constexpr std::uint64_t kSceneryRecordAlignment = 16;

// The filler between a payload and its aligned record is 0x11 in every
// retail section, the same byte the geometry chunks use. The game never
// inspects it; the parser checks it because a different filler would mean a
// different writer.
constexpr unsigned char kSceneryFillerByte = 0x11U;

// The header record. Its size is not stored: every retail payload ends
// exactly 60 bytes after the aligned record, and the runtime writes its own
// fields into +24 through +44, so the record cannot be shorter.
constexpr std::size_t kSceneryHeaderRecordSize = 60;
// +12. 0x00445807 stores the record's own address into the global table at
// 0x0072A008 indexed by this word, so it is the section number, not a count.
// 0x0044580E divides it by 100 and adds 0x40, which is the letter of the
// STREAML1R<letter>.BUN family the section belongs to.
constexpr std::size_t kScenerySectionNumberOffset = 12;

// 0x00445866: mul by 0x38E38E39 then shr 4, which is a division by 72.
constexpr std::size_t kSceneryObjectRecordSize = 72;
// 0x00445900 and 0x00445A74: the loop reads six hashes at +0, +4, +8, +12,
// +16 and +20 and stores the solid it resolved for each into the runtime
// pointer at +36 + 4 * slot.
constexpr std::size_t kSceneryObjectMeshSlots = 6;
// 0x00445A81 and 0x00445A8F: when slot 5 or slot 4 resolved nothing, the
// runtime fills it with the first solid that slots 0 to 3 resolved, so
// those four are the chain that decides the object and the last two only
// override it.
constexpr std::size_t kSceneryObjectChainSlots = 4;
constexpr std::size_t kSceneryObjectFlagsOffset = 24;
constexpr std::size_t kSceneryObjectFlags2Offset = 28;
constexpr std::size_t kSceneryObjectRadiusOffset = 68;

// 0x00445894: mul by 0xAAAAAAAB then shr 5, which is a division by 48.
constexpr std::size_t kSceneryInstanceRecordSize = 48;
constexpr std::size_t kSceneryInstanceBoundsMinOffset = 0;
constexpr std::size_t kSceneryInstanceBoundsMaxOffset = 6;
constexpr std::size_t kSceneryInstanceObjectIndexOffset = 12;
// +14. 0x00445B23 clears bit 0x10 of this word and 0x00445B36 sets it, so
// it is a mutable flags word and not part of the placement.
constexpr std::size_t kSceneryInstanceFlagsOffset = 14;
constexpr std::size_t kSceneryInstancePositionOffset = 16;
constexpr std::size_t kSceneryInstanceMatrixOffset = 28;
constexpr std::size_t kSceneryInstanceMatrixWords = 9;
// +46. The two bytes after the matrix are zero in all 29715 retail
// instances, so they are padding and the parser refuses anything else.
constexpr std::size_t kSceneryInstancePaddingOffset = 46;

// The int16 matrix is a fixed-point 3x3: this value is 1.0. It is the
// divisor the retail data proves, not one read from the file.
constexpr std::int32_t kSceneryMatrixOne = 8192;

// 0x004458B0: mul by 0xAAAAAAAB then shr 4, which is a division by 24.
constexpr std::size_t kSceneryBoundNodeRecordSize = 24;
constexpr std::size_t kSceneryBoundNodeEntryCountOffset = 12;
constexpr std::size_t kSceneryBoundNodeEntryOffset = 14;
// Five int16 entry slots follow the count, which is 1 to 5 in every retail
// node. A node with no entry would bound nothing, so zero is refused.
constexpr std::size_t kSceneryBoundNodeEntries = 5;

// Two mesh hashes speed.exe 0x0044590B and 0x00445917 skip without looking
// them up. The first is SKYDOME; the second names no mesh in the sampled
// installation. They are decoded like any other hash and reported, never
// resolved.
constexpr std::uint32_t kSkydomeMeshHash = 0xBE43EDBBU;
constexpr std::uint32_t kReservedMeshHash = 0x72923C81U;

// One 72-byte record of the object directory. An instance names an object
// by its position in this array, and the object names the meshes that may
// draw it.
struct TrackObject {
    std::size_t index = 0;
    // Absolute file offset of the record.
    std::uint64_t offset = 0;
    // +0, +4, +8, +12, +16 and +20. Slot 0 is set in all 9681 retail
    // objects; a zero slot means the runtime resolves nothing for it. The
    // hashes are the same 0x00134011 +16 mesh name hashes the geometry
    // chunks carry.
    std::uint32_t mesh_hash[kSceneryObjectMeshSlots] = {0, 0, 0, 0, 0, 0};
    // +24 and +28. Flag words, kept raw. No bit of either was traced to a
    // behaviour, so neither is named.
    std::uint32_t flags = 0;
    std::uint32_t flags2 = 0;
    // +68. Positive and finite in every retail object. The runtime reads it
    // as a float; what it measures was not reproduced here, so the name
    // follows its only plausible use and docs/TRACK_OBJECTS.md says so.
    float radius = 0.0F;
};

// One 48-byte record of the instance array: one placed copy of one object.
struct TrackInstance {
    std::size_t index = 0;
    std::uint64_t offset = 0;
    // +0 and +6, three int16 each. A conservative world-space box around
    // the placed object, in the same slot order as the position below. The
    // bounding tree bounds these boxes and nothing else.
    std::int16_t bounds_min[3] = {0, 0, 0};
    std::int16_t bounds_max[3] = {0, 0, 0};
    // +12, a u16. The position of this instance's object in the section's
    // object directory. Inside the directory in all 29715 retail instances.
    std::uint32_t object_index = 0;
    // +14, a u16. Bit 0x10 is set and cleared at runtime; the rest is kept
    // raw and unnamed.
    std::uint32_t flags = 0;
    // +16, three f32. The translation, in the same raw slot order as
    // TrackVertex, so slot 1 is pz and slot 2 is py.
    float position[3] = {0.0F, 0.0F, 0.0F};
    // +28, nine int16 in row-major order: the first three are the image of
    // the mesh's first slot, and kSceneryMatrixOne is 1.0. A mesh vertex v
    // reaches the world as v * matrix + position, which is what
    // track_instance_transform returns in float form.
    std::int16_t matrix[kSceneryInstanceMatrixWords] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
};

// One 24-byte node of the bounding tree. Node 0 is the root.
struct TrackBoundNode {
    std::size_t index = 0;
    std::uint64_t offset = 0;
    // +0 and +6. Contains the boxes of everything below the node.
    std::int16_t bounds_min[3] = {0, 0, 0};
    std::int16_t bounds_max[3] = {0, 0, 0};
    // +12, a u16 in 1..5, then five int16 at +14. A negative entry -n is
    // child node n; a non-negative entry is an instance index. Only the
    // first entry_count slots carry an entry.
    std::uint32_t entry_count = 0;
    std::int16_t entries[kSceneryBoundNodeEntries] = {0, 0, 0, 0, 0};
};

// One 0x80034100 chunk, decoded. A section that fails any rule is kept with
// valid == false and no decoded record, exactly as an invalid track mesh is.
struct TrackSection {
    std::size_t index = 0;
    // Absolute file offset of the 0x80034100 chunk header.
    std::uint64_t offset = 0;
    std::uint64_t header_record_offset = 0;
    std::uint64_t header_prefix_bytes = 0;
    std::uint64_t object_list_offset = 0;
    std::uint64_t instance_list_offset = 0;
    std::uint64_t instance_prefix_bytes = 0;
    std::uint64_t bound_tree_offset = 0;
    // The 0x00034101 +12 section number and the letter speed.exe derives
    // from it. has_letter is false when the division leaves the letter
    // outside 'A' to 'Z', which no retail section does.
    std::uint32_t number = 0;
    char letter = '\0';
    bool has_letter = false;
    std::vector<TrackObject> objects;
    std::vector<TrackInstance> instances;
    std::vector<TrackBoundNode> nodes;
    // Depth of the bounding tree, counting the root as 0.
    std::uint32_t tree_depth = 0;
    bool valid = false;
};

// Deterministic parse cost of one region's scenery sections. Every field is
// a byte or item count derived from the region alone.
struct TrackObjectCost {
    std::uint64_t region_bytes = 0;
    std::uint64_t chunks = 0;
    std::uint64_t sections = 0;
    std::uint64_t valid_sections = 0;
    // The counts below cover the valid sections only.
    std::uint64_t objects = 0;
    std::uint64_t instances = 0;
    std::uint64_t bound_nodes = 0;
    // Bytes as stored: objects * 72, instances * 48, nodes * 24.
    std::uint64_t source_object_bytes = 0;
    std::uint64_t source_instance_bytes = 0;
    std::uint64_t source_bound_node_bytes = 0;
    // Non-zero mesh slots over every object, and how many distinct hashes
    // they name.
    std::uint64_t mesh_slots_used = 0;
    std::uint64_t distinct_mesh_hashes = 0;
    std::uint64_t largest_section_instances = 0;
    std::uint32_t deepest_tree = 0;
    // Union of every valid instance's box, in the stored int16 units and
    // the stored slot order. Zero when the region places nothing.
    bool has_bounds = false;
    std::int32_t bounds_min[3] = {0, 0, 0};
    std::int32_t bounds_max[3] = {0, 0, 0};
};

struct TrackObjectLimits {
    std::uint64_t max_region_bytes = 64ULL * 1024ULL * 1024ULL;
    std::size_t max_chunks = 200000;
    std::size_t max_sections = 4096;
    // An instance names its object with a u16, so a longer directory holds
    // records no instance of the section can reach.
    std::uint32_t max_objects_per_section = 65536;
    // A bounding tree entry is an int16, so neither array can be longer
    // than the positive half of that range.
    std::uint32_t max_instances_per_section = 32768;
    std::uint32_t max_nodes_per_section = 32768;
    // Running totals over the whole region, tested as each array is about
    // to be decoded and so counting the arrays of a section a later rule
    // refuses. The three caps above bound one section; these bound what a
    // caller has to hold for the region, which max_region_bytes bounds
    // only very loosely, since 64 MB of payload is room for roughly 930000
    // objects. The first array to cross its cap raises an issue and its
    // section is refused; the region walk goes on.
    std::uint64_t max_objects = 1ULL << 20;
    std::uint64_t max_instances = 1ULL << 20;
    std::uint64_t max_bound_nodes = 1ULL << 20;
    // The deepest retail tree is 10 nodes from its root.
    std::uint32_t max_tree_depth = 64;
};

struct TrackObjectReport {
    // Size of the file the region was read from; 0 for an in-memory parse.
    std::uint64_t file_size = 0;
    TrackRegionSelector region;
    std::vector<TrackSection> sections;
    std::vector<TrackIssue> issues;
    TrackObjectCost cost;

    bool ok() const noexcept { return issues.empty(); }
    std::size_t valid_section_count() const noexcept;
};

// The letter speed.exe derives from a section number at 0x0044580E:
// 0x40 + number / 100. Returns false, and leaves letter untouched, when
// that is not a letter from 'A' to 'Z'.
bool track_section_letter(std::uint32_t number, char& letter) noexcept;

// The instance placement in float form. linear is row-major, so world slot
// j is translation[j] + sum over i of local[i] * linear[i * 3 + j]. Total:
// it only divides the stored int16 by kSceneryMatrixOne.
struct TrackTransform {
    float linear[9] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    float translation[3] = {0.0F, 0.0F, 0.0F};
};

TrackTransform track_instance_transform(const TrackInstance& instance) noexcept;

// Applies a transform to one point, in the raw slot order of TrackVertex.
void track_transform_point(
    const TrackTransform& transform,
    const float local[3],
    float world[3]) noexcept;

// Parses one region image. data must be the region's bytes and nothing
// else; base_offset must be the absolute file offset those bytes came from,
// because the 16-byte alignment of the header record and of the instance
// array is relative to the start of the file. Never throws for malformed
// input: every problem is reported in TrackObjectReport::issues.
//
// A region with no 0x80034100 chunk is refused, exactly as a region with no
// geometry root is refused by parse_track_region.
TrackObjectReport parse_track_objects(
    const unsigned char* data,
    std::size_t size,
    std::uint64_t base_offset,
    const TrackObjectLimits& limits = TrackObjectLimits{});

// Reads exactly region.size bytes at region.offset of path, read-only, into
// bytes, then parses them. The rest of the file is never read.
TrackObjectReport load_track_objects(
    const std::filesystem::path& path,
    const TrackRegionSelector& region,
    std::vector<unsigned char>& bytes,
    const TrackObjectLimits& limits = TrackObjectLimits{});

// The distinct mesh hashes named by the valid sections' objects, ascending.
std::vector<std::uint32_t> track_object_mesh_hashes(
    const TrackObjectReport& report);

// One used mesh slot of one object, bound against a decoded region.
struct TrackObjectBinding {
    std::size_t section_index = 0;
    std::size_t object_index = 0;
    std::size_t slot = 0;
    std::uint32_t hash = 0;
    // Position in TrackRegionReport::meshes, meaningful only when resolved.
    // The lowest index among the valid meshes claiming the hash, so a
    // duplicate name is bound the same way on every run.
    std::size_t mesh_index = 0;
    bool resolved = false;
};

// What resolve_track_objects found. Counts are over the valid sections.
struct TrackObjectResolution {
    // One entry per non-zero mesh slot, in section, object and slot order.
    std::vector<TrackObjectBinding> bindings;
    // Distinct hashes no valid mesh of the region claims, ascending.
    std::vector<std::uint32_t> unresolved_hashes;
    std::uint64_t slots_total = 0;
    std::uint64_t slots_empty = 0;
    std::uint64_t slots_resolved = 0;
    std::uint64_t slots_unresolved = 0;
    // Instances whose object slot 0 resolved, and how many of those have a
    // stored box that contains every vertex of that mesh after the
    // instance's own transform. Retail satisfies this for nearly all of
    // them; docs/TRACK_OBJECTS.md gives the measured rate and the reason a
    // mismatch is reported rather than refused.
    std::uint64_t instances_checked = 0;
    std::uint64_t instances_bounds_contained = 0;
    // World bounds over every checked instance's transformed vertices, in
    // the raw slot order.
    bool has_bounds = false;
    float bounds_min[3] = {0.0F, 0.0F, 0.0F};
    float bounds_max[3] = {0.0F, 0.0F, 0.0F};
};

// Binds every used mesh slot of every valid object to a valid mesh of a
// decoded region, and cross-checks each instance's stored box against the
// geometry it places. The two reports need not come from the same region;
// a section commonly names meshes that stream in with another one, and
// those slots are reported unresolved rather than treated as an error.
TrackObjectResolution resolve_track_objects(
    const TrackObjectReport& objects,
    const TrackRegionReport& region);

} // namespace nfsu
