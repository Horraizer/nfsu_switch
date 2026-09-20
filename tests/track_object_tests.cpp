#include "nfsu/track_objects.h"

#include "nfsu/geometry.h"
#include "nfsu/track.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void check_issue_count(
    const nfsu::TrackObjectReport& report,
    std::size_t expected,
    const std::string& what) {
    check(report.issues.size() == expected,
          what + ": expected " + std::to_string(expected) + " issues, got " +
              std::to_string(report.issues.size()) +
              (report.issues.empty()
                   ? std::string()
                   : " (first: " + report.issues[0].message + ")"));
}

bool has_text(
    const std::vector<nfsu::TrackIssue>& issues,
    const std::string& text) {
    for (const nfsu::TrackIssue& issue : issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool has_issue_with(
    const nfsu::TrackObjectReport& report,
    const std::string& text) {
    return has_text(report.issues, text);
}

void check_refused(
    const nfsu::TrackObjectReport& report,
    const std::string& text,
    const std::string& what) {
    check(has_issue_with(report, text),
          what + ": expected an issue mentioning \"" + text + "\"" +
              (report.issues.empty()
                   ? std::string(", got none")
                   : ", got \"" + report.issues[0].message + "\""));
    check(report.valid_section_count() == 0,
          what + ": the section must not be valid");
}

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_i16(Bytes& bytes, std::int16_t value) {
    append_u16(bytes, static_cast<std::uint16_t>(value));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFFU));
    }
}

void append_f32(Bytes& bytes, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes, bits);
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

// The same absolute-offset chunk writer tests/track_tests.cpp uses: every
// alignment prefix is computed from the position the region will occupy in
// a file, so a fixture can be placed anywhere.
class Writer {
public:
    explicit Writer(std::uint64_t base) : base_(base) {}

    std::uint64_t at() const { return base_ + bytes_.size(); }
    std::size_t size() const { return bytes_.size(); }
    Bytes& bytes() { return bytes_; }
    const Bytes& bytes() const { return bytes_; }
    std::uint64_t base() const { return base_; }

    void begin(std::uint32_t id) {
        open_.push_back(bytes_.size());
        append_u32(bytes_, id);
        append_u32(bytes_, 0);
    }

    void end() {
        const std::size_t header = open_.back();
        open_.pop_back();
        put_u32(bytes_, header + 4,
                static_cast<std::uint32_t>(bytes_.size() - header - 8));
    }

    void leaf(std::uint32_t id, const Bytes& payload) {
        begin(id);
        append(payload);
        end();
    }

    void append(const Bytes& payload) {
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    std::size_t pad_to(std::uint64_t alignment, unsigned char filler = 0x11U) {
        std::size_t count = 0;
        while (at() % alignment != 0) {
            bytes_.push_back(filler);
            ++count;
        }
        return count;
    }

private:
    std::uint64_t base_;
    Bytes bytes_;
    std::vector<std::size_t> open_;
};

struct ObjectSpec {
    std::uint32_t hashes[6] = {0xAAAA0001U, 0, 0, 0, 0, 0};
    std::uint32_t flags = 16;
    std::uint32_t flags2 = 0;
    float radius = 2.5F;
};

struct InstanceSpec {
    std::int16_t bounds_min[3] = {-2, -2, -2};
    std::int16_t bounds_max[3] = {2, 2, 2};
    std::uint16_t object_index = 0;
    std::uint16_t flags = 0;
    float position[3] = {0.0F, 0.0F, 0.0F};
    std::int16_t matrix[9] = {8192, 0, 0, 0, 8192, 0, 0, 0, 8192};
    std::int16_t padding = 0;
};

struct NodeSpec {
    std::int16_t bounds_min[3] = {-2, -2, -2};
    std::int16_t bounds_max[3] = {2, 2, 2};
    std::uint16_t entry_count = 1;
    std::int16_t entries[5] = {0, 0, 0, 0, 0};
};

struct SectionSpec {
    std::uint32_t number = 303;
    std::vector<ObjectSpec> objects{ObjectSpec{}};
    std::vector<InstanceSpec> instances{InstanceSpec{}};
    // Empty means "one root node over every instance", which needs at most
    // kSceneryBoundNodeEntries instances.
    std::vector<NodeSpec> nodes;

    // Deliberate corruptions, applied while the section is written.
    bool drop_header = false;
    bool drop_objects = false;
    bool drop_instances = false;
    bool drop_tree = false;
    bool duplicate_header = false;
    bool unknown_child = false;
    bool break_header_filler = false;
    bool break_instance_filler = false;
    // Bytes cut from the end of the header payload.
    std::size_t header_short_by = 0;
    // Bytes appended after the header record, which the payload must not
    // carry: the record ends the chunk.
    std::size_t header_extra_bytes = 0;
    // Bytes appended to a list payload, breaking its record stride.
    std::size_t object_extra_bytes = 0;
    std::size_t instance_extra_bytes = 0;
    std::size_t tree_extra_bytes = 0;
    // Writes the four children in the order retail uses, or reversed.
    bool reverse_child_order = false;
    // Writes a 0x00034104 with no node in it at all.
    bool empty_tree = false;
};

Bytes object_payload(const std::vector<ObjectSpec>& objects,
                     std::size_t extra_bytes) {
    Bytes payload;
    for (const ObjectSpec& object : objects) {
        const std::size_t start = payload.size();
        for (std::size_t slot = 0; slot < nfsu::kSceneryObjectMeshSlots; ++slot) {
            append_u32(payload, object.hashes[slot]);
        }
        append_u32(payload, object.flags);
        append_u32(payload, object.flags2);
        // The radius goes at a literal 68, not at the constant the parser
        // reads from: the retail hexdump is the evidence for that offset,
        // so moving the constant must move only one side of the test.
        payload.resize(start + 68, 0U);
        append_f32(payload, object.radius);
        payload.resize(start + nfsu::kSceneryObjectRecordSize, 0U);
    }
    payload.insert(payload.end(), extra_bytes, 0x00U);
    return payload;
}

Bytes instance_payload(const std::vector<InstanceSpec>& instances,
                       std::size_t extra_bytes) {
    Bytes payload;
    for (const InstanceSpec& instance : instances) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(payload, instance.bounds_min[axis]);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(payload, instance.bounds_max[axis]);
        }
        append_u16(payload, instance.object_index);
        append_u16(payload, instance.flags);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_f32(payload, instance.position[axis]);
        }
        for (std::size_t word = 0; word < 9; ++word) {
            append_i16(payload, instance.matrix[word]);
        }
        append_i16(payload, instance.padding);
    }
    payload.insert(payload.end(), extra_bytes, 0x00U);
    return payload;
}

