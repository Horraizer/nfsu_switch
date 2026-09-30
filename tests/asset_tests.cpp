// The retail asset manifest and its check (issue #47): the sets against the
// files the rest of the project names, the naming rule, every classification
// over a fake file system and over real directories through the desktop and
// the console services, the bound on every read, the console's config file,
// and the whole report the console shows. docs/SWITCH_ASSETS.md.
//
// Nothing here reads retail data. The synthetic installs are a few kilobytes
// and one file of two and a half MiB, whose hashes are computed here; the
// real directories are written under the working directory, which CTest sets
// to this build tree, so two build trees testing at once never share one.

#include "nfsu/arg_path.h"
#include "nfsu/assets.h"
#include "nfsu/platform_fakes.h"
#include "nfsu/posix_file_system.h"
#include "nfsu/race_events.h"
#include "nfsu/sha256.h"
#include "nfsu/stock_frame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

void check_equal(const std::string& got, const std::string& want, const std::string& message) {
    if (got != want) {
        std::cerr << "test failure: " << message << "\n  got:  " << got << "\n  want: " << want
                  << '\n';
        ++g_failures;
    }
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::vector<unsigned char> pattern(std::size_t size, unsigned seed) {
    std::vector<unsigned char> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<unsigned char>((i * 31U + seed * 17U + (i >> 11U)) & 0xFFU);
    }
    return bytes;
}

std::string hash_of(const std::vector<unsigned char>& bytes) {
    return nfsu::sha256_hex(nfsu::sha256(bytes.data(), bytes.size()));
}

// ---------------------------------------------------------------------------
// A synthetic install: four files, one larger than two read chunks, one
// empty, in three directories and at two depths.
// ---------------------------------------------------------------------------

struct Synthetic {
    nfsu::AssetManifest manifest;
    std::vector<std::vector<unsigned char>> contents;
};

Synthetic synthetic() {
    Synthetic s;
    s.manifest.name = "synthetic";
    s.manifest.title = "four synthetic files";
    const char* const paths[] = {"Cars/240SX/GEOMETRY.BIN", "Global/GLOBALB.BUN",
                                 "Tracks/TRACKB1003.lzc", "Tracks/STREAML1RA_1003.BUN"};
    const std::size_t sizes[] = {3000, 2U * nfsu::kAssetReadChunkBytes + 524295U, 100, 0};
    for (std::size_t i = 0; i < 4; ++i) {
        s.contents.push_back(pattern(sizes[i], static_cast<unsigned>(i + 1)));
        s.manifest.files.push_back(nfsu::AssetFile{paths[i], sizes[i], hash_of(s.contents[i]),
                                                   "synthetic"});
    }
    return s;
}

std::uint64_t synthetic_bytes(const Synthetic& s) {
    return nfsu::asset_manifest_bytes(s.manifest);
}

// Every file of `s` under `root` in the fake, spelled as `spell` says.
void install(nfsu::FakeFileSystem& files, const std::string& root, const Synthetic& s,
             const std::vector<std::string>& spell = {}) {
    for (std::size_t i = 0; i < s.manifest.files.size(); ++i) {
        const std::string path = i < spell.size() && !spell[i].empty()
                                     ? spell[i]
                                     : s.manifest.files[i].path;
        files.add_file(root + "/" + path, s.contents[i]);
    }
}

nfsu::AssetCheckReport run_check(nfsu::FileSystemService& files, const std::string& root,
                                 const nfsu::AssetManifest& manifest,
                                 nfsu::AssetCheckObserver* observer = nullptr) {
    nfsu::AssetCheckReport report;
    std::string reason;
    const bool ran = nfsu::check_asset_install(files, fs::path(root), manifest, report, reason,
                                               observer);
    check(ran, "the check ran over a valid manifest: " + reason);
    return report;
}

std::vector<std::string> texts(const std::vector<nfsu::AssetLine>& lines) {
    std::vector<std::string> out;
    for (const nfsu::AssetLine& line : lines) {
        out.push_back(line.text);
    }
    return out;
}

void check_lines(const std::vector<std::string>& got, const std::vector<std::string>& want,
                 const std::string& label) {
    if (got.size() != want.size()) {
        std::cerr << "test failure: " << label << ": " << got.size() << " lines, expected "
                  << want.size() << '\n';
        for (const std::string& line : got) {
            std::cerr << "  | " << line << '\n';
        }
        ++g_failures;
        return;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        check_equal(got[i], want[i], label + ", line " + std::to_string(i));
    }
}

std::size_t reads_of(const nfsu::FakeFileSystem& files, const std::string& tree_name) {
    std::size_t count = 0;
    for (const auto& call : files.reads()) {
        if (call.path == tree_name) {
            ++count;
        }
    }
    return count;
}

class Recorder final : public nfsu::AssetCheckObserver {
public:
    void file_started(std::size_t index, std::size_t total, const nfsu::AssetFile& file) override {
        started.push_back(std::to_string(index) + "/" + std::to_string(total) + " " + file.path);
    }
    void file_progress(std::size_t index, std::uint64_t hashed, std::uint64_t size) override {
        progress.push_back(std::to_string(index) + " " + std::to_string(hashed) + "/" +
                           std::to_string(size));
    }
    std::vector<std::string> started;
    std::vector<std::string> progress;
};

// ---------------------------------------------------------------------------
// The retail sets
// ---------------------------------------------------------------------------

struct Pinned {
    const char* path;
    std::uint64_t size;
    const char* sha256;
};

// The table of docs/SWITCH_ASSETS.md, which is this list; a change to either
// has to be made to both.
const Pinned kPinned[] = {
    {"Cars/240SX/GEOMETRY.BIN", 4780816,
     "002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619"},
    {"Cars/240SX/TEXTURES.BIN", 697340,
     "725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA"},
    {"Cars/BRAKES/GEOMETRY.BIN", 69408,
     "FEC4CBF4F9C3E7FB1CD883F0B874F920248652A2E63EE4A341446563F0B9B1A5"},
    {"Cars/PLATES/GEOMETRY.BIN", 1184,
     "0EBCFD14DD303613F5DA3FF5D7A6DF57BE3CDE915F458BA243FE5D089B64BD8A"},
    {"Cars/TEXTURES.BIN", 1230808,
     "999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58"},
    {"FrontEnd/FrontB.lzc", 4182578,
     "76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90"},
    {"Global/DYNTEX.BIN", 4677572,
     "020E99EBEA6A4AEFC7F7C858BA8672A48FA040E00299E2372E914687BF90525C"},
    {"Global/GLOBALB.BUN", 2361176,
     "39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9"},
    {"Global/INGAMEB.BUN", 1670080,
     "044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299"},
    {"Languages/LanguageTextures.bin", 517284,
     "6A92604D9766557115DEE8F8CE18F392557AF1A8B0369E69B474F36DF2691615"},
    {"Tracks/STREAML1RA.BUN", 92394688,
     "2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E"},
    {"Tracks/STREAML1RA_1003.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"},
    {"Tracks/TEX1003TRACK.BIN", 1086216,
     "081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5"},
    {"Tracks/TRACKB1003.lzc", 433352,
     "ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA"},
};

