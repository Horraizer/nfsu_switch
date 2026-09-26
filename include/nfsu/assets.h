#pragma once

#include "nfsu/platform.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The retail files a feature needs, and a read-only check that a directory
// holds exactly those files (issue #47). docs/SWITCH_ASSETS.md.
//
// A manifest names each file by its path under the installation root, its
// size and its SHA-256, all taken from the retail installation. The check
// finds each file through a FileSystemService, matching every name without
// regard to ASCII case, sizes it, and hashes it only when the size is the
// expected one, reading it in chunks of at most kAssetReadChunkBytes. It
// classifies every file and then the install as a whole, and it writes
// nothing and reads nothing a manifest does not name. The same code runs on
// the desktop, as `nfsu_probe asset-check`, and on the console, as
// nfsu_switch_assets; only the FileSystemService differs.
//
// No retail data is here: a manifest holds names, sizes and hashes only.
namespace nfsu {

// ---------------------------------------------------------------------------
// Manifests
// ---------------------------------------------------------------------------

struct AssetFile {
    // Relative to the root, '/'-separated, in the retail installation's own
    // spelling, which is what the owner sees in the game's folder:
    // "Tracks/TRACKB1003.lzc", although the game formats "TRACKB%d.LZC".
    std::string path;
    std::uint64_t size = 0;
    // 64 upper-case hexadecimal digits.
    std::string sha256;
    // What in this project reads the file, in a few words.
    std::string purpose;
};

struct AssetManifest {
    // The set's name on the command line: "first-lap".
    std::string name;
    // One line saying what the set is for.
    std::string title;
    std::vector<AssetFile> files;
};

// The bounds. A manifest holds at most kAssetMaxManifestFiles files, each
// named by at most kAssetMaxPathBytes bytes and at most kAssetMaxFileBytes
// long - the limit every parser these files go through applies to its
// input. A directory holding more than kAssetMaxDirectoryNames names is
// refused rather than searched, and a file is read at most
// kAssetReadChunkBytes at a time, into one buffer the check allocates once.
constexpr std::size_t kAssetMaxManifestFiles = 64;
constexpr std::size_t kAssetMaxPathBytes = 240;
constexpr std::uint64_t kAssetMaxFileBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kAssetMaxDirectoryNames = 4096;
constexpr std::size_t kAssetReadChunkBytes = 1024U * 1024U;

// The retail sets, in the order a listing shows them. "first-lap", the
// default, is the other two together: "stock-car", the six files the stock
// 240SX is drawn from, and "event-1", the seven files race 1 of Underground
// mode - event 1, a circuit on track 1003 in stream region L1RA - loads.
std::vector<std::string> retail_asset_manifest_names();

// The retail set of that name. False, with `manifest` untouched, for a name
// that is not one.
bool retail_asset_manifest(const std::string& name, AssetManifest& manifest);

// The sum of the files' sizes.
std::uint64_t asset_manifest_bytes(const AssetManifest& manifest) noexcept;

// Whether a manifest is one the check can run: a name, 1 to
// kAssetMaxManifestFiles files, each path relative, printable ASCII, with
// '/' separators, no empty, "." or ".." part, no backslash or colon, and
// at most kAssetMaxPathBytes bytes; each size at most kAssetMaxFileBytes;
// each hash 64 upper-case hexadecimal digits; and no two paths that name
// the same file under the naming rule below. False with a sentence naming
// the first file that breaks one.
bool validate_asset_manifest(const AssetManifest& manifest, std::string& reason);

// The naming rule: two names match when they are the same length and equal
// byte for byte once 'A' to 'Z' are folded to 'a' to 'z'. No other byte is
// folded, so a name holding anything outside ASCII matches only itself.
// This is the rule the game gets from Windows for the names it formats in
// upper case ("TRACKB%d.LZC") and finds on disk in whatever case the
// installer wrote ("TRACKB1003.lzc"), and it is applied here on every
// platform, so a case-sensitive desktop and the console's SD card find the
// same files.
bool asset_names_match(const std::string& a, const std::string& b) noexcept;

// ---------------------------------------------------------------------------
// Finding one file
// ---------------------------------------------------------------------------

enum class AssetLookup : std::uint8_t {
    found,
    // A name along the path is in no listing: the detail names which.
    missing,
    // Two names in one directory both match a name along the path. Only a
    // case-sensitive file system can hold such a pair.
    ambiguous,
    // A directory along the path, other than the root, could not be listed.
    unreadable
};

struct AssetResolution {
    AssetLookup lookup = AssetLookup::missing;
    // The root joined with the names found. Meaningful when found.
    std::filesystem::path path;
    // The names found, '/'-separated, in the case the file system spells
    // them. When found, the whole relative path; otherwise as far as it got.
    std::string found_as;
    // When not found, one printable ASCII sentence saying why.
    std::string detail;
    // False when the root itself could not be listed; `detail` then holds
    // the service's reason and the lookup is missing.
    bool root_listed = true;
};

// Walks `relative` from `root` one name at a time under the naming rule,
// listing each directory once through `files`. Reads no file.
AssetResolution resolve_asset_path(
    FileSystemService& files, const std::filesystem::path& root, const std::string& relative);

// ---------------------------------------------------------------------------
// The check
// ---------------------------------------------------------------------------

enum class AssetStatus : std::uint8_t {
    ok,
    missing,
    // Present with another size. It is not read.
    wrong_size,
    // The right size and another SHA-256.
    wrong_hash,
    // Present and not sized or read to the end, or not a regular file, or
    // changed while it was read.
    unreadable,
    // Two names match one name of its path (AssetLookup::ambiguous).
    ambiguous
};

constexpr std::size_t kAssetStatusCount = 6;

// "ok", "missing", "wrong-size", "wrong-hash", "unreadable", "ambiguous".
const char* asset_status_name(AssetStatus status) noexcept;

struct AssetFileReport {
    AssetFile expected;
    AssetStatus status = AssetStatus::missing;
    // True when the whole path was found; found_as is then where.
    bool found = false;
    // As AssetResolution::found_as: the whole path when found, otherwise
    // the directories reached before the lookup stopped.
    std::string found_as;
    bool sized = false;
    std::uint64_t size = 0;
    bool hashed = false;
    // Upper-case hexadecimal, when hashed.
    std::string sha256;
    std::uint64_t bytes_read = 0;
    // Why it is not ok, as one printable ASCII sentence; empty when ok.
    std::string detail;
};

// The install as a whole: the first of these that applies, in this order.
enum class AssetVerdict : std::uint8_t {
    // Every file ok.
    complete,
    // At least one file missing.
    missing_files,
    // None missing, at least one of the wrong size: an interrupted copy.
    partial,
    // None missing or of the wrong size, at least one of another SHA-256.
    wrong_version,
    // Every file present and the right size where it could be sized, and
    // at least one unreadable or ambiguous.
    unreadable
};

// "complete", "missing files", "partial", "wrong version", "unreadable".
const char* asset_verdict_name(AssetVerdict verdict) noexcept;

struct AssetCheckReport {
    std::string manifest;
    std::string title;
    // False when the root could not be listed; every file is then missing
    // and root_problem says why.
    bool root_listed = false;
    std::string root_problem;
    std::vector<AssetFileReport> files;
    // Files per AssetStatus, indexed by its value.
    std::array<std::size_t, kAssetStatusCount> counts{};
    AssetVerdict verdict = AssetVerdict::missing_files;
    // Everything the check read: bytes, read calls and directory listings.
    std::uint64_t bytes_read = 0;
    std::size_t reads = 0;
    std::size_t listings = 0;