Bytes tree_payload(const std::vector<NodeSpec>& nodes, std::size_t extra_bytes) {
    Bytes payload;
    for (const NodeSpec& node : nodes) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(payload, node.bounds_min[axis]);
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            append_i16(payload, node.bounds_max[axis]);
        }
        append_u16(payload, node.entry_count);
        for (std::size_t slot = 0; slot < nfsu::kSceneryBoundNodeEntries; ++slot) {
            append_i16(payload, node.entries[slot]);
        }
    }
    payload.insert(payload.end(), extra_bytes, 0x00U);
    return payload;
}

std::vector<NodeSpec> default_tree(const std::vector<InstanceSpec>& instances) {
    NodeSpec root;
    root.entry_count = static_cast<std::uint16_t>(instances.size());
    bool first = true;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        root.entries[i] = static_cast<std::int16_t>(i);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (first) {
                root.bounds_min[axis] = instances[i].bounds_min[axis];
                root.bounds_max[axis] = instances[i].bounds_max[axis];
            } else {
                root.bounds_min[axis] = std::min(root.bounds_min[axis],
                                                 instances[i].bounds_min[axis]);
                root.bounds_max[axis] = std::max(root.bounds_max[axis],
                                                 instances[i].bounds_max[axis]);
            }
        }
        first = false;
    }
    return std::vector<NodeSpec>{root};
}

void write_section(Writer& writer, const SectionSpec& spec) {
    writer.begin(nfsu::kScenerySectionId);

    const std::vector<NodeSpec> nodes =
        spec.nodes.empty() ? default_tree(spec.instances) : spec.nodes;

    const auto write_header = [&]() {
        writer.begin(nfsu::kSceneryHeaderId);
        writer.pad_to(nfsu::kSceneryRecordAlignment,
                      spec.break_header_filler ? 0x22U : 0x11U);
        Bytes record(nfsu::kSceneryHeaderRecordSize, 0U);
        // A literal 12, not the constant the parser reads from: speed.exe
        // 0x00445804 is the evidence for that offset, so moving the
        // constant must move only one side of the test.
        put_u32(record, 12, spec.number);
        record.resize(record.size() - spec.header_short_by);
        record.insert(record.end(), spec.header_extra_bytes, 0x00U);
        writer.append(record);
        writer.end();
    };
    const auto write_instances = [&]() {
        writer.begin(nfsu::kSceneryInstanceListId);
        writer.pad_to(nfsu::kSceneryRecordAlignment,
                      spec.break_instance_filler ? 0x22U : 0x11U);
        writer.append(instance_payload(spec.instances, spec.instance_extra_bytes));
        writer.end();
    };
    const auto write_objects = [&]() {
        writer.leaf(nfsu::kSceneryObjectListId,
                    object_payload(spec.objects, spec.object_extra_bytes));
    };
    const auto write_tree = [&]() {
        writer.leaf(nfsu::kSceneryBoundTreeId,
                    spec.empty_tree
                        ? Bytes{}
                        : tree_payload(nodes, spec.tree_extra_bytes));
    };

    // Retail writes the header, the instances, the objects and then the
    // tree. The parser must not depend on that order.
    std::vector<int> order{0, 1, 2, 3};
    if (spec.reverse_child_order) {
        std::reverse(order.begin(), order.end());
    }
    for (const int which : order) {
        if (which == 0 && !spec.drop_header) {
            write_header();
            if (spec.duplicate_header) {
                write_header();
            }
        } else if (which == 1 && !spec.drop_instances) {
            write_instances();
        } else if (which == 2 && !spec.drop_objects) {
            write_objects();
        } else if (which == 3 && !spec.drop_tree) {
            write_tree();
        }
    }
    if (spec.unknown_child) {
        writer.leaf(0x00034199U, Bytes(8, 0U));
    }
    writer.end();
}

nfsu::TrackObjectReport parse(
    const Writer& writer,
    const nfsu::TrackObjectLimits& limits = nfsu::TrackObjectLimits{}) {
    return nfsu::parse_track_objects(writer.bytes().data(), writer.bytes().size(),
                                     writer.base(), limits);
}

nfsu::TrackObjectReport parse_one(
    const SectionSpec& spec,
    std::uint64_t base = 4096,
    const nfsu::TrackObjectLimits& limits = nfsu::TrackObjectLimits{}) {
    Writer writer(base);
    write_section(writer, spec);
    return parse(writer, limits);
}

// ---------------------------------------------------------------------------