std::string base_name(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const nfsu::AssetFile* find_file(const nfsu::AssetManifest& manifest, const std::string& name) {
    for (const nfsu::AssetFile& file : manifest.files) {
        if (nfsu::asset_names_match(base_name(file.path), name)) {
            return &file;
        }
    }
    return nullptr;
}

void test_retail_sets() {
    const std::vector<std::string> names = nfsu::retail_asset_manifest_names();
    check(names == std::vector<std::string>{"first-lap", "stock-car", "event-1"},
          "the three retail sets, first-lap first");

    nfsu::AssetManifest first;
    nfsu::AssetManifest car;
    nfsu::AssetManifest event;
    check(nfsu::retail_asset_manifest("first-lap", first), "first-lap is a set");
    check(nfsu::retail_asset_manifest("stock-car", car), "stock-car is a set");
    check(nfsu::retail_asset_manifest("event-1", event), "event-1 is a set");
    nfsu::AssetManifest untouched;
    untouched.name = "keep";
    check(!nfsu::retail_asset_manifest("First-Lap", untouched) && untouched.name == "keep",
          "a set name is matched exactly and a miss leaves the manifest alone");

    for (const nfsu::AssetManifest* manifest : {&first, &car, &event}) {
        std::string reason;
        check(nfsu::validate_asset_manifest(*manifest, reason),
              manifest->name + " validates: " + reason);
    }
    check(first.files.size() == 14 && car.files.size() == 6 && event.files.size() == 7,
          "14, 6 and 7 files");
    check(nfsu::asset_manifest_bytes(first) == 114102502ULL, "first-lap bytes");
    check(nfsu::asset_manifest_bytes(car) == 9140732ULL, "stock-car bytes");
    check(nfsu::asset_manifest_bytes(event) == 102128090ULL, "event-1 bytes");

    // first-lap is the two halves together, each file once, in path order.
    std::set<std::string> halves;
    for (const nfsu::AssetFile& file : car.files) {
        halves.insert(file.path);
    }
    for (const nfsu::AssetFile& file : event.files) {
        halves.insert(file.path);
    }
    halves.insert("Global/DYNTEX.BIN");
    halves.insert("Languages/LanguageTextures.bin");
    std::vector<std::string> first_paths;
    for (const nfsu::AssetFile& file : first.files) {
        first_paths.push_back(file.path);
    }
    check(std::vector<std::string>(halves.begin(), halves.end()) == first_paths,
          "first-lap adds the HUD inputs to stock-car and event-1, sorted");

    // The pinned table, file by file.
    check(first.files.size() == sizeof(kPinned) / sizeof(kPinned[0]), "pinned count");
    for (std::size_t i = 0; i < first.files.size() && i < 14; ++i) {
        check_equal(first.files[i].path, kPinned[i].path, "pinned path " + std::to_string(i));
        check(first.files[i].size == kPinned[i].size, "pinned size of " + first.files[i].path);
        check_equal(first.files[i].sha256, kPinned[i].sha256,
                    "pinned sha256 of " + first.files[i].path);
        check(!first.files[i].purpose.empty(), "a purpose for " + first.files[i].path);
    }
    // Every set's entry for a path is the same entry.
    for (const nfsu::AssetManifest* half : {&car, &event}) {
        for (const nfsu::AssetFile& file : half->files) {
            bool same = false;
            for (const nfsu::AssetFile& other : first.files) {
                same = same || (other.path == file.path && other.size == file.size &&
                                other.sha256 == file.sha256);
            }
            check(same, half->name + " and first-lap agree on " + file.path);
        }
    }

    // stock-car is exactly the six files nfsu_switch_car reads, spelled the
    // same way.
    std::set<std::string> frame;
    for (std::size_t i = 0; i < nfsu::kStockFrameFileCount; ++i) {
        frame.insert(nfsu::stock_frame_file_path(i));
    }
    std::set<std::string> car_paths;
    for (const nfsu::AssetFile& file : car.files) {
        car_paths.insert(file.path);
    }
    check(frame == car_paths, "stock-car is the stock frame's six inputs");

    // event-1's track files are the names the game formats for track 1003
    // in region L1RA - found under the naming rule, since the game spells
    // the bundle TRACKB1003.LZC and the installation TRACKB1003.lzc.
    nfsu::TrackInfoRecord track;
    track.region = "L1RA";
    track.number = 1003;
    const std::string formatted[] = {
        nfsu::track_stream_file_name(track), nfsu::track_stream_pack_file_name(track),
        nfsu::track_texture_file_name(1003), nfsu::track_bundle_file_name(1003)};
    for (const std::string& name : formatted) {
        const nfsu::AssetFile* file = find_file(event, name);
        check(file != nullptr && file->path.compare(0, 7, "Tracks/") == 0,
              "event-1 holds the game's " + name + " under Tracks/");
    }
    check(nfsu::track_bundle_file_name(1003) == "TRACKB1003.LZC",
          "the game spells the bundle in upper case");
    const nfsu::AssetFile* bundle = find_file(event, "TRACKB1003.LZC");
    check(bundle != nullptr && bundle->path == "Tracks/TRACKB1003.lzc",
          "and the set spells it as the installation does");
    check(find_file(event, "FrontB.lzc") != nullptr && find_file(event, "GLOBALB.BUN") != nullptr &&
              find_file(event, "INGAMEB.BUN") != nullptr,
          "event-1 holds the event table, GLOBALB and INGAMEB");
    check(find_file(first, "GlobalB.lzc") == nullptr && find_file(first, "GLOBALA.BUN") == nullptr,
          "no set holds the two files the owner's installation carries modified");
}

void test_naming_rule() {
    check(nfsu::asset_names_match("TRACKB1003.LZC", "trackb1003.lzc"), "case folds");
    check(nfsu::asset_names_match("Tracks", "tRACKS"), "mixed case folds");
    check(nfsu::asset_names_match("", ""), "two empty names match");
    check(!nfsu::asset_names_match("Tracks", "Track"), "lengths differ");
    // Review comment 3761, N3 (M3): the shorter name first as well.
    check(!nfsu::asset_names_match("Track", "Tracks"), "a prefix is not a match");
    check(!nfsu::asset_names_match("", "a"), "nor is the empty name");
    check(!nfsu::asset_names_match("GLOBALB.BUN", "GLOBALB.BIN"), "one letter differs");
    check(!nfsu::asset_names_match("@", "`"), "0x40 and 0x60 are not a letter pair");
    check(!nfsu::asset_names_match("[", "{"), "0x5B and 0x7B are not a letter pair");
    check(!nfsu::asset_names_match("\xC0", "\xE0"), "bytes above 0x7F are not folded");
    check(nfsu::asset_names_match("\xD0\xA0x", "\xD0\xA0X"), "they match only themselves");
}

void test_validation() {
    const Synthetic base = synthetic();
    std::string reason;
    check(nfsu::validate_asset_manifest(base.manifest, reason), "the synthetic set validates");

    struct Case {
        const char* label;
        void (*change)(nfsu::AssetManifest&);
        const char* needle;
    };
    const Case cases[] = {
        {"no name", [](nfsu::AssetManifest& m) { m.name.clear(); }, "has no name"},
        {"no files", [](nfsu::AssetManifest& m) { m.files.clear(); }, "holds 0 files"},
        {"65 files",
         [](nfsu::AssetManifest& m) {
             m.files.clear();
             for (int i = 0; i < 65; ++i) {
                 m.files.push_back(nfsu::AssetFile{"f" + std::to_string(i), 0,
                                                   std::string(64, 'A'), ""});
             }
         },
         "holds 65 files"},
        {"empty path", [](nfsu::AssetManifest& m) { m.files[0].path.clear(); }, "path of 0 bytes"},
        {"long path",
         [](nfsu::AssetManifest& m) { m.files[0].path = std::string(241, 'a'); },
         "path of 241 bytes"},
        {"backslash", [](nfsu::AssetManifest& m) { m.files[0].path = "Cars\\x"; }, "0x5C"},
        {"colon", [](nfsu::AssetManifest& m) { m.files[0].path = "sdmc:/x"; }, "0x3A"},
        {"non-ASCII", [](nfsu::AssetManifest& m) { m.files[0].path = "Cars/\xD0\xA0"; }, "0xD0"},
        {"parent", [](nfsu::AssetManifest& m) { m.files[0].path = "Cars/../x"; },
         "empty, \".\" or \"..\" part"},
        {"dot", [](nfsu::AssetManifest& m) { m.files[0].path = "./x"; }, "empty, \".\""},
        {"empty part", [](nfsu::AssetManifest& m) { m.files[0].path = "Cars//x"; },
         "empty, \".\""},
        {"absolute", [](nfsu::AssetManifest& m) { m.files[0].path = "/x"; }, "empty, \".\""},
        {"trailing slash", [](nfsu::AssetManifest& m) { m.files[0].path = "x/"; }, "empty, \".\""},
        {"too large",
         [](nfsu::AssetManifest& m) { m.files[0].size = nfsu::kAssetMaxFileBytes + 1; },
         "268435457 bytes"},
        {"lower-case hash",
         [](nfsu::AssetManifest& m) { m.files[0].sha256 = std::string(64, 'a'); },
         "not 64 upper-case"},
        {"short hash",
         [](nfsu::AssetManifest& m) { m.files[0].sha256 = std::string(63, 'A'); },
         "not 64 upper-case"},
        // Review comment 3761, N3 (M12): upper case, and not hexadecimal.
        {"digit G",
         [](nfsu::AssetManifest& m) { m.files[0].sha256 = std::string(63, 'A') + "G"; },
         "not 64 upper-case"},
        {"same file twice",
         [](nfsu::AssetManifest& m) { m.files[3].path = "tracks/trackb1003.LZC"; },
         "names the same file as file 2"},
    };
    for (const Case& c : cases) {
        nfsu::AssetManifest manifest = base.manifest;
        c.change(manifest);
        reason.clear();
        const bool valid = nfsu::validate_asset_manifest(manifest, reason);
        check(!valid && contains(reason, c.needle),
              std::string("refused: ") + c.label + " - " + reason);
        nfsu::AssetCheckReport report;
        report.manifest = "untouched";
        nfsu::FakeFileSystem files;
        std::string check_reason;
        check(!nfsu::check_asset_install(files, fs::path("sdmc:/nfsu"), manifest, report,
                                         check_reason) &&
                  report.manifest == "untouched" && files.listed().empty(),
              std::string("the check refuses it before listing anything: ") + c.label);
    }
    nfsu::AssetManifest at_limit = base.manifest;
    at_limit.files[0].size = nfsu::kAssetMaxFileBytes;
    at_limit.files[0].path = std::string(240, 'a');
    check(nfsu::validate_asset_manifest(at_limit, reason), "the bounds themselves are allowed");
}

// ---------------------------------------------------------------------------
// The check over the fake
// ---------------------------------------------------------------------------

void test_complete() {
    const Synthetic s = synthetic();
    nfsu::FakeFileSystem files;
    install(files, "sdmc:/nfsu", s);
    Recorder recorder;
    const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest, &recorder);
    check(report.verdict == nfsu::AssetVerdict::complete, "complete");
    check(report.root_listed && report.root_problem.empty(), "the root was listed");
    check(report.count(nfsu::AssetStatus::ok) == 4, "four files ok");
    for (const nfsu::AssetFileReport& file : report.files) {
        check(file.status == nfsu::AssetStatus::ok && file.hashed && file.sized &&
                  file.found_as == file.expected.path && file.detail.empty() &&
                  file.bytes_read == file.expected.size && file.sha256 == file.expected.sha256,
              "ok in every field: " + file.expected.path);
    }
    // Exactly the files' bytes: every file read once from its first byte to
    // its last, and one byte asked for past the end, which was not there.
    check(report.bytes_read == synthetic_bytes(s), "bytes read are the files' bytes");
    const std::size_t big = 2U * nfsu::kAssetReadChunkBytes + 524295U;
    check(reads_of(files, "sdmc:/nfsu/Global/GLOBALB.BUN") == 4,
          "the big file in three chunks and a probe");
    check(reads_of(files, "sdmc:/nfsu/Tracks/STREAML1RA_1003.BUN") == 1,
          "the empty file only probed");
    check(report.reads == files.reads().size() && report.reads == 2 + 4 + 2 + 1,
          "every read counted");
    std::uint64_t expected_offset = 0;
    for (const auto& call : files.reads()) {
        check(call.count <= nfsu::kAssetReadChunkBytes, "no read over one chunk");
        if (call.path == "sdmc:/nfsu/Global/GLOBALB.BUN") {
            if (expected_offset < big) {
                check(call.offset == expected_offset, "the big file read in order");
                expected_offset += call.got;
            } else {
                check(call.offset == big && call.count == 1 && call.got == 0,
                      "the probe asks for one byte at the end");
            }
        }
    }
    // Each directory listed once: the root, Cars, Cars/240SX, Global, Tracks.
    check(report.listings == 5 && files.listed().size() == 5, "five listings, each once");
    check(recorder.started ==
              std::vector<std::string>{"0/4 Cars/240SX/GEOMETRY.BIN", "1/4 Global/GLOBALB.BUN",
                                       "2/4 Tracks/TRACKB1003.lzc",
                                       "3/4 Tracks/STREAML1RA_1003.BUN"},
          "the observer hears every file in order");
    check(recorder.progress ==
              std::vector<std::string>{"0 3000/3000", "1 1048576/2621447", "1 2097152/2621447",
                                       "1 2621447/2621447", "2 100/100"},
          "and every chunk");

    check_lines(texts(nfsu::asset_report_lines(report, "sdmc:/nfsu",
                                               nfsu::AssetReportForm::console)),
                {
                    "set synthetic: four synthetic files, 4 files, 2624547 bytes",
                    "install: COMPLETE - all 4 files match by name, size and SHA-256",
                    "nothing to copy",
                    "ok            Cars/240SX/GEOMETRY.BIN  3000 bytes, sha256 matches",
                    "ok            Global/GLOBALB.BUN  2621447 bytes, sha256 matches",
                    "ok            Tracks/TRACKB1003.lzc  100 bytes, sha256 matches",
                    "ok            Tracks/STREAML1RA_1003.BUN  0 bytes, sha256 matches",
                    "read 2624547 bytes in 9 reads and 5 directory listings",
                },
                "the complete report, console form: the verdict first");
    check_lines(texts(nfsu::asset_report_lines(report, "D:/nfsu",
                                               nfsu::AssetReportForm::desktop)),
                {
                    "set synthetic: four synthetic files, 4 files, 2624547 bytes",
                    "ok            Cars/240SX/GEOMETRY.BIN  3000 bytes, sha256 " +
                        s.manifest.files[0].sha256,
                    "ok            Global/GLOBALB.BUN  2621447 bytes, sha256 " +
                        s.manifest.files[1].sha256,
                    "ok            Tracks/TRACKB1003.lzc  100 bytes, sha256 " +
                        s.manifest.files[2].sha256,
                    "ok            Tracks/STREAML1RA_1003.BUN  0 bytes, sha256 " +
                        s.manifest.files[3].sha256,
                    "install: COMPLETE - all 4 files match by name, size and SHA-256",
                    "nothing to copy",
                    "read 2624547 bytes in 9 reads and 5 directory listings",
                },
                "the complete report, desktop form: the files first");
    const std::vector<nfsu::AssetLine> full =
        nfsu::asset_report_lines(report, "D:/nfsu", nfsu::AssetReportForm::desktop);
    check_equal(full[1].text,
                "ok            Cars/240SX/GEOMETRY.BIN  3000 bytes, sha256 " +
                    s.manifest.files[0].sha256,
                "the desktop form prints the hash");
    check(full[1].tone == nfsu::AssetTone::good && full[5].tone == nfsu::AssetTone::good,
          "ok lines and the verdict are good");
}

