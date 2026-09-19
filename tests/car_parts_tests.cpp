#include "nfsu/car_parts.h"
#include "nfsu/texture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<unsigned char>((value >> (8U * i)) & 0xFFU));
    }
}

void append_u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
    bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void put_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xFFU);
    }
}

void append_chunk(Bytes& out, std::uint32_t id, const Bytes& payload) {
    append_u32(out, id);
    append_u32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

struct TestRecord {
    std::string name = "PART";
    std::uint32_t car = 0;
    std::uint32_t part = 0;
    std::uint32_t hash_0c = 0;
    int slot = 0;
    std::uint8_t level = 0;
    std::uint8_t byte_12 = 0;
    std::uint8_t byte_13 = 0;
    std::uint32_t attribute_index = 0;
    std::int16_t attribute_count = 0;
    std::uint32_t second_attribute_index = 0;
    std::int16_t second_attribute_count = 0;
    std::array<std::uint32_t, nfsu::kCarPartLodCount> lods{};
};

struct TestDatabase {
    std::vector<TestRecord> records;
    std::vector<nfsu::CarPartAttribute> attributes;
    std::uint32_t version = nfsu::kCarPartDatabaseVersion;
    // Leading top-level padding chunk, as in GLOBALB.BUN.
    std::size_t leading_padding = 16;
};

struct Layout {
    Bytes bytes;
    std::size_t root = 0;
    std::size_t header_payload = 0;
    std::size_t strings_payload = 0;
    std::size_t attributes_payload = 0;
    std::size_t records_payload = 0;
};

Bytes header_payload(const TestDatabase& db) {
    Bytes header;
    append_u32(header, 0);
    append_u32(header, 0);
    append_u32(header, db.version);
    append_u32(header, 0);
    append_u32(header, static_cast<std::uint32_t>(db.records.size()));
    append_u32(header, 0);
    append_u32(header, static_cast<std::uint32_t>(db.attributes.size()));
    append_u32(header, 0);
    return header;
}

Layout build(const TestDatabase& db) {
    Bytes strings;
    std::vector<std::uint32_t> name_offsets;
    for (const TestRecord& record : db.records) {
        name_offsets.push_back(static_cast<std::uint32_t>(strings.size()));
        strings.insert(strings.end(), record.name.begin(), record.name.end());
        strings.push_back(0);
    }
    Bytes attributes;
    for (const nfsu::CarPartAttribute& attribute : db.attributes) {
        append_u32(attributes, attribute.key);
        append_u32(attributes, attribute.value);
    }
    Bytes records;
    for (std::size_t i = 0; i < db.records.size(); ++i) {
        const TestRecord& r = db.records[i];
        append_u32(records, name_offsets[i]);
        append_u32(records, r.car);
        append_u32(records, r.part);
        append_u32(records, r.hash_0c);
        records.push_back(static_cast<unsigned char>(static_cast<signed char>(r.slot)));
        records.push_back(r.level);
        records.push_back(r.byte_12);
        records.push_back(r.byte_13);
        append_u32(records, r.attribute_index);
        append_u32(records, r.second_attribute_index);
        append_u16(records, static_cast<std::uint16_t>(r.attribute_count));
        append_u16(records, static_cast<std::uint16_t>(r.second_attribute_count));
        for (const std::uint32_t hash : r.lods) {
            append_u32(records, hash);
        }
    }

    Layout layout;
    if (db.leading_padding != 0) {
        append_chunk(layout.bytes, 0, Bytes(db.leading_padding, 0x11));
    }
    Bytes children;
    const Bytes header = header_payload(db);
    append_chunk(children, nfsu::kCarPartHeaderId, header);
    const std::size_t strings_at = children.size() + 8;
    append_chunk(children, nfsu::kCarPartStringTableId, strings);
    const std::size_t attributes_at = children.size() + 8;
    append_chunk(children, nfsu::kCarPartAttributeTableId, attributes);
    const std::size_t records_at = children.size() + 8;
    append_chunk(children, nfsu::kCarPartRecordTableId, records);
    layout.root = layout.bytes.size();
    append_chunk(layout.bytes, nfsu::kCarPartDatabaseId, children);
    layout.header_payload = layout.root + 16;
    layout.strings_payload = layout.root + 8 + strings_at;
    layout.attributes_payload = layout.root + 8 + attributes_at;
    layout.records_payload = layout.root + 8 + records_at;
    // A trailing unrelated chunk, as in GLOBALB.BUN.
    append_chunk(layout.bytes, 0x00034201U, Bytes(12, 0));
    return layout;
}

nfsu::CarPartDatabase parse(const Bytes& bytes,
                            const nfsu::CarPartLimits& limits = nfsu::CarPartLimits{}) {
    return nfsu::parse_car_part_database(bytes.data(), bytes.size(), limits);
}

bool has_issue(const nfsu::CarPartDatabase& db, const std::string& text) {
    for (const nfsu::CarPartIssue& issue : db.issues) {
        if (issue.message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void expect_rejected(const Bytes& bytes, const std::string& text, const std::string& what,
                     const nfsu::CarPartLimits& limits = nfsu::CarPartLimits{}) {
    const nfsu::CarPartDatabase db = parse(bytes, limits);
    check(!db.valid && !db.ok(), what + ": database must be invalid");
    check(has_issue(db, text), what + ": expected an issue containing '" + text + "'");
    check(nfsu::find_car_part(db, nfsu::kAnyCarType, nfsu::kAnySlot, nfsu::kAnyPartName,
                              nfsu::kNoCarPart, nfsu::kAnyUpgradeLevel) == nfsu::kNoCarPart,
          what + ": lookups must refuse an invalid database");
}

TestRecord record(std::uint32_t car, int slot, std::uint8_t level, std::uint32_t part,
                  const std::string& name = "PART") {
    TestRecord r;
    r.car = car;
    r.slot = slot;
    r.level = level;
    r.part = part;
    r.name = name;
    r.lods = {part + 1U, part + 2U, part + 3U, 0U};
    return r;
}

void test_slot_names() {
    check(std::string(nfsu::car_slot_name(0)) == "BASE", "slot 0 name");
    check(std::string(nfsu::car_slot_name(11)) == "SPOILER", "slot 11 name");
    check(std::string(nfsu::car_slot_name(16)) == "BRAKE", "slot 16 name");
    check(std::string(nfsu::car_slot_name(119)) == "MISC", "slot 119 name");
    check(nfsu::car_slot_name(-1) == nullptr, "slot -1 has no name");
    check(nfsu::car_slot_name(120) == nullptr, "slot 120 has no name");
    // Hashes of the names in speed.exe's slot table.
    check(nfsu::bin_hash(nfsu::car_slot_name(11)) == 0xC93B73FDU, "SPOILER hash");
    check(nfsu::bin_hash(nfsu::car_slot_name(118)) == 0x24165AE0U,
          "WHEEL_MANUFACTURER hash");
    for (int slot = 0; slot < static_cast<int>(nfsu::kCarSlotCount); ++slot) {
        for (int other = 0; other < slot; ++other) {
            check(std::string(nfsu::car_slot_name(slot)) != nfsu::car_slot_name(other),
                  "slot names are unique");
        }
    }
    check(std::string(nfsu::stock_part_rule_name(nfsu::StockPartRule::plates_fallback)) ==
              "plates-fallback",
          "rule name");
}

void test_valid_database() {
    TestDatabase db;
    db.attributes = {{0x11111111U, 7U}, {nfsu::kDisableSlotAttributeKey, 0xC93B73FDU}};
    TestRecord first = record(0x100U, 1, 0, 0xAAAA0000U, "STOCK");
    first.hash_0c = 0x02DAAB07U;
    first.byte_12 = 3;
    first.byte_13 = 4;
    first.attribute_index = 0;
    first.attribute_count = 2;
    first.second_attribute_index = 1;
    first.second_attribute_count = 1;
    first.lods = {0x2E44DCB9U, 0x2E44DCBAU, 0x2E44DCBBU, 0U};
    db.records = {first, record(0x100U, 119, 0, 0xBBBB0000U, "ColVol KIT00"),
                  record(0x200U, 0, 2, 0xCCCC0000U, "")};
    const Layout layout = build(db);
    const nfsu::CarPartDatabase parsed = parse(layout.bytes);
    check(parsed.ok() && parsed.valid, "valid database parses");
    check(parsed.issues.empty(), "valid database has no issues");
    check(parsed.chunk_offset == layout.root, "chunk offset");
    check(parsed.version == 5, "version");
    check(parsed.file_size == layout.bytes.size(), "file size");
    check(parsed.records.size() == 3 && parsed.attributes.size() == 2, "counts");
    if (parsed.records.size() != 3 || parsed.attributes.size() != 2) {
        return;
    }
    const nfsu::CarPartRecord& r = parsed.records[0];
    check(r.index == 0 && r.offset == layout.records_payload, "record offset");
    check(r.name == "STOCK" && r.name_offset == 0, "record name");
    check(r.car_type_hash == 0x100U && r.part_name_hash == 0xAAAA0000U, "record hashes");
    check(r.hash_0c == 0x02DAAB07U, "record +0x0C");
    check(r.slot == 1 && r.upgrade_level == 0, "record slot and level");
    check(r.byte_12 == 3 && r.byte_13 == 4, "record +0x12 +0x13");
    check(r.attribute_index == 0 && r.attribute_count == 2, "record attributes");
    check(r.second_attribute_index == 1 && r.second_attribute_count == 1,
          "record second attributes");
    check(r.lod_mesh_hashes[0] == 0x2E44DCB9U && r.lod_mesh_hashes[2] == 0x2E44DCBBU &&
              r.lod_mesh_hashes[3] == 0,
          "record LOD hashes");
    check(r.raw[0] == 0 && r.raw[16] == 1 && r.raw[32] == 0xB9U, "record raw bytes");
    check(parsed.records[1].name == "ColVol KIT00" && parsed.records[1].slot == 119,
          "second record");
    check(parsed.records[2].name.empty() && parsed.records[2].upgrade_level == 2,
          "empty display name is accepted");
    check(parsed.attributes[1].key == nfsu::kDisableSlotAttributeKey, "attribute key");
}

TestDatabase lookup_database() {
    TestDatabase db;
    db.records = {
        record(0x100U, 1, 1, 0x10U),   // 0
        record(0x100U, 1, 0, 0x11U),   // 1
        record(0x100U, 2, 0, 0x12U),   // 2
        record(0x100U, 1, 0, 0x13U),   // 3
        record(0x200U, 1, 0, 0x20U),   // 4
        record(0x200U, 3, 0, 0x21U),   // 5
        record(0x300U, 1, 0, 0x30U),   // 6
    };
    return db;
}

void test_find_car_part() {
    const nfsu::CarPartDatabase db = parse(build(lookup_database()).bytes);
    check(db.ok(), "lookup database parses");
    using nfsu::find_car_part;
    using nfsu::kAnyCarType;
    using nfsu::kAnyPartName;
    using nfsu::kAnySlot;
    using nfsu::kAnyUpgradeLevel;
    using nfsu::kNoCarPart;
    check(find_car_part(db, 0x100U, 1, kAnyPartName, kNoCarPart, kAnyUpgradeLevel) == 0,
          "first match in file order");
    check(find_car_part(db, 0x100U, 1, kAnyPartName, kNoCarPart, 0) == 1,
          "level filter");
    check(find_car_part(db, 0x100U, 1, kAnyPartName, 1, 0) == 3, "continue after a record");
    check(find_car_part(db, 0x100U, 1, kAnyPartName, 3, 0) == kNoCarPart,
          "search ends at the next car type");
    check(find_car_part(db, 0x200U, kAnySlot, kAnyPartName, kNoCarPart, kAnyUpgradeLevel) == 4,
          "binary search finds the first record of a car type");
    check(find_car_part(db, 0x200U, 3, kAnyPartName, kNoCarPart, kAnyUpgradeLevel) == 5,
          "slot filter");
    check(find_car_part(db, 0x300U, 1, kAnyPartName, kNoCarPart, 0) == 6, "last car type");
    check(find_car_part(db, 0x250U, kAnySlot, kAnyPartName, kNoCarPart, kAnyUpgradeLevel) ==
              kNoCarPart,
          "unknown car type");
    check(find_car_part(db, kAnyCarType, 3, kAnyPartName, kNoCarPart, kAnyUpgradeLevel) == 5,
          "any car type scans every record");
    check(find_car_part(db, kAnyCarType, 1, kAnyPartName, 4, kAnyUpgradeLevel) == 6,
          "any car type continues across car types");
    check(find_car_part(db, kAnyCarType, kAnySlot, 0x12U, kNoCarPart, kAnyUpgradeLevel) == 2,
          "part name filter");
    check(find_car_part(db, 0x100U, kAnySlot, 0x12U, 2, kAnyUpgradeLevel) == kNoCarPart,
          "no later record has the name");
    check(find_car_part(db, 0x100U, 2, kAnyPartName, kNoCarPart, 1) == kNoCarPart,
          "no record has the level");
    check(find_car_part(db, 0x100U, 1, kAnyPartName, 4, kAnyUpgradeLevel) == kNoCarPart,
          "after a record of another car type stops at once");
    check(find_car_part(db, 0x100U, kAnySlot, kAnyPartName, 7, kAnyUpgradeLevel) ==
              kNoCarPart,
          "after outside the database");
    check(find_car_part(db, 0x100U, kAnySlot, kAnyPartName, 6, kAnyUpgradeLevel) ==
              kNoCarPart,
          "after the last record");
    check(find_car_part(db, 0x100U, 200, kAnyPartName, kNoCarPart, kAnyUpgradeLevel) ==
              kNoCarPart,
          "slot outside the table");
}

void test_malformed() {
    const TestDatabase base = lookup_database();
    const Layout good = build(base);

    expect_rejected(Bytes{}, "found 0", "empty file");
    {
        Bytes bytes;
        append_chunk(bytes, 0, Bytes(8, 0));
        expect_rejected(bytes, "found 0", "no database chunk");
    }
    {
        Bytes bytes = good.bytes;
        const Bytes again = build(base).bytes;
        bytes.insert(bytes.end(), again.begin(), again.end());
        expect_rejected(bytes, "found 2", "two database chunks");
    }
    {
        Bytes bytes = good.bytes;
        bytes.push_back(0);
        expect_rejected(bytes, "smaller than a chunk header", "trailing byte");
    }
    {
        Bytes bytes = good.bytes;
        put_u32(bytes, good.root + 4, static_cast<std::uint32_t>(bytes.size()));
        expect_rejected(bytes, "remain in its parent", "database chunk too long");
    }
    {
        Bytes bytes = good.bytes;
        put_u32(bytes, good.header_payload - 8, nfsu::kCarPartStringTableId);
        put_u32(bytes, good.strings_payload - 8, nfsu::kCarPartHeaderId);
        expect_rejected(bytes, "in this order", "children swapped");
    }
    {
        Bytes children;
        append_chunk(children, nfsu::kCarPartHeaderId, header_payload(base));
        Bytes bytes;
        append_chunk(bytes, nfsu::kCarPartDatabaseId, children);
        expect_rejected(bytes, "in this order", "children missing");
    }
    {
        // An extra fifth child.
        Bytes bytes = good.bytes;
        const std::uint32_t root_size =
            static_cast<std::uint32_t>(bytes[good.root + 4]) |
            (static_cast<std::uint32_t>(bytes[good.root + 5]) << 8U) |
            (static_cast<std::uint32_t>(bytes[good.root + 6]) << 16U);
        Bytes extra;
        append_chunk(extra, 0x00034607U, Bytes(4, 0));
        const std::size_t end = good.root + 8 + root_size;
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(end), extra.begin(),
                     extra.end());
        put_u32(bytes, good.root + 4, root_size + static_cast<std::uint32_t>(extra.size()));
        expect_rejected(bytes, "in this order", "extra child");
    }
    {
        TestDatabase db = base;
        db.version = 6;
        expect_rejected(build(db).bytes, "version 6 is not supported", "version");
    }
    for (const std::size_t word : {0U, 4U, 12U, 20U, 28U}) {
        Bytes bytes = good.bytes;
        put_u32(bytes, good.header_payload + word, 1);
        expect_rejected(bytes, "must be zero in the file",
                        "header word " + std::to_string(word));
    }
    {
        Bytes bytes = good.bytes;
        put_u32(bytes, good.header_payload + 16, 8);
        expect_rejected(bytes, "the header declares 8 records", "record count");
    }
    {
        Bytes bytes = good.bytes;
        put_u32(bytes, good.header_payload + 24, 1);
        expect_rejected(bytes, "the header declares 1 attributes", "attribute count");
    }
    {
        // Header payload of 28 bytes.
        Bytes children;
        Bytes header = header_payload(base);
        header.resize(28);
        append_chunk(children, nfsu::kCarPartHeaderId, header);
        append_chunk(children, nfsu::kCarPartStringTableId, Bytes{});
        append_chunk(children, nfsu::kCarPartAttributeTableId, Bytes{});
        append_chunk(children, nfsu::kCarPartRecordTableId, Bytes{});
        Bytes bytes;
        append_chunk(bytes, nfsu::kCarPartDatabaseId, children);
        expect_rejected(bytes, "expected 32", "short header");
    }
    {
        Bytes bytes = good.bytes;
        put_u32(bytes, good.records_payload + 48, 4096);
        expect_rejected(bytes, "outside the string table", "name offset");
    }
    {
        TestDatabase db = base;
        db.records.back().name = "LAST";
        Layout layout = build(db);
        // Replace the final NUL of the string table.
        layout.bytes[layout.attributes_payload - 9] = 'X';
        expect_rejected(layout.bytes, "not NUL-terminated", "unterminated name");
    }
    {
        // Display names are kept byte for byte; one retail name ends in TAB.
        TestDatabase db = base;
        db.records[0].name = std::string("ACE\t", 4);
        db.records[1].name = "\xC3\xA9";
        const nfsu::CarPartDatabase parsed = parse(build(db).bytes);
        check(parsed.ok() && parsed.records.size() == base.records.size() &&
                  parsed.records[0].name == std::string("ACE\t", 4) &&
                  parsed.records[1].name == "\xC3\xA9",
              "names are not restricted to printable ASCII");
    }
    for (const int slot : {120, -1, 127}) {
        TestDatabase db = base;
        db.records[2].slot = slot;
        expect_rejected(build(db).bytes, "is outside 0..119",
                        "slot " + std::to_string(slot));
    }
    {
        TestDatabase db = base;
        db.attributes = {{1U, 2U}};
        db.records[0].attribute_index = 1;
        db.records[0].attribute_count = 1;
        expect_rejected(build(db).bytes, "outside the attribute table", "attribute range");
    }
    {
        TestDatabase db = base;
        db.attributes = {{1U, 2U}};
        db.records[0].second_attribute_index = 0xFFFFFFFFU;
        db.records[0].second_attribute_count = 2;
        expect_rejected(build(db).bytes, "outside the attribute table",
                        "second attribute range overflow");
    }
    {
        TestDatabase db = base;
        db.records[0].attribute_count = -1;
        expect_rejected(build(db).bytes, "negative attribute count", "negative count");
    }
    {
        // With a zero count the index is never read, as in the game
        // (0x57c991, 0x57c9a6), so it need not be inside the table.
        TestDatabase db = base;
        db.attributes = {{1U, 2U}};
        db.records[0].attribute_index = 0xFFFFFFFFU;
        db.records[0].second_attribute_index = 12345U;
        const nfsu::CarPartDatabase parsed = parse(build(db).bytes);
        check(parsed.ok() && parsed.records[0].attribute_index == 0xFFFFFFFFU,
              "an unused attribute index is kept and not checked");
    }
    {
        TestDatabase db = base;
        db.records[5].car = 0x050U;
        expect_rejected(build(db).bytes, "ascending car type hash order", "unsorted");
    }
    {
        // Unsigned order: 0x80000000 sorts after 0x7FFFFFFF.
        TestDatabase db;
        db.records = {record(0x7FFFFFFFU, 0, 0, 1U), record(0x80000000U, 0, 0, 2U)};
        check(parse(build(db).bytes).ok(), "unsigned car type hash order");
        db.records = {record(0x80000000U, 0, 0, 1U), record(0x7FFFFFFFU, 0, 0, 2U)};
        expect_rejected(build(db).bytes, "ascending car type hash order", "signed order");
    }
    {
        nfsu::CarPartLimits limits;
        limits.max_records = 6;
        expect_rejected(good.bytes, "7 part records; the limit is 6", "record limit", limits);
    }
    {
        TestDatabase db = base;
        db.attributes = {{1U, 2U}, {3U, 4U}};
        nfsu::CarPartLimits limits;
        limits.max_attributes = 1;
        expect_rejected(build(db).bytes, "2 part attributes; the limit is 1",
                        "attribute limit", limits);
    }
    {
        nfsu::CarPartLimits limits;
        limits.max_top_level_chunks = 2;
        expect_rejected(good.bytes, "chunk limit 2 exceeded", "top-level chunk limit",
                        limits);
    }
    {
        nfsu::CarPartLimits limits;
        limits.max_file_size = good.bytes.size() - 1;
        expect_rejected(good.bytes, "limit is", "file size limit", limits);
    }
    {
        // Every truncation is rejected without a crash, except the one that
        // only drops the unrelated trailing chunk.
        const std::size_t database_end = good.bytes.size() - 20;
        for (std::size_t size = 0; size < good.bytes.size(); ++size) {
            const nfsu::CarPartDatabase db =
                nfsu::parse_car_part_database(good.bytes.data(), size);
            check(db.ok() == (size == database_end),
                  "truncation to " + std::to_string(size) + " bytes");
        }
    }
    {
        const nfsu::CarPartDatabase db = nfsu::parse_car_part_database(nullptr, 16);
        check(!db.ok() && has_issue(db, "no input data"), "null data");
    }
    {
        // Single-byte mutations never crash, and a database that still
        // validates keeps its invariants.
        std::size_t accepted = 0;
        for (std::size_t at = 0; at < good.bytes.size(); ++at) {
            for (const unsigned char value : {0x00U, 0x01U, 0x7FU, 0xFFU}) {
                Bytes bytes = good.bytes;
                if (bytes[at] == value) {
                    continue;
                }
                bytes[at] = static_cast<unsigned char>(value);
                const nfsu::CarPartDatabase db = parse(bytes);
                if (!db.ok()) {
                    continue;
                }
                ++accepted;
                check(db.records.size() * nfsu::kCarPartRecordSize <= bytes.size(),
                      "mutated database record count");
                std::uint32_t previous = 0;
                for (const nfsu::CarPartRecord& record : db.records) {
                    check(record.car_type_hash >= previous, "mutated database order");
                    previous = record.car_type_hash;
                    check(record.slot >= 0 &&
                              record.slot < static_cast<int>(nfsu::kCarSlotCount),
                          "mutated database slot");
                    check(record.attribute_count >= 0 &&
                              (record.attribute_count == 0 ||
                               record.attribute_index +
                                       static_cast<std::uint32_t>(record.attribute_count) <=
                                   db.attributes.size()),
                          "mutated database attribute range");
                }
                nfsu::StockParts p;
                std::string ignored;
                nfsu::select_stock_parts(db, "TESTCAR", p, ignored);
            }
        }
        check(accepted > 0, "some mutations stay valid");
    }
}

// A synthetic car type with every stock rule exercised.
TestDatabase stock_database(const std::string& car_name) {
    const std::uint32_t car = nfsu::bin_hash(car_name);
    const std::uint32_t brakes = nfsu::bin_hash("BRAKES");
    const std::uint32_t plates = nfsu::bin_hash("PLATES");
    TestDatabase db;
    db.attributes = {
        {0x12345678U, nfsu::bin_hash("ROOF")},
        {nfsu::kDisableSlotAttributeKey, nfsu::bin_hash("FRONT_BUMPER")},
        {nfsu::kDisableSlotAttributeKey, nfsu::bin_hash("BODY")},
        {nfsu::kDisableSlotAttributeKey, nfsu::bin_hash("NOT_A_SLOT")},
        {nfsu::kDisableSlotAttributeKey, nfsu::bin_hash("SPOILER")},
    };
    std::vector<TestRecord> car_records;
    TestRecord base = record(car, 0, 0, 0x1000U, "BASE");
    base.attribute_index = 0;
    base.attribute_count = 2;  // disables FRONT_BUMPER before slot 6 runs
    car_records.push_back(base);
    car_records.push_back(record(car, 1, 1, 0x1100U, "KIT"));
    car_records.push_back(record(car, 1, 0, 0x1101U, "STOCK"));
    car_records.push_back(record(car, 1, 0, 0x1102U, "SECOND STOCK"));
    car_records.push_back(record(car, 6, 0, 0x1600U, "WIDE"));
    TestRecord roof = record(car, 7, 0, 0x1700U, "ROOF");
    roof.attribute_index = 2;
    roof.attribute_count = 2;  // disables BODY after slot 6 ran
    car_records.push_back(roof);
    TestRecord hood = record(car, 8, 0, 0x1800U, "HOOD");
    hood.second_attribute_index = 4;
    hood.second_attribute_count = 1;  // second list is not consulted
    car_records.push_back(hood);
    car_records.push_back(record(car, 11, 0, 0x1B00U, "SPOILER"));
    car_records.push_back(record(car, 12, 0, 0x1C00U, "ENGINE"));
    car_records.push_back(record(car, 16, 0, 0x2000U, "CAR BRAKE"));
    car_records.push_back(record(car, 19, 0, 0x2300U, "LEVEL0 HOOD DECAL"));
    car_records.push_back(record(car, 19, 1, nfsu::bin_hash(car_name + "_DECAL_HOOD_RECT_MEDIUM"),
                                 "HOOD RECT MEDIUM"));
    car_records.push_back(record(car, 20, 0, 0x2400U, "LEVEL0 WINDOW DECAL"));
    car_records.push_back(record(car, 22, 1, nfsu::bin_hash(car_name + "_DECAL_LEFT_DOOR_RECT_MEDIUM"),
                                 "L DOOR RECT MEDIUM"));
    car_records.push_back(record(car, 26, 0, 0x2600U, "WIDEBODY DECAL"));
    car_records.push_back(record(car, 31, 0, 0x3100U, "VINYL"));
    car_records.push_back(record(car, 119, 0, 0x7700U, "ColVol"));

    std::vector<TestRecord> brake_records = {
        record(brakes, 16, 1, 0x9001U, "BRAKE L1"),
        record(brakes, 16, 0, 0x9000U, "BRAKE STOCK"),
    };
    std::vector<TestRecord> plate_records = {
        record(plates, 17, 0, 0x9100U, "NOT A PLATE"),
        record(plates, 18, 2, 0x9101U, "PLATE"),
    };
    // Records sorted by car type hash.
    std::vector<std::pair<std::uint32_t, std::vector<TestRecord>*>> groups = {
        {car, &car_records}, {brakes, &brake_records}, {plates, &plate_records}};
    std::sort(groups.begin(), groups.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& group : groups) {
        db.records.insert(db.records.end(), group.second->begin(), group.second->end());
    }
    return db;
}

std::size_t index_of(const nfsu::CarPartDatabase& db, const std::string& name) {
    for (const nfsu::CarPartRecord& r : db.records) {
        if (r.name == name) {
            return r.index;
        }
    }
    return nfsu::kNoCarPart;
}

void test_stock_parts() {
    const std::string car_name = "TESTCAR";
    const nfsu::CarPartDatabase db = parse(build(stock_database(car_name)).bytes);
    check(db.ok(), "stock database parses");
    nfsu::StockParts parts;
    std::string error;
    check(nfsu::select_stock_parts(db, car_name, parts, error), "stock parts: " + error);
    check(parts.car_type_name == car_name && parts.car_type_hash == nfsu::bin_hash(car_name),
          "stock car type");
    const auto slot = [&](int s) -> const nfsu::StockPartSlot& {
        return parts.slots[static_cast<std::size_t>(s)];
    };
    using Rule = nfsu::StockPartRule;
    check(slot(0).record == index_of(db, "BASE") && slot(0).rule == Rule::upgrade_level_0,
          "slot 0 level-0 part");
    check(slot(1).record == index_of(db, "STOCK") && slot(1).rule == Rule::upgrade_level_0,
          "slot 1 takes the first level-0 record, not the first record");
    check(slot(2).record == nfsu::kNoCarPart && slot(2).rule == Rule::none,
          "slot without records stays empty");
    check(slot(6).record == nfsu::kNoCarPart && slot(6).rule == Rule::skipped,
          "wide body is skipped");
    check(slot(12).record == nfsu::kNoCarPart && slot(12).rule == Rule::skipped,
          "engine is skipped");
    check(slot(31).record == nfsu::kNoCarPart && slot(31).rule == Rule::skipped,
          "vinyl layer is skipped");
    check(slot(16).record == index_of(db, "BRAKE STOCK") && slot(16).rule == Rule::brakes,
          "brake comes from car type BRAKES level 0, not the car's own record");
    check(slot(18).record == index_of(db, "PLATE") &&
              slot(18).rule == Rule::plates_fallback,
          "license plate falls back to car type PLATES at any level");
    check(slot(19).record == index_of(db, "HOOD RECT MEDIUM") &&
              slot(19).rule == Rule::decal_name,
          "hood decal is chosen by name, replacing the level-0 record");
    check(slot(20).record == nfsu::kNoCarPart && slot(20).rule == Rule::none,
          "front window decal without a named record is emptied");
    check(slot(22).record == index_of(db, "L DOOR RECT MEDIUM") &&
              slot(22).rule == Rule::decal_name,
          "left door decal by name");
    check(slot(26).record == index_of(db, "WIDEBODY DECAL") &&
              slot(26).rule == Rule::upgrade_level_0,
          "only slots 19..25 are replaced by name; 26..29 keep their level-0 record");
    check(slot(119).record == index_of(db, "ColVol") && slot(119).rule == Rule::upgrade_level_0,
          "slot 119 level-0 part");

    // Enable bytes: slot 0 disables FRONT_BUMPER, but the no-wide-body rule
    // of slot 6 enables it again; slot 7 disables BODY after that rule.
    check(slot(1).enabled, "front bumper re-enabled by the wide body rule");
    check(!slot(5).enabled, "body disabled by a later part");
    check(slot(11).enabled, "second attribute list does not disable slots");
    check(!slot(6).enabled, "wide body slot disabled");
    check(slot(2).enabled && slot(10).enabled, "rear bumper and skirt enabled");
    for (int s = nfsu::kSlotDecalLeftDoor; s <= nfsu::kSlotDecalRightQuarter; ++s) {
        check(slot(s).enabled, "door and quarter decals enabled");
    }
    for (int s = nfsu::kSlotWideBodyDecalLeftDoor; s <= nfsu::kSlotWideBodyDecalRightQuarter;
         ++s) {
        check(!slot(s).enabled, "wide body decals disabled");
    }
    check(slot(119).enabled && slot(8).enabled, "other slots enabled");

    // Brakes without a level-0 record leave slot 16 empty.
    {
        TestDatabase raw = stock_database(car_name);
        for (TestRecord& r : raw.records) {
            if (r.name == "BRAKE STOCK") {
                r.level = 3;
            }
        }
        const nfsu::CarPartDatabase other = parse(build(raw).bytes);
        nfsu::StockParts p;
        check(nfsu::select_stock_parts(other, car_name, p, error), "no stock brake: " + error);
        check(p.slots[16].record == nfsu::kNoCarPart && p.slots[16].rule == Rule::none,
              "no level-0 brake record");
    }
    // A car-specific level-0 plate wins over the PLATES fallback.
    {
        TestDatabase raw = stock_database(car_name);
        TestRecord own = record(nfsu::bin_hash(car_name), 18, 0, 0x2200U, "OWN PLATE");
        for (std::size_t i = 0; i < raw.records.size(); ++i) {
            if (raw.records[i].name == "LEVEL0 HOOD DECAL") {
                raw.records.insert(raw.records.begin() + static_cast<std::ptrdiff_t>(i), own);
                break;
            }
        }
        const nfsu::CarPartDatabase other = parse(build(raw).bytes);
        nfsu::StockParts p;
        check(nfsu::select_stock_parts(other, car_name, p, error), "own plate: " + error);
        check(p.slots[18].record == index_of(other, "OWN PLATE") &&
                  p.slots[18].rule == Rule::upgrade_level_0,
              "car plate at level 0");
    }
}

void test_stock_errors() {
    const nfsu::CarPartDatabase db = parse(build(stock_database("TESTCAR")).bytes);
    nfsu::StockParts parts;
    parts.car_type_name = "stale";
    std::string error;
    check(!nfsu::select_stock_parts(db, "OTHERCAR", parts, error) &&
              error.find("no part record has car type") != std::string::npos,
          "unknown car type");
    check(parts.car_type_name.empty(), "output is reset on failure");
    check(!nfsu::select_stock_parts(db, "", parts, error), "empty car name");
    check(!nfsu::select_stock_parts(db, std::string(32, 'A'), parts, error),
          "car name longer than 31 bytes");
    check(!nfsu::select_stock_parts(db, "BAD\tNAME", parts, error), "control byte");
    const nfsu::CarPartDatabase invalid = parse(Bytes{});
    check(!nfsu::select_stock_parts(invalid, "TESTCAR", parts, error) &&
              error == "car part database is not valid",
          "invalid database");
}

nfsu::CarPartRecord lod_record(std::array<std::uint32_t, nfsu::kCarPartLodCount> hashes) {
    nfsu::CarPartRecord record;
    record.lod_mesh_hashes = hashes;
    return record;
}

void test_lod_for_pixel_size() {
    using nfsu::car_lod_for_pixel_size;
    check(car_lod_for_pixel_size(1000.0F, 0) == 0, "huge car uses LOD A");
    check(car_lod_for_pixel_size(120.0F, 0) == 0, "at the first threshold");
    check(car_lod_for_pixel_size(119.9F, 0) == 1, "just below the first threshold");
    check(car_lod_for_pixel_size(45.0F, 0) == 1, "at the second threshold");
    check(car_lod_for_pixel_size(44.9F, 0) == 2, "just below the second threshold");
    check(car_lod_for_pixel_size(20.0F, 0) == 2, "at the third threshold");
    check(car_lod_for_pixel_size(19.9F, 0) == 3, "just below the third threshold");
    // The game does not draw a car below kCarMinimumPixelSize at all.
    check(car_lod_for_pixel_size(static_cast<float>(nfsu::kCarMinimumPixelSize), 0) == 3,
          "smallest drawn size uses LOD D");
    check(car_lod_for_pixel_size(-1.0F, 0) == 3, "sizes below every threshold clamp");
    // A minimum LOD only ever raises the index (the split-screen and
    // reflection passes use 1, 2 and 3).
    check(car_lod_for_pixel_size(1000.0F, 1) == 1, "minimum LOD B");
    check(car_lod_for_pixel_size(1000.0F, 3) == 3, "minimum LOD D");
    check(car_lod_for_pixel_size(1000.0F, 9) == 3, "minimum LOD is clamped");
    check(car_lod_for_pixel_size(30.0F, 1) == 2, "minimum below the chosen LOD");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(car_lod_for_pixel_size(nan, 1) == 1, "NaN keeps the minimum LOD");
}

void test_part_models() {
    std::array<nfsu::PartModel, nfsu::kCarPartLodCount> models;
    std::string error;
    using Source = nfsu::PartModelSource;
    const auto resolve = [&](std::array<std::uint32_t, nfsu::kCarPartLodCount> hashes,
                             std::vector<std::uint32_t> loaded) {
        return nfsu::resolve_part_models(lod_record(hashes), loaded, models, error);
    };

    check(resolve({10, 11, 12, 13}, {10, 11, 12, 13}), "all present: " + error);
    for (std::size_t lod = 0; lod < nfsu::kCarPartLodCount; ++lod) {
        check(models[lod].hash == 10 + lod && models[lod].source == Source::requested &&
                  models[lod].resolved,
              "all present: LOD " + std::to_string(lod));
    }

    // A missing: the fallback starts at the LOD B hash.
    check(resolve({10, 11, 12, 13}, {11, 12, 13}), "A missing: " + error);
    check(models[0].requested_hash == 10 && models[0].hash == 11 &&
              models[0].source == Source::fallback && models[0].resolved,
          "A missing uses the LOD B mesh");
    check(models[1].hash == 11 && models[1].source == Source::requested, "B unchanged");

    // B and C missing: they use the nearest resolved higher-detail LOD.
    check(resolve({10, 11, 12, 13}, {10, 13}), "B and C missing: " + error);
    check(models[0].hash == 10 && models[0].source == Source::requested, "A resolved");
    check(models[1].hash == 10 && models[1].source == Source::fallback &&
              models[1].resolved,
          "B falls back to A");
    check(models[2].hash == 10 && models[2].source == Source::fallback,
          "C falls back to A, not to the missing B");
    check(models[3].hash == 13 && models[3].source == Source::requested, "D resolved");

    // D missing falls back to C, as the retail 240SX stock spoiler does.
    check(resolve({0xB5E01D44, 0xB5E01D45, 0xB5E01D46, 0xB5E01D47},
                  {0xB5E01D44, 0xB5E01D45, 0xB5E01D46}),
          "spoiler: " + error);
    check(models[3].requested_hash == 0xB5E01D47 && models[3].hash == 0xB5E01D46 &&
              models[3].source == Source::fallback && models[3].resolved,
          "stock spoiler D uses the C mesh");

    // A zero hash leaves the model empty and does not change the fallback.
    check(resolve({10, 11, 0, 13}, {10, 11}), "zero C: " + error);
    check(models[2].requested_hash == 0 && models[2].hash == 0 &&
              models[2].source == Source::none && !models[2].resolved,
          "zero hash draws nothing");
    check(models[3].hash == 11 && models[3].source == Source::fallback &&
              models[3].resolved,
          "D falls back over an empty C");

    // Nothing loaded at all: every model keeps a hash but resolves nothing.
    check(resolve({10, 11, 12, 13}, {}), "nothing loaded: " + error);
    check(models[0].hash == 11 && !models[0].resolved, "A falls back to the B hash");
    check(models[1].hash == 11 && models[1].source == Source::fallback &&
              !models[1].resolved,
          "B keeps its own hash through the fallback");
    check(models[2].hash == 11 && models[2].source == Source::fallback, "C uses B");

    // A zero LOD A leaves that model empty and does not consume the
    // fallback, which still starts at the LOD B hash.
    check(resolve({0, 11, 12, 13}, {11, 13}), "zero A: " + error);
    check(models[0].requested_hash == 0 && models[0].hash == 0 &&
              models[0].source == Source::none,
          "zero A draws nothing");
    check(models[1].hash == 11 && models[1].source == Source::requested,
          "B still resolves after a zero A");
    check(models[2].hash == 11 && models[2].source == Source::fallback,
          "C falls back to B after a zero A");

    // A record whose only hash is LOD A (decals and collision volumes).
    check(resolve({10, 0, 0, 0}, {}), "only A: " + error);
    check(models[0].requested_hash == 10 && models[0].hash == 0 &&
              models[0].source == Source::none,
          "a missing A with no LOD B hash stays empty");
    check(resolve({10, 0, 0, 0}, {10}), "only A loaded: " + error);
    check(models[0].source == Source::requested && models[1].source == Source::none,
          "only LOD A is filled");

    check(!nfsu::resolve_part_models(lod_record({10, 11, 12, 13}), {11, 10}, models, error) &&
              error.find("ascending") != std::string::npos,
          "unsorted loaded hashes");
    check(models[0].hash == 0, "models are cleared on failure");
    check(!nfsu::resolve_part_models(lod_record({10, 11, 12, 13}), {10, 10}, models, error),
          "duplicate loaded hashes");
}

void test_load_file() {
    const fs::path dir = fs::temp_directory_path() / "nfsu_car_parts_tests";
    std::error_code ignored;
    fs::create_directories(dir, ignored);
    const fs::path path = dir / "GLOBALB_TEST.BUN";
    const Bytes bytes = build(lookup_database()).bytes;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    const nfsu::CarPartDatabase db = nfsu::load_car_part_database(path);
    check(db.ok() && db.records.size() == 7, "load from file");
    nfsu::CarPartLimits limits;
    limits.max_file_size = 8;
    check(!nfsu::load_car_part_database(path, limits).ok(), "file size limit on load");
    fs::remove(path, ignored);
    const nfsu::CarPartDatabase missing = nfsu::load_car_part_database(path);
    check(!missing.ok() && !missing.issues.empty(), "missing file");
    fs::remove(dir, ignored);
}

} // namespace

int main() {
    test_slot_names();
    test_valid_database();
    test_find_car_part();
    test_malformed();
    test_stock_parts();
    test_stock_errors();
    test_lod_for_pixel_size();
    test_part_models();
    test_load_file();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "car part tests passed\n";
    return 0;
}