void test_well_formed_section() {
    SectionSpec spec;
    spec.number = 303;
    spec.objects.clear();
    ObjectSpec first;
    first.hashes[0] = 0x00001111U;
    first.hashes[1] = 0x00002222U;
    first.hashes[4] = 0x00001111U;
    first.flags = 0x0010003CU;
    first.radius = 3.5F;
    ObjectSpec second;
    second.hashes[0] = 0x00003333U;
    second.radius = 1.25F;
    spec.objects.push_back(first);
    spec.objects.push_back(second);

    spec.instances.clear();
    InstanceSpec a;
    a.object_index = 0;
    a.position[0] = 10.0F;
    a.position[1] = 20.0F;
    a.position[2] = 30.0F;
    a.bounds_min[0] = 8;
    a.bounds_min[1] = 18;
    a.bounds_min[2] = 28;
    a.bounds_max[0] = 12;
    a.bounds_max[1] = 22;
    a.bounds_max[2] = 32;
    a.flags = 0x0060U;
    InstanceSpec b;
    b.object_index = 1;
    b.bounds_min[0] = -4;
    b.bounds_min[1] = -4;
    b.bounds_min[2] = -4;
    b.bounds_max[0] = 4;
    b.bounds_max[1] = 4;
    b.bounds_max[2] = 4;
    // A quarter turn about the third slot: row 0 becomes slot 1.
    const std::int16_t turned[9] = {0, 8192, 0, -8192, 0, 0, 0, 0, 8192};
    std::copy(turned, turned + 9, b.matrix);
    spec.instances.push_back(a);
    spec.instances.push_back(b);

    const nfsu::TrackObjectReport report = parse_one(spec);
    check_issue_count(report, 0, "well formed");
    check(report.sections.size() == 1, "well formed: one section");
    if (report.sections.empty()) {
        return;
    }
    const nfsu::TrackSection& section = report.sections[0];
    check(section.valid, "well formed: the section is valid");
    check(section.number == 303, "well formed: section number");
    check(section.has_letter && section.letter == 'C',
          "well formed: section letter");
    check(section.objects.size() == 2, "well formed: two objects");
    check(section.instances.size() == 2, "well formed: two instances");
    check(section.nodes.size() == 1, "well formed: one bounding node");
    check(section.tree_depth == 0, "well formed: a single node is depth 0");

    check(section.objects[0].mesh_hash[0] == 0x00001111U,
          "well formed: object slot 0");
    check(section.objects[0].mesh_hash[1] == 0x00002222U,
          "well formed: object slot 1");
    check(section.objects[0].mesh_hash[4] == 0x00001111U,
          "well formed: object slot 4");
    check(section.objects[0].mesh_hash[5] == 0U, "well formed: object slot 5");
    check(section.objects[0].flags == 0x0010003CU, "well formed: object flags");
    check(section.objects[0].radius == 3.5F, "well formed: object radius");

    check(section.instances[0].object_index == 0, "well formed: object index");
    check(section.instances[0].flags == 0x0060U, "well formed: instance flags");
    check(section.instances[0].position[0] == 10.0F &&
              section.instances[0].position[1] == 20.0F &&
              section.instances[0].position[2] == 30.0F,
          "well formed: instance position");
    check(section.instances[0].bounds_min[1] == 18 &&
              section.instances[0].bounds_max[1] == 22,
          "well formed: instance bounds");
    check(section.instances[1].matrix[1] == 8192 &&
              section.instances[1].matrix[3] == -8192,
          "well formed: instance matrix");

    check(report.cost.sections == 1 && report.cost.valid_sections == 1,
          "well formed: section cost");
    check(report.cost.objects == 2 && report.cost.instances == 2 &&
              report.cost.bound_nodes == 1,
          "well formed: record cost");
    check(report.cost.source_object_bytes == 144 &&
              report.cost.source_instance_bytes == 96 &&
              report.cost.source_bound_node_bytes == 24,
          "well formed: source byte cost");
    // Three non-zero slots over both objects, naming two distinct hashes
    // in the first object and one more in the second.
    check(report.cost.mesh_slots_used == 4, "well formed: used mesh slots");
    check(report.cost.distinct_mesh_hashes == 3,
          "well formed: distinct mesh hashes");
    check(report.cost.has_bounds && report.cost.bounds_min[0] == -4 &&
              report.cost.bounds_max[0] == 12,
          "well formed: instance bounds union");

    const std::vector<std::uint32_t> hashes = nfsu::track_object_mesh_hashes(report);
    check(hashes.size() == 3 && hashes[0] == 0x00001111U &&
              hashes[1] == 0x00002222U && hashes[2] == 0x00003333U,
          "well formed: mesh hashes are ascending and unique");

    // The same bytes parsed twice give the same answer.
    const nfsu::TrackObjectReport again = parse_one(spec);
    check(again.sections.size() == report.sections.size() &&
              again.cost.instances == report.cost.instances &&
              again.sections[0].instances[1].matrix[3] ==
                  section.instances[1].matrix[3],
          "well formed: parsing is deterministic");
}

void test_child_order_does_not_matter() {
    SectionSpec spec;
    spec.reverse_child_order = true;
    const nfsu::TrackObjectReport report = parse_one(spec);
    check_issue_count(report, 0, "reversed child order");
    check(report.valid_section_count() == 1,
          "reversed child order: the section still decodes");
}

void test_section_letter() {
    char letter = '\0';
    check(nfsu::track_section_letter(303, letter) && letter == 'C',
          "letter: 303 is C");
    check(nfsu::track_section_letter(2608, letter) && letter == 'Z',
          "letter: 2608 is Z");
    check(nfsu::track_section_letter(100, letter) && letter == 'A',
          "letter: 100 is A");
    check(nfsu::track_section_letter(199, letter) && letter == 'A',
          "letter: 199 is still A");
    letter = '\0';
    check(!nfsu::track_section_letter(0, letter),
          "letter: 0 is below A");
    check(!nfsu::track_section_letter(2700, letter),
          "letter: 2700 is past Z");
    check(letter == '\0', "letter: a refused number leaves the output alone");
}