void test_missing() {
    const Synthetic s = synthetic();
    {
        nfsu::FakeFileSystem fewer;
        for (std::size_t i = 0; i < 4; ++i) {
            if (i != 2) {
                fewer.add_file("sdmc:/nfsu/" + s.manifest.files[i].path, s.contents[i]);
            }
        }
        const nfsu::AssetCheckReport report = run_check(fewer, "sdmc:/nfsu", s.manifest);
        check(report.verdict == nfsu::AssetVerdict::missing_files, "one missing file");
        check(report.files[2].status == nfsu::AssetStatus::missing &&
                  report.files[2].detail == "no TRACKB1003.lzc in Tracks" &&
                  report.files[2].found_as == "Tracks" && !report.files[2].sized,
              "named with the directory it is not in: " + report.files[2].detail);
        check(report.count(nfsu::AssetStatus::ok) == 3, "the other three still checked");
        const std::vector<std::string> lines =
            texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::desktop));
        check_lines(std::vector<std::string>(lines.begin() + 5, lines.end()),
                    {
                        "install: MISSING FILES - 3 of 4 files ok: 1 missing, 0 wrong size, 0 "
                        "wrong version, 0 unreadable",
                        "copy this file from the game's installation folder, keeping the "
                        "folders:",
                        "  Tracks/TRACKB1003.lzc -> sdmc:/nfsu/Tracks/TRACKB1003.lzc",
                        "read 2624447 bytes in 7 reads and 5 directory listings",
                    },
                    "the missing report's tail");
        check_equal(lines[3], "MISSING       Tracks/TRACKB1003.lzc  no TRACKB1003.lzc in Tracks",
                    "the missing line");
        check_lines(texts(nfsu::asset_report_lines(report, "sdmc:/nfsu",
                                                   nfsu::AssetReportForm::console)),
                    {
                        "set synthetic: four synthetic files, 4 files, 2624547 bytes",
                        "install: MISSING FILES - 3 of 4 files ok: 1 missing, 0 wrong size, 0 "
                        "wrong version, 0 unreadable",
                        "copy this file from the game's installation folder to the same path "
                        "under sdmc:/nfsu:",
                        "  Tracks/TRACKB1003.lzc  100 bytes",
                        "ok            Cars/240SX/GEOMETRY.BIN  3000 bytes, sha256 matches",
                        "ok            Global/GLOBALB.BUN  2621447 bytes, sha256 matches",
                        "MISSING       Tracks/TRACKB1003.lzc  no TRACKB1003.lzc in Tracks",
                        "ok            Tracks/STREAML1RA_1003.BUN  0 bytes, sha256 matches",
                        "read 2624447 bytes in 7 reads and 5 directory listings",
                    },
                    "the missing report, console form");
    }
    {
        // A whole directory missing: both files under it, named by the
        // directory, and it is never listed.
        nfsu::FakeFileSystem files;
        files.add_file("sdmc:/nfsu/Cars/240SX/GEOMETRY.BIN", s.contents[0]);
        files.add_file("sdmc:/nfsu/Global/GLOBALB.BUN", s.contents[1]);
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.files[2].detail == "no Tracks in the root" &&
                  report.files[3].detail == "no Tracks in the root",
              "a missing directory is named");
        check(report.count(nfsu::AssetStatus::missing) == 2 && report.listings == 4,
              "and nothing under it is listed");
        const std::vector<std::string> lines = texts(
            nfsu::asset_report_lines(report, "sdmc:/nfsu/", nfsu::AssetReportForm::desktop));
        check_equal(lines[6], "copy these 2 files from the game's installation folder, keeping "
                              "the folders:",
                    "the plural copy line");
        check_equal(lines[8],
                    "  Tracks/STREAML1RA_1003.BUN -> sdmc:/nfsu/Tracks/STREAML1RA_1003.BUN",
                    "a root spelled with a trailing slash is not doubled");
        const std::vector<std::string> console = texts(
            nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::console));
        check_equal(console[2], "copy these 2 files from the game's installation folder to the "
                                "same paths under sdmc:/nfsu:",
                    "the console's plural copy line");
    }
}