    std::size_t count(AssetStatus status) const noexcept;
};

// Told about the check's progress, so that a console can keep its screen
// alive while a large file is hashed. Called on the checking thread.
class AssetCheckObserver {
public:
    virtual ~AssetCheckObserver() = default;
    // Before file `index` of `total` is looked for.
    virtual void file_started(std::size_t index, std::size_t total, const AssetFile& file) = 0;
    // After each chunk of file `index` is hashed: `hashed` of `size` bytes.
    virtual void file_progress(std::size_t index, std::uint64_t hashed, std::uint64_t size) = 0;
};

// Checks every file of `manifest` under `root`, in manifest order, through
// `files`. For each file it resolves the path, sizes it, and only when the
// size is right reads it from its first byte to its last in chunks of at
// most kAssetReadChunkBytes, then asks for one byte more to prove the file
// ended where it was sized. So the most any file costs is its expected size
// plus one byte, and a file of the wrong size costs nothing. Returns false,
// with `report` untouched and `reason` set, only for a manifest
// validate_asset_manifest refuses; everything the install does wrong is in
// the report. `observer` may be null.
bool check_asset_install(
    FileSystemService& files,
    const std::filesystem::path& root,
    const AssetManifest& manifest,
    AssetCheckReport& report,
    std::string& reason,
    AssetCheckObserver* observer = nullptr);

// ---------------------------------------------------------------------------
// The report, as lines
// ---------------------------------------------------------------------------

enum class AssetTone : std::uint8_t {
    plain,
    good,
    bad,
    note
};

struct AssetLine {
    std::string text;
    AssetTone tone = AssetTone::plain;
};

enum class AssetReportForm : std::uint8_t {
    // What nfsu_probe asset-check prints: the set, the root if it could not
    // be listed, one line per file with every hash in full, the verdict,
    // each file to copy as "<path> -> <root>/<path>", and what was read.
    desktop,
    // What nfsu_switch_assets shows, summary first so that the verdict and
    // the files to copy are never what a full screen cuts off: the set, the
    // root if it could not be listed, the verdict, the files to copy with
    // their sizes under one line naming the root, one line per file, and
    // what was read. A matching hash is "matches", a differing one shows
    // its first 8 digits, and a size that differs says only which way, so
    // that every file line fits the screen's 106 columns.
    console
};

// The report as lines of `form`. `root_text` is how the root is spelled in
// the lines that name it. Every line is printable ASCII apart from what
// `root_text` itself holds, which is printed as it is given.
std::vector<AssetLine> asset_report_lines(
    const AssetCheckReport& report, const std::string& root_text, AssetReportForm form);

// ---------------------------------------------------------------------------
// The console: its root and the config file that moves it
// ---------------------------------------------------------------------------

// The SD card, the config file under it, and the root used when there is no
// config file: the layout issue #141's application already reads.
constexpr const char* kConsoleSdRoot = "sdmc:/";
constexpr const char* kConsoleAssetConfig = "config/nfsu_switch/assets.cfg";
constexpr const char* kConsoleDefaultAssetRoot = "sdmc:/nfsu";
constexpr std::size_t kAssetConfigMaxBytes = 4096;
constexpr std::size_t kAssetRootMaxBytes = 200;

// Parses the text of a config file (docs/SWITCH_ASSETS.md, "The config
// file"): printable ASCII, tab and line ends only; lines ending in LF or
// CR LF; blank lines and lines whose first other character is '#' ignored;
// exactly one "root = <path>" line, the path absolute on the SD card -
// "sdmc:/" and folder names separated by '/', none empty, "." or "..", and
// none holding a character a FAT name cannot: a tab, '"', '*', ':', '<',
// '>', '?', '|' or a backslash. Sets `root` to the path with
// any trailing '/' removed ("sdmc:/" itself stays as it is). False, with
// `root` untouched and `reason` naming the line and what is wrong with it.
bool parse_asset_config(const std::string& text, std::string& root, std::string& reason);

enum class AssetConfigState : std::uint8_t {
    // There is no config file: the default root is used.
    absent,
    // The config file named a root, which is used.
    applied,
    // There is a config file and it was refused, or whether there is one
    // cannot be told because a folder on its path - the card's top level
    // included - could not be listed: nothing is checked.
    refused
};

struct AssetConfig {
    AssetConfigState state = AssetConfigState::absent;
    // The config file's path, for every message about it.
    std::string file;
    // The root in force: the default, or the config file's. Empty when
    // refused.
    std::string root;
    // Why it was refused, or why it counts as absent.
    std::string detail;
    // When refused, one sentence saying what to do about it.
    std::string advice;
};

// Looks for `relative` under `sd_root` by the naming rule and, when it is
// there, reads it - at most kAssetConfigMaxBytes, in one bounded read - and
// parses it. A config file that is present but cannot be read, is larger
// than the bound or does not parse is refused, not ignored: a root the
// owner asked for and did not get would otherwise be checked in silence.
// For the same reason a folder on its path that cannot be listed - "sdmc:/"
// itself included, whether it fails or holds more than
// kAssetMaxDirectoryNames names - refuses rather than counts as absent:
// the file may be there. Only a lookup that listed every folder it needed
// and found no match is absent.
AssetConfig load_asset_config(
    FileSystemService& files,
    const std::string& sd_root,
    const std::string& relative,
    const std::string& default_root);

struct ConsoleAssetSetup {
    std::string sd_root = kConsoleSdRoot;
    std::string config = kConsoleAssetConfig;
    std::string default_root = kConsoleDefaultAssetRoot;
};

struct ConsoleAssetCheck {
    AssetConfig config;
    // True once the install was checked: the config was not refused and
    // the manifest was valid.
    bool checked = false;
    AssetCheckReport report;
    // What the console shows: the config line, then asset_report_lines in
    // the console form, or, when the config was refused, the refusal, its
    // advice and the files the set needs.
    std::vector<AssetLine> lines;
};

// What nfsu_switch_assets does between drawing its title and waiting for +:
// the config, then the check of `manifest` under the root in force, as
// lines. It is here rather than in switch/ so that the desktop tests run
// it, through a fake SD card, exactly as the console does.
ConsoleAssetCheck run_console_asset_check(
    FileSystemService& files,
    const ConsoleAssetSetup& setup,
    const AssetManifest& manifest,
    AssetCheckObserver* observer = nullptr);

} // namespace nfsu