void test_transform() {
    nfsu::TrackInstance instance;
    instance.position[0] = 1.0F;
    instance.position[1] = 2.0F;
    instance.position[2] = 3.0F;
    const std::int16_t identity[9] = {8192, 0, 0, 0, 8192, 0, 0, 0, 8192};
    std::copy(identity, identity + 9, instance.matrix);
    nfsu::TrackTransform transform = nfsu::track_instance_transform(instance);
    check(transform.linear[0] == 1.0F && transform.linear[4] == 1.0F &&
              transform.linear[8] == 1.0F && transform.linear[1] == 0.0F,
          "transform: the stored one becomes 1.0");
    float local[3] = {4.0F, 5.0F, 6.0F};
    float world[3] = {0.0F, 0.0F, 0.0F};
    nfsu::track_transform_point(transform, local, world);
    check(world[0] == 5.0F && world[1] == 7.0F && world[2] == 9.0F,
          "transform: identity translates only");

    // A quarter turn in the plane of slots 0 and 1: the first row is the
    // image of the first slot, so (1, 0, 0) must land on (0, 1, 0).
    const std::int16_t turned[9] = {0, 8192, 0, -8192, 0, 0, 0, 0, 8192};
    std::copy(turned, turned + 9, instance.matrix);
    instance.position[0] = 0.0F;
    instance.position[1] = 0.0F;
    instance.position[2] = 0.0F;
    transform = nfsu::track_instance_transform(instance);
    float unit[3] = {1.0F, 0.0F, 0.0F};
    nfsu::track_transform_point(transform, unit, world);
    check(world[0] == 0.0F && world[1] == 1.0F && world[2] == 0.0F,
          "transform: row major, the first row is the image of slot 0");

    // Half scale on every axis.
    const std::int16_t halved[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    std::copy(halved, halved + 9, instance.matrix);
    transform = nfsu::track_instance_transform(instance);
    local[0] = 2.0F;
    local[1] = 4.0F;
    local[2] = 8.0F;
    nfsu::track_transform_point(transform, local, world);
    check(world[0] == 1.0F && world[1] == 2.0F && world[2] == 4.0F,
          "transform: a scaled matrix scales");
}

void test_alignment_and_filler() {
    // A base offset that puts the section header on an odd 16-byte phase
    // forces a real alignment prefix in front of both aligned records.
    for (const std::uint64_t base : {std::uint64_t{0}, std::uint64_t{4096},
                                     std::uint64_t{4104}, std::uint64_t{4100}}) {
        SectionSpec spec;
        const nfsu::TrackObjectReport report = parse_one(spec, base);
        check_issue_count(report, 0,
                          "alignment at base " + std::to_string(base));
        if (report.valid_section_count() != 1) {
            continue;
        }
        const nfsu::TrackSection& section = report.sections[0];
        check(section.header_record_offset %
                      nfsu::kSceneryRecordAlignment == 0,
              "alignment: the header record is aligned");
        check(section.instance_list_offset %
                      nfsu::kSceneryRecordAlignment == 0,
              "alignment: the instance array is aligned");
        check(section.header_prefix_bytes < nfsu::kSceneryRecordAlignment &&
                  section.instance_prefix_bytes < nfsu::kSceneryRecordAlignment,
              "alignment: a prefix is shorter than the alignment");
        // The object directory and the bounding tree are never aligned.
        check(section.object_list_offset % 8 == 0,
              "alignment: the object list sits on its payload");
    }

    SectionSpec bad_header;
    bad_header.break_header_filler = true;
    // Only a base that needs a prefix can carry a wrong filler byte.
    const nfsu::TrackObjectReport header_report = parse_one(bad_header, 4104);
    check_refused(header_report, "alignment filler byte",
                  "a wrong header filler");

    SectionSpec bad_instances;
    bad_instances.break_instance_filler = true;
    const nfsu::TrackObjectReport instance_report = parse_one(bad_instances, 4096);
    check_refused(instance_report, "alignment filler byte",
                  "a wrong instance filler");
}

void test_section_shape() {
    {
        SectionSpec spec;
        spec.drop_header = true;
        check_refused(parse_one(spec), "no 0x00034101 chunk", "a missing header");
    }
    {
        SectionSpec spec;
        spec.drop_objects = true;
        check_refused(parse_one(spec), "no 0x00034102 chunk",
                      "a missing object list");
    }
    {
        SectionSpec spec;
        spec.drop_instances = true;
        check_refused(parse_one(spec), "no 0x00034103 chunk",
                      "a missing instance list");
    }
    {
        SectionSpec spec;
        spec.drop_tree = true;
        check_refused(parse_one(spec), "no 0x00034104 chunk",
                      "a missing bounding tree");
    }
    {
        SectionSpec spec;
        spec.duplicate_header = true;
        check_refused(parse_one(spec), "a second chunk 0x00034101",
                      "a duplicate header");
    }
    {
        SectionSpec spec;
        spec.unknown_child = true;
        check_refused(parse_one(spec), "unexpected chunk 0x00034199",
                      "an unknown child chunk");
    }
    {
        SectionSpec spec;
        spec.header_short_by = 1;
        check_refused(parse_one(spec), "too few for a 60-byte record",
                      "a truncated header record");
    }
    {
        // The other side of the same rule: the record ends the payload, so
        // trailing bytes are refused rather than ignored, the same way an
        // unknown chunk id is.
        SectionSpec spec;
        spec.header_extra_bytes = 64;
        check_refused(parse_one(spec),
                      "64 bytes after its 60-byte record",
                      "a header payload with trailing bytes");
    }
    {
        SectionSpec spec;
        spec.header_extra_bytes = 1;
        check_refused(parse_one(spec), "1 bytes after its 60-byte record",
                      "a header payload with one trailing byte");
    }
}

void test_object_rules() {
    {
        SectionSpec spec;
        spec.object_extra_bytes = 4;
        check_refused(parse_one(spec), "not a whole number of 72-byte object",
                      "a partial object record");
    }
    {
        SectionSpec spec;
        spec.objects.clear();
        spec.instances.clear();
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        spec.nodes.push_back(root);
        // An empty directory is refused before the instance that would
        // name it is ever read.
        InstanceSpec only;
        spec.instances.push_back(only);
        check_refused(parse_one(spec), "the object directory is empty",
                      "an empty object directory");
    }
    {
        SectionSpec spec;
        spec.objects[0].hashes[0] = 0;
        spec.objects[0].hashes[1] = 0x1234U;
        check_refused(parse_one(spec), "names no mesh in slot 0",
                      "an object with an empty slot 0");
    }
    {
        SectionSpec spec;
        spec.objects[0].radius = std::numeric_limits<float>::quiet_NaN();
        check_refused(parse_one(spec), "radius that is not finite",
                      "an object with a NaN radius");
    }
}

void test_instance_rules() {
    {
        SectionSpec spec;
        spec.instance_extra_bytes = 8;
        check_refused(parse_one(spec), "not a whole number of 48-byte records",
                      "a partial instance record");
    }
    {
        SectionSpec spec;
        spec.instances.clear();
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "the instance array is empty",
                      "an empty instance array");
    }
    {
        SectionSpec spec;
        spec.instances[0].object_index = 1;
        check_refused(parse_one(spec), "names object 1; the directory holds 1",
                      "an instance naming a missing object");
    }
    {
        SectionSpec spec;
        spec.instances[0].position[1] =
            std::numeric_limits<float>::infinity();
        check_refused(parse_one(spec), "position slot that is not finite",
                      "an instance with an infinite position");
    }
    {
        SectionSpec spec;
        spec.instances[0].padding = 1;
        check_refused(parse_one(spec), "non-zero padding word",
                      "an instance with a dirty padding word");
    }
    {
        // The bounding-node rule emits the same "which is empty" tail, and
        // the default tree derives its root box from the instance box, so
        // the whole instance message is asserted rather than that tail.
        SectionSpec spec;
        spec.instances[0].bounds_min[2] = 5;
        spec.instances[0].bounds_max[2] = 4;
        check_refused(parse_one(spec),
                      "instance 0 has slot 2 bounds 5 .. 4, which is empty",
                      "an instance with inverted bounds");
    }
}