void test_wrong_size() {
    const Synthetic s = synthetic();
    for (const std::size_t size : {std::size_t{60}, std::size_t{101}}) {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        std::vector<unsigned char> changed = s.contents[2];
        changed.resize(size, 0x55);
        files.add_file("sdmc:/nfsu/Tracks/TRACKB1003.lzc", changed);
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        const nfsu::AssetFileReport& file = report.files[2];
        check(report.verdict == nfsu::AssetVerdict::partial, "wrong size is partial");
        check(file.status == nfsu::AssetStatus::wrong_size && file.sized && file.size == size &&
                  !file.hashed && file.bytes_read == 0,
              "sized, never hashed");
        check(reads_of(files, "sdmc:/nfsu/Tracks/TRACKB1003.lzc") == 0,
              "a file of the wrong size is never read");
        check(contains(file.detail, size < 100 ? "shorter than the retail file: an interrupted copy?"
                                               : "longer than the retail file"),
              "says which way: " + file.detail);
        const std::vector<std::string> lines =
            texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::desktop));
        const std::vector<std::string> console =
            texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::console));
        check_equal(console[6],
                    "WRONG SIZE    Tracks/TRACKB1003.lzc  " + std::to_string(size) +
                        " bytes, expected 100: " +
                        (size < 100 ? "shorter: an interrupted copy?" : "longer"),
                    "the console's wrong-size line");
        check_equal(console[3], "  Tracks/TRACKB1003.lzc  100 bytes", "the console's copy line");
        check_equal(lines[3],
                    "WRONG SIZE    Tracks/TRACKB1003.lzc  " + std::to_string(size) +
                        " bytes, expected 100: " + file.detail,
                    "the wrong-size line");
        check_equal(lines[5],
                    "install: PARTIAL - 3 of 4 files ok: 0 missing, 1 wrong size, 0 wrong "
                    "version, 0 unreadable",
                    "the partial verdict");
    }
}

void test_wrong_hash() {
    const Synthetic s = synthetic();
    nfsu::FakeFileSystem files;
    install(files, "sdmc:/nfsu", s);
    std::vector<unsigned char> changed = s.contents[1];
    changed[nfsu::kAssetReadChunkBytes + 12] ^= 0x01U;
    files.add_file("sdmc:/nfsu/Global/GLOBALB.BUN", changed);
    const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
    const nfsu::AssetFileReport& file = report.files[1];
    check(report.verdict == nfsu::AssetVerdict::wrong_version, "wrong version");
    check(file.status == nfsu::AssetStatus::wrong_hash && file.hashed &&
              file.sha256 == hash_of(changed) && file.bytes_read == changed.size(),
          "the whole file hashed, one bit off");
    const std::vector<std::string> lines =
        texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::console));
    check_equal(lines[5],
                "WRONG VERSION Global/GLOBALB.BUN  2621447 bytes, sha256 " +
                    hash_of(changed).substr(0, 8) + "... expected " +
                    s.manifest.files[1].sha256.substr(0, 8) + "...",
                "the console's short hashes");
    const std::vector<std::string> full =
        texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::desktop));
    check_equal(full[2],
                "WRONG VERSION Global/GLOBALB.BUN  2621447 bytes, sha256 " + hash_of(changed) +
                    " expected " + s.manifest.files[1].sha256,
                "the desktop's full hashes");
    check_equal(lines[1],
                "install: WRONG VERSION - 3 of 4 files ok: 0 missing, 0 wrong size, 1 wrong "
                "version, 0 unreadable",
                "the wrong-version verdict, first on the console");
    check_equal(full[5],
                "install: WRONG VERSION - 3 of 4 files ok: 0 missing, 0 wrong size, 1 wrong "
                "version, 0 unreadable",
                "and after the files on the desktop");
}

void test_case_insensitive() {
    const Synthetic s = synthetic();
    nfsu::FakeFileSystem files;
    install(files, "sdmc:/nfsu", s,
            {"cars/240sx/geometry.bin", "GLOBAL/globalb.bun", "tracks/trackb1003.LZC",
             "tracks/STREAML1RA_1003.bun"});
    const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
    check(report.verdict == nfsu::AssetVerdict::complete, "every spelling found");
    check(report.files[0].found_as == "cars/240sx/geometry.bin" &&
              report.files[2].found_as == "tracks/trackb1003.LZC",
          "found as the file system spells it");
    check(report.listings == 5, "tracks listed once for both of its files");
    const std::vector<std::string> lines =
        texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::console));
    check_equal(lines[5],
                "ok            Tracks/TRACKB1003.lzc  100 bytes, sha256 matches (found as "
                "tracks/trackb1003.LZC)",
                "the line says where");
}

void test_ambiguous() {
    const Synthetic s = synthetic();
    {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        files.add_file("sdmc:/nfsu/Tracks/TRACKB1003.LZC", s.contents[2]);
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.files[2].status == nfsu::AssetStatus::ambiguous &&
                  report.files[2].detail ==
                      "Tracks holds 2 names that match TRACKB1003.lzc: \"TRACKB1003.LZC\" "
                      "\"TRACKB1003.lzc\"",
              "two spellings of one file: " + report.files[2].detail);
        check(report.verdict == nfsu::AssetVerdict::unreadable, "ambiguous alone is unreadable");
        check(reads_of(files, "sdmc:/nfsu/Tracks/TRACKB1003.lzc") == 0 &&
                  reads_of(files, "sdmc:/nfsu/Tracks/TRACKB1003.LZC") == 0,
              "neither is read");
        const std::vector<std::string> lines =
            texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::desktop));
        check_equal(lines[5],
                    "install: UNREADABLE - 3 of 4 files ok: 0 missing, 0 wrong size, 0 wrong "
                    "version, 1 unreadable",
                    "ambiguous counts as unreadable");
        // Review comment 3761, N2: the lookup stopped at Tracks, so there is
        // no place the file was found as.
        check(!report.files[2].found && report.files[2].found_as == "Tracks", "not found");
        check_equal(lines[3],
                    "AMBIGUOUS     Tracks/TRACKB1003.lzc  Tracks holds 2 names that match "
                    "TRACKB1003.lzc: \"TRACKB1003.LZC\" \"TRACKB1003.lzc\"",
                    "the ambiguous line names no partial path as found");
    }
    {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        files.add_directory("sdmc:/nfsu/TRACKS");
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.count(nfsu::AssetStatus::ambiguous) == 2 &&
                  contains(report.files[3].detail, "the root holds 2 names that match Tracks"),
              "two spellings of one directory: " + report.files[3].detail);
    }
}