void test_tree_rules() {
    {
        SectionSpec spec;
        spec.tree_extra_bytes = 12;
        check_refused(parse_one(spec), "not a whole number of 24-byte bounding",
                      "a partial bounding node");
    }
    {
        // A 0x00034104 that is present but holds no node: the instances it
        // should index would then be reachable from nothing.
        SectionSpec spec;
        spec.empty_tree = true;
        check_refused(parse_one(spec), "the bounding tree is empty",
                      "an empty bounding tree");
    }
    {
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 0;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "declares 0 entries",
                      "a bounding node with no entry");
    }
    {
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 6;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "declares 6 entries",
                      "a bounding node with too many entries");
    }
    {
        // The node side of the empty-box rule, with valid instances, so it
        // is pinned independently of the instance rule.
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = 0;
        root.bounds_min[1] = 7;
        root.bounds_max[1] = 6;
        spec.nodes.push_back(root);
        check_refused(
            parse_one(spec),
            "bounding node 0 has slot 1 bounds 7 .. 6, which is empty",
            "a bounding node with inverted bounds");
    }
    {
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = 3;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "names instance 3; the section holds 1",
                      "a leaf naming a missing instance");
    }
    {
        // The first index past the end rather than an overshoot. An
        // off-by-one in the leaf bound reads past the instance array here,
        // so only this fixture pins that comparison.
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = static_cast<std::int16_t>(spec.instances.size());
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "names instance 1; the section holds 1",
                      "a leaf naming the first instance past the end");
    }
    {
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = -4;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "names child node 4; the tree holds 1",
                      "a child index past the tree");
    }
    {
        // The same boundary on the child side: a one-node tree whose only
        // entry names node 1.
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        spec.nodes.push_back(root);
        spec.nodes[0].entries[0] =
            static_cast<std::int16_t>(-static_cast<int>(spec.nodes.size()));
        check_refused(parse_one(spec), "names child node 1; the tree holds 1",
                      "a child naming the first node past the end");
    }
    {
        // A node that is its own child.
        SectionSpec spec;
        spec.instances.clear();
        spec.instances.push_back(InstanceSpec{});
        spec.instances.push_back(InstanceSpec{});
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 2;
        root.entries[0] = 0;
        root.entries[1] = -1;
        NodeSpec child;
        child.entry_count = 2;
        child.entries[0] = 1;
        child.entries[1] = -1;
        spec.nodes.push_back(root);
        spec.nodes.push_back(child);
        check_refused(parse_one(spec), "which is already in the tree",
                      "a node that is its own child");
    }
    {
        // Two nodes naming the same child: a shared subtree, not a tree.
        SectionSpec spec;
        spec.instances.clear();
        spec.instances.push_back(InstanceSpec{});
        spec.instances.push_back(InstanceSpec{});
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 2;
        root.entries[0] = -1;
        root.entries[1] = -2;
        NodeSpec first;
        first.entry_count = 2;
        first.entries[0] = 0;
        first.entries[1] = -2;
        NodeSpec second;
        second.entry_count = 1;
        second.entries[0] = 1;
        spec.nodes.push_back(root);
        spec.nodes.push_back(first);
        spec.nodes.push_back(second);
        check_refused(parse_one(spec), "which is already in the tree",
                      "a shared child node");
    }
    {
        // A node no parent names.
        SectionSpec spec;
        spec.instances.clear();
        spec.instances.push_back(InstanceSpec{});
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = 0;
        NodeSpec orphan;
        orphan.entry_count = 1;
        orphan.entries[0] = 0;
        spec.nodes.push_back(root);
        spec.nodes.push_back(orphan);
        check_refused(parse_one(spec), "is not reachable from the root",
                      "an unreachable bounding node");
    }
    {
        // One instance in two leaves.
        SectionSpec spec;
        spec.instances.clear();
        spec.instances.push_back(InstanceSpec{});
        spec.instances.push_back(InstanceSpec{});
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 3;
        root.entries[0] = 0;
        root.entries[1] = 1;
        root.entries[2] = 0;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "which another node already holds",
                      "an instance in two bounding nodes");
    }
    {
        // An instance the tree never names.
        SectionSpec spec;
        spec.instances.clear();
        spec.instances.push_back(InstanceSpec{});
        spec.instances.push_back(InstanceSpec{});
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = 0;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "is in no bounding node",
                      "an instance outside the tree");
    }
    {
        // A node whose box does not hold the instance below it, on both
        // sides at once.
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = 0;
        root.bounds_min[0] = -1;
        root.bounds_max[0] = 1;
        spec.nodes.push_back(root);
        check_refused(parse_one(spec), "does not contain",
                      "a bounding node that is too small");
    }
    {
        // Only the minimum side: the node starts above the instance box
        // and ends exactly on it.
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = 0;
        root.bounds_min[0] = -1;
        root.bounds_max[0] = 2;
        spec.nodes.push_back(root);
        check_refused(
            parse_one(spec),
            "bounding node 0 has slot 0 bounds -1 .. 2, which does not "
            "contain -2 .. 2 below it",
            "a bounding node that is too small on the minimum side");
    }
    {
        // Only the maximum side.
        SectionSpec spec;
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 1;
        root.entries[0] = 0;
        root.bounds_min[0] = -2;
        root.bounds_max[0] = 1;
        spec.nodes.push_back(root);
        check_refused(
            parse_one(spec),
            "bounding node 0 has slot 0 bounds -2 .. 1, which does not "
            "contain -2 .. 2 below it",
            "a bounding node that is too small on the maximum side");
    }
    {
        // Depth, fan-out and the union rule on a real two-level tree.
        SectionSpec spec;
        spec.instances.clear();
        for (int i = 0; i < 3; ++i) {
            InstanceSpec instance;
            instance.bounds_min[0] = static_cast<std::int16_t>(10 * i);
            instance.bounds_max[0] = static_cast<std::int16_t>(10 * i + 2);
            spec.instances.push_back(instance);
        }
        spec.nodes.clear();
        NodeSpec root;
        root.entry_count = 2;
        root.entries[0] = 0;
        root.entries[1] = -1;
        root.bounds_min[0] = 0;
        root.bounds_max[0] = 22;
        NodeSpec child;
        child.entry_count = 2;
        child.entries[0] = 1;
        child.entries[1] = 2;
        child.bounds_min[0] = 10;
        child.bounds_max[0] = 22;
        spec.nodes.push_back(root);
        spec.nodes.push_back(child);
        const nfsu::TrackObjectReport report = parse_one(spec);
        check_issue_count(report, 0, "a two-level tree");
        check(report.valid_section_count() == 1, "a two-level tree: valid");
        if (report.valid_section_count() == 1) {
            check(report.sections[0].tree_depth == 1,
                  "a two-level tree: depth 1");
            check(report.cost.deepest_tree == 1,
                  "a two-level tree: the cost records the depth");
        }
    }
}

void test_limits() {
    {
        SectionSpec spec;
        nfsu::TrackObjectLimits limits;
        limits.max_objects_per_section = 0;
        check_refused(parse_one(spec, 4096, limits), "exceed the limit of 0",
                      "the object limit");
    }
    {
        SectionSpec spec;
        nfsu::TrackObjectLimits limits;
        limits.max_instances_per_section = 0;
        check_refused(parse_one(spec, 4096, limits), "exceed the limit of 0",
                      "the instance limit");
    }
    {
        SectionSpec spec;
        nfsu::TrackObjectLimits limits;
        limits.max_nodes_per_section = 0;
        check_refused(parse_one(spec, 4096, limits), "exceed the limit of 0",
                      "the bounding node limit");
    }
    {
        // The three region-wide running totals. Each fixture writes two
        // sections, so the second crosses a limit of one section's worth.
        const char* const names[] = {"objects", "instances", "bounding nodes"};
        for (int which = 0; which < 3; ++which) {
            Writer writer(0);
            SectionSpec spec;
            write_section(writer, spec);
            write_section(writer, spec);
            nfsu::TrackObjectLimits limits;
            if (which == 0) {
                limits.max_objects = 1;
            } else if (which == 1) {
                limits.max_instances = 1;
            } else {
                limits.max_bound_nodes = 1;
            }
            const nfsu::TrackObjectReport report = parse(writer, limits);
            const std::string expected = std::string("1 ") + names[which] +
                                         " after 1 already in the region "
                                         "exceed the limit of 1";
            check(has_issue_with(report, expected),
                  std::string("the region ") + names[which] +
                      " limit is reported (got " +
                      (report.issues.empty() ? std::string("no issue")
                                             : report.issues[0].message) +
                      ")");
            check(report.valid_section_count() == 1,
                  std::string("the region ") + names[which] +
                      " limit refuses only the section that crosses it");
        }
        // One section's worth is inside each limit.
        Writer writer(0);
        SectionSpec spec;
        write_section(writer, spec);
        nfsu::TrackObjectLimits limits;
        limits.max_objects = 1;
        limits.max_instances = 1;
        limits.max_bound_nodes = 1;
        const nfsu::TrackObjectReport report = parse(writer, limits);
        check_issue_count(report, 0, "one section inside the region limits");
        check(report.valid_section_count() == 1,
              "one section inside the region limits is valid");
    }
    {
        // A chain of nodes deeper than the limit allows.
        SectionSpec spec;
        spec.instances.clear();
        spec.instances.push_back(InstanceSpec{});
        spec.nodes.clear();
        for (int i = 0; i < 5; ++i) {
            NodeSpec node;
            node.entry_count = 1;
            node.entries[0] = static_cast<std::int16_t>(-(i + 1));
            spec.nodes.push_back(node);
        }
        NodeSpec leaf;
        leaf.entry_count = 1;
        leaf.entries[0] = 0;
        spec.nodes.push_back(leaf);
        nfsu::TrackObjectLimits limits;
        limits.max_tree_depth = 2;
        check_refused(parse_one(spec, 4096, limits), "deeper than 2",
                      "the tree depth limit");
        const nfsu::TrackObjectReport deep = parse_one(spec);
        check_issue_count(deep, 0, "a deep tree inside the limit");
        check(deep.valid_section_count() == 1 &&
                  deep.sections[0].tree_depth == 5,
              "a deep tree inside the limit: depth 5");
    }
    {
        Writer writer(0);
        SectionSpec spec;
        write_section(writer, spec);
        write_section(writer, spec);
        nfsu::TrackObjectLimits limits;
        limits.max_sections = 1;
        const nfsu::TrackObjectReport report = parse(writer, limits);
        check(has_issue_with(report, "section limit 1 exceeded"),
              "the section limit is reported");
        check(report.sections.size() == 1,
              "the section limit stops after the first section");
    }
    {
        Writer writer(0);
        SectionSpec spec;
        write_section(writer, spec);
        nfsu::TrackObjectLimits limits;
        limits.max_chunks = 2;
        const nfsu::TrackObjectReport report = parse(writer, limits);
        check(has_issue_with(report, "chunk limit 2 exceeded"),
              "the chunk limit is reported");
    }
}

void test_region_boundaries() {
    {
        Writer writer(0);
        writer.leaf(0x00000000U, Bytes(16, 0U));
        const nfsu::TrackObjectReport report = parse(writer);
        check(has_issue_with(report, "no scenery section chunk 0x80034100"),
              "a region with no section is refused");
    }
    {
        const nfsu::TrackObjectReport report =
            nfsu::parse_track_objects(nullptr, 0, 0);
        check(has_issue_with(report, "region is empty"),
              "a null region is refused");
    }
    {
        Bytes tiny(4, 0U);
        const nfsu::TrackObjectReport report =
            nfsu::parse_track_objects(tiny.data(), tiny.size(), 0);
        check(has_issue_with(report, "a chunk header needs 8"),
              "a region shorter than a header is refused");
    }
    {
        Writer writer(0);
        SectionSpec spec;
        write_section(writer, spec);
        writer.bytes().push_back(0U);
        writer.bytes().push_back(0U);
        const nfsu::TrackObjectReport report = parse(writer);
        check(has_issue_with(report, "trailing bytes are smaller than a chunk"),
              "trailing bytes are refused");
        check(report.valid_section_count() == 1,
              "the whole sections before the trailing bytes still decode");
    }
    {
        // A section whose declared payload runs past the region.
        Writer writer(0);
        SectionSpec spec;
        write_section(writer, spec);
        put_u32(writer.bytes(), 4,
                static_cast<std::uint32_t>(writer.size() + 16));
        const nfsu::TrackObjectReport report = parse(writer);
        check(has_issue_with(report, "remain in its parent"),
              "a section overrunning the region is refused");
    }
}