void test_unreadable() {
    const Synthetic s = synthetic();
    {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        files.fail_listing("sdmc:/nfsu/Tracks", "cannot be listed (errno 5)");
        files.fail_read("sdmc:/nfsu/Global/GLOBALB.BUN", nfsu::kAssetReadChunkBytes + 5,
                        "cannot be read at byte 1048576");
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.verdict == nfsu::AssetVerdict::unreadable, "unreadable");
        check(report.files[2].status == nfsu::AssetStatus::unreadable &&
                  report.files[2].detail == "Tracks cannot be listed: cannot be listed (errno 5)",
              "a directory that will not list: " + report.files[2].detail);
        check(report.files[1].status == nfsu::AssetStatus::unreadable &&
                  report.files[1].detail == "cannot be read at byte 1048576" &&
                  report.files[1].bytes_read == nfsu::kAssetReadChunkBytes && !report.files[1].hashed,
              "a read that fails part way: " + report.files[1].detail);
        check(report.count(nfsu::AssetStatus::unreadable) == 3, "three unreadable");
        const std::vector<std::string> lines = texts(
            nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::desktop));
        check_equal(lines[3],
                    "UNREADABLE    Tracks/TRACKB1003.lzc  Tracks cannot be listed: cannot be "
                    "listed (errno 5)",
                    "a folder that will not list names no partial path as found");
    }
    {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        files.add_file("sdmc:/nfsu/Tracks/TRACKB1003.lzc/inner", {1, 2, 3});
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.files[2].status == nfsu::AssetStatus::unreadable &&
                  report.files[2].detail == "is a directory, not a file",
              "a directory where the file should be: " + report.files[2].detail);
    }
    {
        // Sized at 100 and holding 101: the probe past the end finds a byte.
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        std::vector<unsigned char> longer = s.contents[2];
        longer.push_back(7);
        files.add_file("sdmc:/nfsu/Tracks/TRACKB1003.lzc", longer);
        files.report_size("sdmc:/nfsu/Tracks/TRACKB1003.lzc", 100);
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.files[2].status == nfsu::AssetStatus::unreadable &&
                  report.files[2].detail ==
                      "holds more than the 100 bytes it was sized at: it changed while it was "
                      "checked" &&
                  !report.files[2].hashed,
              "a file that grew: " + report.files[2].detail);
    }
    {
        // Sized at 100 and holding 90: the reads run out.
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        std::vector<unsigned char> shorter = s.contents[2];
        shorter.resize(90);
        files.add_file("sdmc:/nfsu/Tracks/TRACKB1003.lzc", shorter);
        files.report_size("sdmc:/nfsu/Tracks/TRACKB1003.lzc", 100);
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.files[2].status == nfsu::AssetStatus::unreadable &&
                  report.files[2].detail ==
                      "ended after 90 of 100 bytes: it changed while it was checked",
              "a file that shrank: " + report.files[2].detail);
    }
}

void test_root() {
    const Synthetic s = synthetic();
    {
        nfsu::FakeFileSystem files;
        files.add_directory("sdmc:/other");
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(!report.root_listed && report.root_problem == "does not exist",
              "an absent root: " + report.root_problem);
        check(report.count(nfsu::AssetStatus::missing) == 4 &&
                  report.verdict == nfsu::AssetVerdict::missing_files && report.listings == 1,
              "every file missing, the root asked for once");
        const std::vector<std::string> lines =
            texts(nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::desktop));
        check_equal(lines[1], "root sdmc:/nfsu cannot be listed: it does not exist",
                    "the root line");
        check_equal(lines[2],
                    "MISSING       Cars/240SX/GEOMETRY.BIN  the root does not exist",
                    "each file says why");
    }
    {
        nfsu::FakeFileSystem files;
        files.add_file("sdmc:/nfsu", {1});
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(!report.root_listed && report.root_problem == "is not a directory",
              "a root that is a file");
    }
    {
        // The listing bound: a directory of 4097 names is refused, not
        // searched, and 4096 are searched.
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        for (std::size_t i = 0; i < nfsu::kAssetMaxDirectoryNames - 2; ++i) {
            files.add_file("sdmc:/nfsu/Tracks/extra" + std::to_string(i), {});
        }
        check(run_check(files, "sdmc:/nfsu", s.manifest).verdict ==
                  nfsu::AssetVerdict::complete,
              "4096 names are searched");
        files.add_file("sdmc:/nfsu/Tracks/one-more", {});
        const nfsu::AssetCheckReport report = run_check(files, "sdmc:/nfsu", s.manifest);
        check(report.files[2].status == nfsu::AssetStatus::unreadable &&
                  report.files[2].detail == "Tracks cannot be listed: holds more than 4096 names",
              "4097 names are refused: " + report.files[2].detail);
    }
}

void test_verdict_order() {
    const Synthetic s = synthetic();
    const auto verdict_with = [&](bool missing, bool size, bool hash, bool unreadable) {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        if (missing) {
            files = nfsu::FakeFileSystem();
            files.add_directory("sdmc:/nfsu/Cars/240SX");
            for (std::size_t i = 1; i < 4; ++i) {
                files.add_file("sdmc:/nfsu/" + s.manifest.files[i].path, s.contents[i]);
            }
        }
        if (size) {
            files.add_file("sdmc:/nfsu/Tracks/TRACKB1003.lzc", {1, 2});
        }
        if (hash) {
            std::vector<unsigned char> changed = s.contents[1];
            changed[0] ^= 0x80U;
            files.add_file("sdmc:/nfsu/Global/GLOBALB.BUN", changed);
        }
        if (unreadable) {
            files.add_file("sdmc:/nfsu/Tracks/STREAML1RA_1003.bun", {});
        }
        return run_check(files, "sdmc:/nfsu", s.manifest).verdict;
    };
    check(verdict_with(true, true, true, true) == nfsu::AssetVerdict::missing_files,
          "missing first");
    check(verdict_with(false, true, true, true) == nfsu::AssetVerdict::partial,
          "then wrong size");
    check(verdict_with(false, false, true, true) == nfsu::AssetVerdict::wrong_version,
          "then wrong version");
    check(verdict_with(false, false, false, true) == nfsu::AssetVerdict::unreadable,
          "then unreadable");
    check(verdict_with(false, false, false, false) == nfsu::AssetVerdict::complete, "complete");
    check(std::string(nfsu::asset_verdict_name(nfsu::AssetVerdict::missing_files)) ==
                  "missing files" &&
              std::string(nfsu::asset_verdict_name(nfsu::AssetVerdict::wrong_version)) ==
                  "wrong version" &&
              std::string(nfsu::asset_status_name(nfsu::AssetStatus::wrong_hash)) == "wrong-hash",
          "the names");
}

// ---------------------------------------------------------------------------
// The config file
// ---------------------------------------------------------------------------