void test_load_reads_only_the_region() {
    const fs::path path =
        fs::temp_directory_path() / "nfsu_track_objects_region.bin";
    Writer writer(256);
    SectionSpec spec;
    spec.number = 1500;
    write_section(writer, spec);

    Bytes file(256, 0xCDU);
    file.insert(file.end(), writer.bytes().begin(), writer.bytes().end());
    // Bytes after the region that would break the parse if they were read.
    file.insert(file.end(), 512, 0xFFU);
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(file.data()),
                     static_cast<std::streamsize>(file.size()));
    }

    nfsu::TrackRegionSelector selector;
    selector.offset = 256;
    selector.size = writer.size();
    Bytes region_bytes;
    const nfsu::TrackObjectReport report =
        nfsu::load_track_objects(path, selector, region_bytes);
    check_issue_count(report, 0, "load: the region parses");
    check(region_bytes.size() == writer.size(),
          "load: exactly the region is read");
    check(report.file_size == file.size(), "load: the file size is reported");
    check(report.valid_section_count() == 1 &&
              report.sections[0].number == 1500,
          "load: the section decodes");
    check(report.sections[0].letter == 'O',
          "load: 1500 is section letter O");

    {
        nfsu::TrackRegionSelector past;
        past.offset = file.size();
        past.size = 16;
        Bytes bytes;
        const nfsu::TrackObjectReport outside =
            nfsu::load_track_objects(path, past, bytes);
        check(has_issue_with(outside, "outside the"),
              "load: a region past the end is refused");
        check(bytes.empty(), "load: nothing is read for a refused region");
    }
    {
        nfsu::TrackRegionSelector zero;
        zero.offset = 256;
        zero.size = 0;
        Bytes bytes;
        const nfsu::TrackObjectReport empty =
            nfsu::load_track_objects(path, zero, bytes);
        check(has_issue_with(empty, "region size is zero"),
              "load: a zero-length region is refused");
    }
    {
        nfsu::TrackObjectLimits limits;
        limits.max_region_bytes = 8;
        nfsu::TrackRegionSelector selector_too_big;
        selector_too_big.offset = 256;
        selector_too_big.size = writer.size();
        Bytes bytes;
        const nfsu::TrackObjectReport big =
            nfsu::load_track_objects(path, selector_too_big, bytes, limits);
        check(has_issue_with(big, "limit is 8"),
              "load: a region past the byte limit is refused");
    }

    std::error_code remove_error;
    fs::remove(path, remove_error);
}

// ---------------------------------------------------------------------------
// Mesh references, resolved against a decoded geometry region.
// ---------------------------------------------------------------------------

// A minimal track geometry root holding one mesh per requested hash, each a
// single triangle whose three vertices sit at known coordinates.
struct GeometryMeshSpec {
    std::uint32_t hash = 0;
    std::string name = "MESH";
    float span = 1.0F;
    // 0x00134900 +12. Bit 0x80 selects a 36-byte retail stride, which
    // docs/TRACK.md refuses, so the mesh keeps its hash and its metadata
    // but is left invalid with no vertex.
    std::uint32_t vertex_format_flags = 0;
};

void write_geometry_region(
    Writer& writer,
    const std::vector<GeometryMeshSpec>& meshes) {
    writer.begin(nfsu::kGeometryRootId);
    writer.begin(nfsu::kGeometryHeaderId);
    {
        Bytes table;
        std::vector<GeometryMeshSpec> sorted = meshes;
        std::sort(sorted.begin(), sorted.end(),
                  [](const GeometryMeshSpec& a, const GeometryMeshSpec& b) {
                      return a.hash < b.hash;
                  });
        for (const GeometryMeshSpec& mesh : sorted) {
            append_u32(table, mesh.hash);
            append_u32(table, 0U);
        }
        writer.leaf(nfsu::kMeshHashTableId, table);
    }
    writer.end();

    for (const GeometryMeshSpec& mesh : meshes) {
        writer.begin(nfsu::kMeshContainerId);
        writer.begin(nfsu::kMeshMetadataId);
        writer.pad_to(16);
        {
            Bytes record(nfsu::kMeshMetadataRecordSize, 0U);
            put_u32(record, nfsu::kMeshMetadataHashOffset, mesh.hash);
            record[nfsu::kMeshMetadataTextureCountOffset] = 1U;
            record[nfsu::kMeshMetadataKeyCountOffset] = 0U;
            for (std::size_t i = 0; i < mesh.name.size() && i + 1 < 28; ++i) {
                record[nfsu::kMeshMetadataNameOffset + i] =
                    static_cast<unsigned char>(mesh.name[i]);
            }
            writer.append(record);
        }
        writer.end();
        {
            Bytes textures;
            append_u32(textures, 0xAAAA0001U);
            append_u32(textures, 0U);
            writer.leaf(nfsu::kTextureListId, textures);
        }
        writer.begin(nfsu::kGeometryContainerId);
        writer.begin(nfsu::kGeometryMetadataId);
        writer.pad_to(16);
        {
            Bytes record(64, 0U);
            put_u32(record, nfsu::kVertexFormatOffset, mesh.vertex_format_flags);
            put_u32(record, nfsu::kMaterialCountOffset, 1U);
            put_u32(record, nfsu::kFaceCountOffset, 1U);
            put_u32(record, nfsu::kVertexCountOffset, 3U);
            writer.append(record);
        }
        writer.end();
        writer.begin(nfsu::kVertexBufferId);
        writer.pad_to(128);
        {
            Bytes vertices;
            const float points[3][3] = {{0.0F, 0.0F, 0.0F},
                                        {mesh.span, 0.0F, 0.0F},
                                        {0.0F, mesh.span, 0.0F}};
            for (const auto& point : points) {
                append_f32(vertices, point[0]);
                append_f32(vertices, point[1]);
                append_f32(vertices, point[2]);
                append_u32(vertices, 0xFF808080U);
                append_f32(vertices, 0.0F);
                append_f32(vertices, 0.0F);
            }
            writer.append(vertices);
        }
        writer.end();
        writer.begin(nfsu::kMaterialBufferId);
        writer.pad_to(16);
        {
            Bytes material(nfsu::kMaterialRecordSize, 0U);
            put_u32(material, 12, 3U);      // index count
            put_u32(material, 28, 0U);      // texture slot
            put_u32(material, 32, 0xFFU);   // material key slot
            put_u32(material, 52, 0U);      // first index
            writer.append(material);
        }
        writer.end();
        writer.begin(nfsu::kFaceBufferId);
        writer.pad_to(16);
        {
            Bytes faces;
            append_u16(faces, 0);
            append_u16(faces, 1);
            append_u16(faces, 2);
            append_u16(faces, 0);
            writer.append(faces);
        }
        writer.end();
        writer.end();
        writer.end();
    }
    writer.end();
}