void test_config_parse() {
    struct Good {
        const char* text;
        const char* root;
    };
    const Good goods[] = {
        {"root = sdmc:/nfsu\n", "sdmc:/nfsu"},
        {"root = sdmc:/nfsu", "sdmc:/nfsu"},
        {"# where the game files are\r\n\r\n  root=sdmc:/games/nfsu/  \r\n", "sdmc:/games/nfsu"},
        {"\troot\t=\tsdmc:/NFSU U\t\n# trailing comment\n", "sdmc:/NFSU U"},
        {"root = sdmc:/", "sdmc:/"},
    };
    for (const Good& good : goods) {
        std::string root = "unset";
        std::string reason;
        check(nfsu::parse_asset_config(good.text, root, reason) && root == good.root,
              std::string("parses: ") + good.text + " -> " + root + " " + reason);
    }
    const std::string long_root = "root = sdmc:/" + std::string(195, 'a');
    struct Bad {
        std::string text;
        const char* needle;
    };
    const Bad bads[] = {
        {"\xEF\xBB\xBFroot = sdmc:/nfsu\n", "starts with a UTF-8 byte order mark"},
        {"# ok\nroot = sdmc:/\xD0\xA0nfsu\n",
         "line 2 column 14 holds the byte 0xD0, which is not printable ASCII"},
        {std::string("root = sdmc:/n\0fsu\n", 19), "line 1 column 15 holds the byte 0x00"},
        {"root = sdmc:/nfsu\x7F\n", "holds the byte 0x7F"},
        {"root sdmc:/nfsu\n", "line 1 is neither a comment nor a setting: \"root sdmc:/nfsu\""},
        {"path = sdmc:/nfsu\n", "line 1 sets \"path\", which is not a setting"},
        {"Root = sdmc:/nfsu\n", "sets \"Root\""},
        {"root = sdmc:/a\n\nroot = sdmc:/b\n", "line 3 sets root again; line 1 already set it"},
        {"root = nfsu\n", "line 1: the root must start with sdmc:/"},
        {"root = /nfsu\n", "must start with sdmc:/"},
        {"root = SDMC:/nfsu\n", "must start with sdmc:/"},
        {"root = sdmc:/games\\nfsu\n", "holds a backslash"},
        {"root = sdmc:/games/../nfsu\n", "holds the folder name .."},
        {"root = sdmc:/./nfsu\n", "holds the folder name ."},
        {"root = sdmc://nfsu\n", "holds an empty folder name"},
        {"root = sdmc:/nf?su\n", "holds the character ?"},
        {"root = sdmc:/a:b\n", "holds the character :"},
        {"root =\n", "line 1: the root is empty"},
        {long_root, "is 201 bytes long; at most 200"},
        {"# only a comment\n", "sets no root"},
        {"", "sets no root"},
        {"root = sdmc:/n\rfsu\n", "line 1 holds a carriage return that does not end it"},
        // Review comment 3761, N1: a tab passes the byte check, and a FAT
        // name cannot hold one.
        {"root = sdmc:/a\tb\n",
         "line 1: the root holds a tab, which a folder name on the SD card cannot"},
        {"root = sdmc:/games/\tnfsu\n", "the root holds a tab"},
    };
    for (const Bad& bad : bads) {
        std::string root = "unchanged";
        std::string reason;
        const bool parsed = nfsu::parse_asset_config(bad.text, root, reason);
        check(!parsed && root == "unchanged" && contains(reason, bad.needle),
              "refused: " + bad.text + " - " + reason);
        bool ascii = true;
        for (const char c : reason) {
            ascii = ascii && static_cast<unsigned char>(c) >= 0x20U &&
                    static_cast<unsigned char>(c) <= 0x7EU;
        }
        check(ascii, "the reason is printable ASCII: " + reason);
    }
    std::string root;
    std::string reason;
    check(nfsu::parse_asset_config("root = sdmc:/" + std::string(194, 'a'), root, reason),
          "a root of exactly 200 bytes is allowed");
}

std::vector<unsigned char> bytes_of(const std::string& text) {
    return std::vector<unsigned char>(text.begin(), text.end());
}

void test_config_load() {
    const std::string file = "sdmc:/config/nfsu_switch/assets.cfg";
    {
        nfsu::FakeFileSystem files;
        files.add_directory("sdmc:/nfsu");
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::absent && config.root == "sdmc:/nfsu" &&
                  config.file == file && config.detail == "no config in the root",
              "no config directory: the default root, " + config.detail);
    }
    {
        nfsu::FakeFileSystem files;
        files.add_file(file, bytes_of("root = sdmc:/games/nfsu\r\n"));
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::applied &&
                  config.root == "sdmc:/games/nfsu",
              "a config file moves the root");
        check(files.reads().size() == 1 && files.reads()[0].count == 26 &&
                  files.reads()[0].got == 25,
              "read once, one byte past its size");
    }
    {
        nfsu::FakeFileSystem files;
        files.add_file("sdmc:/CONFIG/NFSU_Switch/Assets.CFG", bytes_of("root = sdmc:/x\n"));
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::applied && config.root == "sdmc:/x",
              "the config file is found under the naming rule too");
    }
    {
        nfsu::FakeFileSystem files;
        files.add_file(file, bytes_of("root = sdmc:/\xD0\xA0\n"));
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused && config.root.empty() &&
                  contains(config.detail, "line 1 column 14 holds the byte 0xD0"),
              "a non-ASCII config file is refused: " + config.detail);
    }
    {
        nfsu::FakeFileSystem files;
        std::string text = "root = sdmc:/nfsu\n#";
        text += std::string(nfsu::kAssetConfigMaxBytes - text.size(), 'x');
        files.add_file(file, bytes_of(text));
        check(nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu")
                      .state == nfsu::AssetConfigState::applied,
              "a config file of exactly 4096 bytes is read");
        text += "x";
        files.add_file(file, bytes_of(text));
        const std::size_t reads_before = files.reads().size();
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused &&
                  config.detail == "is 4097 bytes; a config file holds at most 4096" &&
                  files.reads().size() == reads_before,
              "a larger one is refused unread: " + config.detail);
    }
    {
        nfsu::FakeFileSystem files;
        files.add_directory(file);
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused &&
                  config.detail == "cannot be read: it is a directory, not a file",
              "a directory is refused: " + config.detail);
    }
    {
        nfsu::FakeFileSystem files;
        files.add_file(file, bytes_of("root = sdmc:/nfsu\n"));
        files.fail_read(file, 0, "cannot be opened for reading");
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused &&
                  config.detail == "cannot be read: it cannot be opened for reading",
              "an unreadable file is refused: " + config.detail);
    }
    {
        nfsu::FakeFileSystem files;
        files.add_file(file, bytes_of("root = sdmc:/nfsu\n"));
        files.report_size(file, 40);
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused &&
                  config.detail == "changed while it was read: 18 bytes where it was sized at 40",
              "a file that changed is refused: " + config.detail);
    }
    {
        // Review comment 3761, N3 (M20): sized at 10 and holding 18, so the
        // bounded read of 11 bytes gets all 11.
        nfsu::FakeFileSystem files;
        files.add_file(file, bytes_of("root = sdmc:/nfsu\n"));
        files.report_size(file, 10);
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused &&
                  config.detail == "changed while it was read: 11 bytes where it was sized at 10",
              "a file that grew is refused: " + config.detail);
    }
    {
        nfsu::FakeFileSystem files;
        files.add_file(file, bytes_of("root = sdmc:/nfsu\n"));
        files.add_file("sdmc:/config/nfsu_switch/ASSETS.CFG", bytes_of("root = sdmc:/x\n"));
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused &&
                  contains(config.detail, "cannot be read: config/nfsu_switch holds 2 names"),
              "two spellings of the config file are refused: " + config.detail);
    }
    // Review comment 3761, F1: the card's top level cannot be listed - more
    // names than the bound, or a listing error - so whether a config file is
    // there cannot be told. It is refused, not taken as absent, and nothing
    // is checked: not the root it names, and not the default.
    for (int variant = 0; variant < 2; ++variant) {
        nfsu::FakeFileSystem files;
        files.add_file(file, bytes_of("root = sdmc:/games\n"));
        files.add_directory("sdmc:/games");
        files.add_directory("sdmc:/nfsu");
        std::string why;
        if (variant == 0) {
            for (std::size_t i = 0; i < nfsu::kAssetMaxDirectoryNames + 4 - 3; ++i) {
                files.add_directory("sdmc:/d" + std::to_string(i));
            }
            why = "sdmc:/ holds more than 4096 names";
        } else {
            files.fail_listing("sdmc:", "cannot be listed (errno 5)");
            why = "sdmc:/ cannot be listed (errno 5)";
        }
        const std::string label = variant == 0 ? "over the bound" : "a listing error";
        const nfsu::AssetConfig config =
            nfsu::load_asset_config(files, "sdmc:/", nfsu::kConsoleAssetConfig, "sdmc:/nfsu");
        check(config.state == nfsu::AssetConfigState::refused && config.root.empty() &&
                  config.detail == "cannot be looked for: " + why,
              "a card whose top level will not list refuses the config, " + label + ": " +
                  config.detail);
        const nfsu::ConsoleAssetCheck run =
            nfsu::run_console_asset_check(files, nfsu::ConsoleAssetSetup{}, synthetic().manifest);
        check(!run.checked && files.listed() == std::vector<std::string>{"sdmc:", "sdmc:"},
              "and checks nothing, " + label);
        check(run.lines.size() >= 2 &&
                  run.lines[0].text == "config: sdmc:/config/nfsu_switch/assets.cfg REFUSED: "
                                       "cannot be looked for: " +
                                           why &&
                  run.lines[1].text ==
                      "Whether there is a config file cannot be told, so nothing was checked.",
              "and says so on screen, " + label + ": " +
                  (run.lines.empty() ? std::string() : run.lines[0].text));
    }
}

// ---------------------------------------------------------------------------
// The console's whole run
// ---------------------------------------------------------------------------

void test_console_run() {
    const Synthetic s = synthetic();
    const nfsu::ConsoleAssetSetup setup;
    check(setup.sd_root == "sdmc:/" && setup.config == "config/nfsu_switch/assets.cfg" &&
              setup.default_root == "sdmc:/nfsu",
          "the console's defaults");
    {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        const nfsu::ConsoleAssetCheck run = nfsu::run_console_asset_check(files, setup, s.manifest);
        check(run.checked && run.report.verdict == nfsu::AssetVerdict::complete,
              "the default root, complete");
        check_equal(run.lines.at(0).text,
                    "config: sdmc:/config/nfsu_switch/assets.cfg not found; root sdmc:/nfsu (the "
                    "default)",
                    "the config line");
        check(run.lines.size() == 9 && run.lines[2].text == "install: COMPLETE - all 4 files "
                                                            "match by name, size and SHA-256",
              "then the report, verdict first");
    }
    {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/games/nfsu", s);
        files.add_file("sdmc:/config/nfsu_switch/assets.cfg",
                       bytes_of("# moved\nroot = sdmc:/games/nfsu/\n"));
        const nfsu::ConsoleAssetCheck run = nfsu::run_console_asset_check(files, setup, s.manifest);
        check(run.checked && run.report.verdict == nfsu::AssetVerdict::complete &&
                  run.lines.at(0).text ==
                      "config: sdmc:/config/nfsu_switch/assets.cfg sets root sdmc:/games/nfsu",
              "a moved root, complete");
    }
    {
        // A config naming a folder that is not there: applied, and the check
        // then says the root cannot be listed.
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        files.add_file("sdmc:/config/nfsu_switch/assets.cfg", bytes_of("root = sdmc:/nfsux\n"));
        const nfsu::ConsoleAssetCheck run = nfsu::run_console_asset_check(files, setup, s.manifest);
        check(run.checked && !run.report.root_listed &&
                  run.report.verdict == nfsu::AssetVerdict::missing_files,
              "a root that is not there");
        check_equal(run.lines.at(2).text, "root sdmc:/nfsux cannot be listed: it does not exist",
                    "is named");
    }
    {
        nfsu::FakeFileSystem files;
        install(files, "sdmc:/nfsu", s);
        files.add_file("sdmc:/config/nfsu_switch/assets.cfg", bytes_of("root = C:\\nfsu\n"));
        const nfsu::ConsoleAssetCheck run = nfsu::run_console_asset_check(files, setup, s.manifest);
        check(!run.checked, "a refused config checks nothing");
        for (const std::string& listed : files.listed()) {
            check(listed.compare(0, 10, "sdmc:/nfsu") != 0,
                  "and lists nothing under the default root: " + listed);
        }
        check_lines(texts(run.lines),
                    {
                        "config: sdmc:/config/nfsu_switch/assets.cfg REFUSED: line 1: the root "
                        "must start with sdmc:/ - an absolute path on the SD card - and reads "
                        "\"C:\\x5Cnfsu\"",
                        "Fix the file, or delete it to use sdmc:/nfsu. Nothing was checked.",
                        "set synthetic needs these 4 files under the root:",
                        "  Cars/240SX/GEOMETRY.BIN  3000 bytes",
                        "  Global/GLOBALB.BUN  2621447 bytes",
                        "  Tracks/TRACKB1003.lzc  100 bytes",
                        "  Tracks/STREAML1RA_1003.BUN  0 bytes",
                    },
                    "the refusal");
        check(run.lines[0].tone == nfsu::AssetTone::bad, "in the bad tone");
    }
    {
        // The retail first-lap set on an SD card holding nothing yet: what
        // the owner sees before copying anything.
        nfsu::AssetManifest first;
        nfsu::retail_asset_manifest("first-lap", first);
        nfsu::FakeFileSystem files;
        files.add_directory("sdmc:/nfsu");
        const nfsu::ConsoleAssetCheck run = nfsu::run_console_asset_check(files, setup, first);
        check_lines(
            texts(run.lines),
            {
                "config: sdmc:/config/nfsu_switch/assets.cfg not found; root sdmc:/nfsu (the "
                "default)",
                "set first-lap: one timed lap of event 1 in the stock 240SX (issue #159), 14 "
                "files, 114102502 bytes",
                "install: MISSING FILES - 0 of 14 files ok: 14 missing, 0 wrong size, 0 wrong "
                "version, 0 unreadable",
                "copy these 14 files from the game's installation folder to the same paths "
                "under sdmc:/nfsu:",
                "  Cars/240SX/GEOMETRY.BIN  4780816 bytes",
                "  Cars/240SX/TEXTURES.BIN  697340 bytes",
                "  Cars/BRAKES/GEOMETRY.BIN  69408 bytes",
                "  Cars/PLATES/GEOMETRY.BIN  1184 bytes",
                "  Cars/TEXTURES.BIN  1230808 bytes",
                "  FrontEnd/FrontB.lzc  4182578 bytes",
                "  Global/DYNTEX.BIN  4677572 bytes",
                "  Global/GLOBALB.BUN  2361176 bytes",
                "  Global/INGAMEB.BUN  1670080 bytes",
                "  Languages/LanguageTextures.bin  517284 bytes",
                "  Tracks/STREAML1RA.BUN  92394688 bytes",
                "  Tracks/STREAML1RA_1003.BUN  0 bytes",
                "  Tracks/TEX1003TRACK.BIN  1086216 bytes",
                "  Tracks/TRACKB1003.lzc  433352 bytes",
                "MISSING       Cars/240SX/GEOMETRY.BIN  no Cars in the root",
                "MISSING       Cars/240SX/TEXTURES.BIN  no Cars in the root",
                "MISSING       Cars/BRAKES/GEOMETRY.BIN  no Cars in the root",
                "MISSING       Cars/PLATES/GEOMETRY.BIN  no Cars in the root",
                "MISSING       Cars/TEXTURES.BIN  no Cars in the root",
                "MISSING       FrontEnd/FrontB.lzc  no FrontEnd in the root",
                "MISSING       Global/DYNTEX.BIN  no Global in the root",
                "MISSING       Global/GLOBALB.BUN  no Global in the root",
                "MISSING       Global/INGAMEB.BUN  no Global in the root",
                "MISSING       Languages/LanguageTextures.bin  no Languages in the root",
                "MISSING       Tracks/STREAML1RA.BUN  no Tracks in the root",
                "MISSING       Tracks/STREAML1RA_1003.BUN  no Tracks in the root",
                "MISSING       Tracks/TEX1003TRACK.BIN  no Tracks in the root",
                "MISSING       Tracks/TRACKB1003.lzc  no Tracks in the root",
                "read 0 bytes in 0 reads and 1 directory listings",
            },
            "the first-lap report on an empty card");
    }
}

// Review comment 3761, N6: the console form of the first-lap report for a
// card on which every file is wrong the same way. Every line fits the
// screen's 106 columns, so nothing wraps, and the whole report with the
// application's title, config and status lines fits its 40 rows.
void test_console_fits() {
    nfsu::AssetManifest first;
    nfsu::retail_asset_manifest("first-lap", first);
    for (const nfsu::AssetStatus status :
         {nfsu::AssetStatus::ok, nfsu::AssetStatus::missing, nfsu::AssetStatus::wrong_size,
          nfsu::AssetStatus::wrong_hash}) {
        for (const bool shorter : {true, false}) {
            nfsu::AssetCheckReport report;
            report.manifest = first.name;
            report.title = first.title;
            report.root_listed = true;
            for (const nfsu::AssetFile& expected : first.files) {
                nfsu::AssetFileReport file;
                file.expected = expected;
                file.status = status;
                file.found = status != nfsu::AssetStatus::missing;
                file.found_as = expected.path;
                file.sized = true;
                file.size = expected.size;
                file.sha256 = expected.sha256;
                if (status == nfsu::AssetStatus::missing) {
                    file.detail = "no " + expected.path.substr(expected.path.rfind('/') + 1) +
                                  " in " + expected.path.substr(0, expected.path.rfind('/'));
                } else if (status == nfsu::AssetStatus::wrong_size) {
                    // The widest sizes a file of this set can be reported at.
                    file.size = shorter && expected.size > 0 ? 10000000 : 99999999;
                    if (shorter && expected.size <= 10000000) {
                        file.size = expected.size > 0 ? expected.size - 1 : 1;
                    }
                    file.detail = file.size < expected.size
                                      ? "shorter than the retail file: an interrupted copy?"
                                      : "longer than the retail file";
                } else if (status == nfsu::AssetStatus::wrong_hash) {
                    file.sha256 = std::string(64, 'F');
                }
                ++report.counts[static_cast<std::size_t>(status)];
                report.files.push_back(file);
            }
            report.verdict = status == nfsu::AssetStatus::ok ? nfsu::AssetVerdict::complete
                           : status == nfsu::AssetStatus::missing
                               ? nfsu::AssetVerdict::missing_files
                           : status == nfsu::AssetStatus::wrong_size
                               ? nfsu::AssetVerdict::partial
                               : nfsu::AssetVerdict::wrong_version;
            report.bytes_read = 114102502;
            report.reads = 123;
            report.listings = 8;
            const std::vector<nfsu::AssetLine> lines =
                nfsu::asset_report_lines(report, "sdmc:/nfsu", nfsu::AssetReportForm::console);
            std::size_t widest = 0;
            for (const nfsu::AssetLine& line : lines) {
                widest = std::max(widest, line.text.size());
            }
            const std::string label = std::string(nfsu::asset_status_name(status)) +
                                      (shorter ? ", shorter" : ", longer");
            check(widest <= 106, "every console line fits 106 columns, " + label + ": " +
                                     std::to_string(widest));
            // Title, config, the report, and the status line.
            check(2 + lines.size() + 1 <= 40,
                  "the whole screen fits 40 rows, " + label + ": " +
                      std::to_string(2 + lines.size() + 1));
        }
    }
}