void test_resolution() {
    const std::uint32_t present = 0x00001111U;
    const std::uint32_t absent = 0x00009999U;
    // A mesh the region carries and docs/TRACK.md refuses. Binding to it
    // would contradict the promise that a binding names a valid mesh, and
    // it would be counted in slots_resolved.
    const std::uint32_t refused = 0x00007777U;

    Writer geometry_writer(0);
    std::vector<GeometryMeshSpec> meshes;
    GeometryMeshSpec mesh;
    mesh.hash = present;
    mesh.name = "TRACKPIECE";
    mesh.span = 4.0F;
    meshes.push_back(mesh);
    GeometryMeshSpec broken;
    broken.hash = refused;
    broken.name = "BADSTRIDE";
    broken.span = 4.0F;
    broken.vertex_format_flags = 0x00000080U;
    meshes.push_back(broken);
    write_geometry_region(geometry_writer, meshes);
    const nfsu::TrackRegionReport region = nfsu::parse_track_region(
        geometry_writer.bytes().data(), geometry_writer.bytes().size(),
        geometry_writer.base());
    check(region.meshes.size() == 2 && region.valid_mesh_count() == 1,
          "resolution: the geometry fixture decodes one of its two meshes");
    check(region.issues.size() == 1 &&
              has_text(region.issues, "selects retail vertex format"),
          "resolution: the refused mesh is the only issue" +
              (region.issues.empty() ? std::string()
                                     : " (" + region.issues[0].message + ")"));
    if (region.valid_mesh_count() != 1 || region.meshes.size() != 2) {
        return;
    }
    check(!region.meshes[1].valid && region.meshes[1].mesh_hash == refused &&
              region.meshes[1].vertices.empty(),
          "resolution: the refused mesh keeps its hash and holds no vertex");

    SectionSpec spec;
    spec.objects.clear();
    ObjectSpec resolved;
    resolved.hashes[0] = present;
    resolved.hashes[1] = absent;
    resolved.hashes[2] = refused;
    ObjectSpec unresolved;
    unresolved.hashes[0] = absent;
    spec.objects.push_back(resolved);
    spec.objects.push_back(unresolved);

    spec.instances.clear();
    // The mesh spans 0..4 on slots 0 and 1, so a box of -1..5 around the
    // origin contains it and a box of 0..1 does not.
    InstanceSpec fits;
    fits.object_index = 0;
    fits.bounds_min[0] = -1;
    fits.bounds_min[1] = -1;
    fits.bounds_min[2] = -1;
    fits.bounds_max[0] = 5;
    fits.bounds_max[1] = 5;
    fits.bounds_max[2] = 1;
    InstanceSpec too_small;
    too_small.object_index = 0;
    too_small.bounds_min[0] = 0;
    too_small.bounds_min[1] = 0;
    too_small.bounds_min[2] = 0;
    too_small.bounds_max[0] = 1;
    too_small.bounds_max[1] = 1;
    too_small.bounds_max[2] = 1;
    InstanceSpec unresolvable;
    unresolvable.object_index = 1;
    spec.instances.push_back(fits);
    spec.instances.push_back(too_small);
    spec.instances.push_back(unresolvable);

    const nfsu::TrackObjectReport objects = parse_one(spec);
    check_issue_count(objects, 0, "resolution: the section decodes");
    if (objects.valid_section_count() != 1) {
        return;
    }

    const nfsu::TrackObjectResolution resolution =
        nfsu::resolve_track_objects(objects, region);
    check(resolution.slots_total == 12, "resolution: twelve slots in total");
    check(resolution.slots_empty == 8, "resolution: eight empty slots");
    check(resolution.slots_resolved == 1, "resolution: one resolved slot");
    check(resolution.slots_unresolved == 3,
          "resolution: three unresolved slots");
    check(resolution.bindings.size() == 4,
          "resolution: one binding per used slot");
    if (resolution.bindings.size() == 4) {
        check(resolution.bindings[0].resolved &&
                  resolution.bindings[0].hash == present &&
                  resolution.bindings[0].object_index == 0 &&
                  resolution.bindings[0].slot == 0 &&
                  resolution.bindings[0].mesh_index == 0,
              "resolution: the first binding resolves");
        check(!resolution.bindings[1].resolved &&
                  resolution.bindings[1].slot == 1,
              "resolution: the second binding does not");
        // The refused mesh is in the region under this exact hash, so only
        // the validity filter keeps this slot unresolved.
        check(!resolution.bindings[2].resolved &&
                  resolution.bindings[2].slot == 2 &&
                  resolution.bindings[2].hash == refused,
              "resolution: a hash that names only a refused mesh does not "
              "resolve");
        check(!resolution.bindings[3].resolved &&
                  resolution.bindings[3].object_index == 1,
              "resolution: the fourth binding does not");
    }
    // The set is reported in ascending order, and 0x7777 sorts before
    // 0x9999.
    check(resolution.unresolved_hashes.size() == 2 &&
              resolution.unresolved_hashes[0] == refused &&
              resolution.unresolved_hashes[1] == absent,
          "resolution: two distinct unresolved hashes");

    check(resolution.instances_checked == 2,
          "resolution: the two resolvable instances are checked");
    check(resolution.instances_bounds_contained == 1,
          "resolution: one of them has a box that holds its mesh");
    check(resolution.has_bounds && resolution.bounds_min[0] == 0.0F &&
              resolution.bounds_max[0] == 4.0F,
          "resolution: world bounds over the checked instances");

    // A translated instance moves its world bounds by the same amount.
    SectionSpec moved = spec;
    moved.instances.clear();
    InstanceSpec shifted = fits;
    shifted.position[0] = 100.0F;
    shifted.bounds_min[0] = 99;
    shifted.bounds_max[0] = 105;
    moved.instances.push_back(shifted);
    const nfsu::TrackObjectReport moved_report = parse_one(moved);
    const nfsu::TrackObjectResolution moved_resolution =
        nfsu::resolve_track_objects(moved_report, region);
    check(moved_resolution.instances_checked == 1 &&
              moved_resolution.instances_bounds_contained == 1,
          "resolution: a translated instance still fits its box");
    check(moved_resolution.bounds_min[0] == 100.0F &&
              moved_resolution.bounds_max[0] == 104.0F,
          "resolution: the translation reaches the world bounds");

    // An empty region resolves nothing and checks nothing, rather than
    // failing: a section commonly names meshes another region carries.
    const nfsu::TrackRegionReport nothing;
    const nfsu::TrackObjectResolution none =
        nfsu::resolve_track_objects(objects, nothing);
    check(none.slots_resolved == 0 && none.slots_unresolved == 4 &&
              none.instances_checked == 0 && !none.has_bounds,
          "resolution: an empty region leaves every slot unresolved");
}

} // namespace

int main() {
    test_well_formed_section();
    test_child_order_does_not_matter();
    test_section_letter();
    test_transform();
    test_alignment_and_filler();
    test_section_shape();
    test_object_rules();
    test_instance_rules();
    test_tree_rules();
    test_limits();
    test_region_boundaries();
    test_load_reads_only_the_region();
    test_resolution();

    if (g_failures != 0) {
        std::cerr << g_failures << " track object test failures of " << g_checks
                  << " checks\n";
        return 1;
    }
    std::cout << "track object tests passed (" << g_checks << " checks)\n";
    return 0;
}