// ---------------------------------------------------------------------------
// Real directories, through the desktop service and the console's
// ---------------------------------------------------------------------------

bool write_file(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!bytes.empty()) {
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(stream);
}

// The parts of a report that must not depend on the service.
std::string summary_of(const nfsu::AssetCheckReport& report) {
    std::string text = std::string(nfsu::asset_verdict_name(report.verdict)) + " root " +
                       (report.root_listed ? "listed" : report.root_problem) + " bytes " +
                       std::to_string(report.bytes_read) + " reads " +
                       std::to_string(report.reads) + " listings " +
                       std::to_string(report.listings);
    for (const nfsu::AssetFileReport& file : report.files) {
        text += " | " + std::string(nfsu::asset_status_name(file.status)) + " " + file.found_as +
                " " + file.detail;
    }
    return text;
}

void test_disk() {
    const fs::path work = "nfsu_asset_tests_disk";
    std::error_code error;
    fs::remove_all(work, error);
    const Synthetic s = synthetic();

    struct Scenario {
        const char* name;
        std::vector<std::string> spell;
        int change;
        const char* want;
    };
    // change: 0 none, 1 drop file 2, 2 truncate file 2, 3 flip a byte of
    // file 1, 4 a directory where file 2 should be.
    const Scenario scenarios[] = {
        {"complete", {}, 0, "complete"},
        {"cased",
         {"cars/240sx/Geometry.bin", "GLOBAL/GLOBALB.bun", "tracks/trackb1003.LZC",
          "tracks/streaml1ra_1003.BUN"},
         0,
         "complete"},
        {"missing", {}, 1, "missing files"},
        {"truncated", {}, 2, "partial"},
        {"flipped", {}, 3, "wrong version"},
        {"directory", {}, 4, "unreadable"},
    };
    for (const Scenario& scenario : scenarios) {
        const fs::path root = work / scenario.name;
        for (std::size_t i = 0; i < 4; ++i) {
            const std::string path = i < scenario.spell.size() ? scenario.spell[i]
                                                               : s.manifest.files[i].path;
            std::vector<unsigned char> bytes = s.contents[i];
            if (i == 2 && scenario.change == 1) {
                continue;
            }
            if (i == 2 && scenario.change == 2) {
                bytes.resize(50);
            }
            if (i == 1 && scenario.change == 3) {
                bytes[2U * nfsu::kAssetReadChunkBytes + 3U] ^= 0x10U;
            }
            if (i == 2 && scenario.change == 4) {
                fs::create_directories(root / path, error);
                continue;
            }
            check(write_file(root / path, bytes), std::string("wrote ") + path);
        }
        const nfsu::AssetCheckReport desktop =
            run_check(nfsu::desktop_file_system(), root.generic_string(), s.manifest);
        const nfsu::AssetCheckReport console =
            run_check(nfsu::posix_file_system(), root.generic_string(), s.manifest);
        check_equal(nfsu::asset_verdict_name(desktop.verdict), scenario.want,
                    std::string("desktop service on disk: ") + scenario.name);
        check_equal(summary_of(console), summary_of(desktop),
                    std::string("the console service agrees on disk: ") + scenario.name);
        // Through the desktop service the root may also be absolute, under a
        // working directory whose name need not be ASCII.
        nfsu::AssetCheckReport wide;
        std::string reason;
        nfsu::check_asset_install(nfsu::desktop_file_system(), fs::absolute(root, error),
                                  s.manifest, wide, reason);
        check_equal(summary_of(wide), summary_of(desktop),
                    std::string("an absolute root reads the same: ") + scenario.name);
    }
    const nfsu::AssetCheckReport cased =
        run_check(nfsu::posix_file_system(), (work / "cased").generic_string(), s.manifest);
    check(cased.files[2].found_as == "tracks/trackb1003.LZC",
          "found as written on disk: " + cased.files[2].found_as);

    // The root itself.
    for (nfsu::FileSystemService* files :
         {&nfsu::desktop_file_system(), &nfsu::posix_file_system()}) {
        const nfsu::AssetCheckReport absent =
            run_check(*files, (work / "absent").generic_string(), s.manifest);
        check(!absent.root_listed && absent.root_problem == "does not exist",
              "an absent root on disk: " + absent.root_problem);
        const nfsu::AssetCheckReport file = run_check(
            *files, (work / "complete" / "Global" / "GLOBALB.BUN").generic_string(), s.manifest);
        check(!file.root_listed && file.root_problem == "is not a directory",
              "a root that is a file on disk: " + file.root_problem);

        // The three calls directly.
        std::vector<std::string> names;
        std::string reason;
        const fs::path tracks = work / "complete" / "Tracks";
        check(files->list_directory(tracks, 2, names, reason) && names.size() == 2,
              "two names listed within a bound of two: " + reason);
        check(!files->list_directory(tracks, 1, names, reason) && names.empty() &&
                  reason == "holds more than 1 names",
              "and refused within a bound of one: " + reason);
        std::uint64_t size = 7;
        check(!files->file_size(work / "complete" / "nothing", size, reason) &&
                  reason == "does not exist" && size == 7,
              "sizing nothing: " + reason);
        check(!files->file_size(tracks, size, reason) && reason == "is a directory, not a file",
              "sizing a directory: " + reason);
        check(files->file_size(tracks / "TRACKB1003.lzc", size, reason) && size == 100,
              "sizing a file");
        unsigned char buffer[16] = {};
        std::size_t got = 99;
        check(files->read_file(tracks / "TRACKB1003.lzc", 96, buffer, 16, got, reason) &&
                  got == 4 && buffer[0] == s.contents[2][96],
              "a read across the end stops at it");
        got = 99;
        check(files->read_file(tracks / "TRACKB1003.lzc", 100, buffer, 1, got, reason) && got == 0,
              "a read at the end gets nothing");
        got = 99;
        check(files->read_file(tracks / "TRACKB1003.lzc", 5000, buffer, 1, got, reason) &&
                  got == 0,
              "a read past the end gets nothing");
        got = 99;
        check(files->read_file(tracks / "TRACKB1003.lzc", 0, nullptr, 0, got, reason) && got == 0,
              "a read of nothing succeeds");
        got = 99;
        check(!files->read_file(work / "complete" / "nothing", 0, buffer, 1, got, reason) &&
                  got == 0 && reason == "cannot be opened for reading",
              "a read of nothing there fails: " + reason);
        fs::path argument;
        check(!files->argument_to_path(nullptr, argument, reason), "a null argument is refused");
    }
    fs::remove_all(work, error);
}

} // namespace

int main() {
    test_retail_sets();
    test_naming_rule();
    test_validation();
    test_complete();
    test_missing();
    test_wrong_size();
    test_wrong_hash();
    test_case_insensitive();
    test_ambiguous();
    test_unreadable();
    test_root();
    test_verdict_order();
    test_config_parse();
    test_config_load();
    test_console_run();
    test_console_fits();
    test_disk();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "asset tests passed\n";
    return 0;
}
