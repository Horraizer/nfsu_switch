#include "nfsu/assets.h"

#include "nfsu/sha256.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

// The retail manifests and the check; include/nfsu/assets.h is the
// contract and docs/SWITCH_ASSETS.md the reasons. Standard library, the
// SHA-256 of nfsu_sha256 and the FileSystemService only: nothing here opens
// a file itself.

namespace nfsu {

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// The retail files
// ---------------------------------------------------------------------------

// Sizes and SHA-256 hashes read from the owner's retail installation, each
// file dated 2003 there. Two files of that installation are 2025
// modifications - Global/GlobalB.lzc and Global/GLOBALA.BUN - and neither is
// in any set: the port reads GLOBALB.BUN, the retail image of GLOBALB.LZC,
// and never reads GLOBALA.BUN. docs/SWITCH_ASSETS.md records both.
struct RetailFile {
    const char* path;
    std::uint64_t size;
    const char* sha256;
    const char* purpose;
};

constexpr RetailFile kRetailFiles[] = {
    {"Cars/240SX/GEOMETRY.BIN", 4780816,
     "002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619",
     "the 240SX's meshes"},
    {"Cars/240SX/TEXTURES.BIN", 697340,
     "725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA",
     "the 240SX's textures and its doorline skin layer"},
    {"Cars/BRAKES/GEOMETRY.BIN", 69408,
     "FEC4CBF4F9C3E7FB1CD883F0B874F920248652A2E63EE4A341446563F0B9B1A5",
     "the brake meshes the stock car places"},
    {"Cars/PLATES/GEOMETRY.BIN", 1184,
     "0EBCFD14DD303613F5DA3FF5D7A6DF57BE3CDE915F458BA243FE5D089B64BD8A",
     "the licence plate mesh"},
    {"Cars/TEXTURES.BIN", 1230808,
     "999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58",
     "the shared car textures, with the skin target DUMMY_SKIN1"},
    {"FrontEnd/FrontB.lzc", 4182578,
     "76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90",
     "the career event table: event 1's track, laps and direction"},
    {"Global/DYNTEX.BIN", 4677572,
     "020E99EBEA6A4AEFC7F7C858BA8672A48FA040E00299E2372E914687BF90525C",
     "the in-race tachometer LINES and RED faces"},
    {"Global/GLOBALB.BUN", 2361176,
     "39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9",
     "the car part database, the car and track tables, shared textures"},
    {"Global/INGAMEB.BUN", 1670080,
     "044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299",
     "the in-race textures the track draws with"},
    {"Languages/LanguageTextures.bin", 517284,
     "6A92604D9766557115DEE8F8CE18F392557AF1A8B0369E69B474F36DF2691615",
     "the in-race LCD glyphs"},
    {"Tracks/STREAML1RA.BUN", 92394688,
     "2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E",
     "the L1RA stream: every section of track 1003"},
    {"Tracks/STREAML1RA_1003.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1003's stream pack, empty in the retail game, which names it"},
    {"Tracks/TEX1003TRACK.BIN", 1086216,
     "081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5",
     "track 1003's sky and track map textures"},
    {"Tracks/TRACKB1003.lzc", 433352,
     "ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA",
     "track 1003's streaming table, zones, route and collision"},
};

constexpr std::size_t kRetailFileCount = sizeof(kRetailFiles) / sizeof(kRetailFiles[0]);

struct RetailSet {
    const char* name;
    const char* title;
    // Indices into kRetailFiles, in path order.
    std::vector<std::size_t> files;
};

const std::vector<RetailSet>& retail_sets() {
    static const std::vector<RetailSet> sets = {
        {"first-lap",
         "one timed lap of event 1 in the stock 240SX (issue #159)",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
        {"stock-car", "the stock 240SX nfsu_switch_car draws", {0, 1, 2, 3, 4, 7}},
        {"event-1",
         "race 1 of Underground mode: event 1, track 1003, stream L1RA",
         {5, 7, 8, 10, 11, 12, 13}},
    };
    return sets;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

char fold(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool printable(char c) noexcept {
    const unsigned char byte = static_cast<unsigned char>(c);
    return byte >= 0x20U && byte <= 0x7EU;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string part;
    for (const char c : text) {
        if (c == separator) {
            parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    parts.push_back(part);
    return parts;
}

std::string hex_byte(unsigned char byte) {
    char text[8];
    std::snprintf(text, sizeof text, "0x%02X", static_cast<unsigned int>(byte));
    return text;
}

// A name from a listing or a config line, as it may appear in a sentence:
// printable ASCII as it is, anything else as \xHH, and long names cut.
std::string quoted(const std::string& text) {
    std::string out = "\"";
    std::size_t shown = 0;
    for (const char c : text) {
        if (shown == 60) {
            out += "...";
            break;
        }
        const unsigned char byte = static_cast<unsigned char>(c);
        if (printable(c) && c != '"' && c != '\\') {
            out += c;
        } else {
            char code[8];
            std::snprintf(code, sizeof code, "\\x%02X", static_cast<unsigned int>(byte));
            out += code;
        }
        ++shown;
    }
    return out + "\"";
}

std::string join_root(const std::string& root, const std::string& relative) {
    if (root.empty()) {
        return relative;
    }
    if (root.back() == '/') {
        return root + relative;
    }
    return root + "/" + relative;
}

std::string trim(const std::string& text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && (text[first] == ' ' || text[first] == '\t')) {
        ++first;
    }
    while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
        --last;
    }
    return text.substr(first, last - first);
}

// ---------------------------------------------------------------------------
// Resolving paths, with each directory listed once
// ---------------------------------------------------------------------------

class DirectoryIndex {
public:
    DirectoryIndex(FileSystemService& files, const fs::path& root) : files_(files), root_(root) {}

    AssetResolution resolve(const std::string& relative) {
        AssetResolution result;
        fs::path current = root_;
        std::string found;
        const std::vector<std::string> names = split(relative, '/');
        for (const std::string& name : names) {
            const Listing& listing = list(found, current);
            if (!listing.listed) {
                if (found.empty()) {
                    result.lookup = AssetLookup::missing;
                    result.root_listed = false;
                    result.detail = "the root " + listing.reason;
                } else {
                    result.lookup = AssetLookup::unreadable;
                    result.detail = found + " cannot be listed: " + listing.reason;
                }
                result.found_as = found;
                return result;
            }
            std::vector<const std::string*> matches;
            for (const std::string& candidate : listing.names) {
                if (asset_names_match(candidate, name)) {
                    matches.push_back(&candidate);
                }
            }
            const std::string where = found.empty() ? std::string("the root") : found;
            if (matches.empty()) {
                result.lookup = AssetLookup::missing;
                result.detail = "no " + name + " in " + where;
                result.found_as = found;
                return result;
            }
            if (matches.size() > 1) {
                result.lookup = AssetLookup::ambiguous;
                result.detail = where + " holds " + std::to_string(matches.size()) +
                                " names that match " + name + ":";
                for (std::size_t i = 0; i < matches.size() && i < 4; ++i) {
                    result.detail += " " + quoted(*matches[i]);
                }
                result.found_as = found;
                return result;
            }
            current /= fs::path(*matches.front());
            found += (found.empty() ? "" : "/") + *matches.front();
        }
        result.lookup = AssetLookup::found;
        result.path = current;
        result.found_as = found;
        return result;
    }

    std::size_t listings() const noexcept { return listings_; }

private:
    struct Listing {
        bool listed = false;
        std::string reason;
        std::vector<std::string> names;
    };

    const Listing& list(const std::string& found, const fs::path& path) {
        const auto cached = cache_.find(found);
        if (cached != cache_.end()) {
            return cached->second;
        }
        Listing listing;
        ++listings_;
        listing.listed =
            files_.list_directory(path, kAssetMaxDirectoryNames, listing.names, listing.reason);
        if (!listing.listed) {
            listing.names.clear();
        }
        return cache_.emplace(found, std::move(listing)).first->second;
    }

    FileSystemService& files_;
    fs::path root_;
    std::map<std::string, Listing> cache_;
    std::size_t listings_ = 0;
};

// One file, found: sized, and hashed when the size is right.
void measure_file(FileSystemService& files, const fs::path& path, std::size_t index,
                  std::vector<unsigned char>& buffer, AssetFileReport& file,
                  AssetCheckReport& report, AssetCheckObserver* observer) {
    std::string reason;
    std::uint64_t size = 0;
    if (!files.file_size(path, size, reason)) {
        file.status = AssetStatus::unreadable;
        file.detail = reason;
        return;
    }
    file.sized = true;
    file.size = size;
    if (size != file.expected.size) {
        file.status = AssetStatus::wrong_size;
        file.detail = size < file.expected.size ? "shorter than the retail file: an interrupted copy?"
                                                : "longer than the retail file";
        return;
    }
    Sha256 hasher;
    std::uint64_t offset = 0;
    while (offset < size) {
        const std::uint64_t left = size - offset;
        const std::size_t want =
            left < buffer.size() ? static_cast<std::size_t>(left) : buffer.size();
        std::size_t got = 0;
        const bool read_ok = files.read_file(path, offset, buffer.data(), want, got, reason);
        ++report.reads;
        if (!read_ok) {
            file.status = AssetStatus::unreadable;
            file.detail = reason;
            return;
        }
        if (got > want) {
            got = want;
        }
        file.bytes_read += got;
        report.bytes_read += got;
        if (got == 0) {
            file.status = AssetStatus::unreadable;
            file.detail = "ended after " + std::to_string(offset) + " of " +
                          std::to_string(size) + " bytes: it changed while it was checked";
            return;
        }
        hasher.update(buffer.data(), got);
        offset += got;
        if (observer != nullptr) {
            observer->file_progress(index, offset, size);
        }
    }
    // One byte past the end must not be there: a file that grew after it was
    // sized is not the file that was hashed.
    std::size_t past = 0;
    const bool probed = files.read_file(path, size, buffer.data(), 1, past, reason);
    ++report.reads;
    if (!probed) {
        file.status = AssetStatus::unreadable;
        file.detail = reason;
        return;
    }
    if (past != 0) {
        file.bytes_read += 1;
        report.bytes_read += 1;
        file.status = AssetStatus::unreadable;
        file.detail = "holds more than the " + std::to_string(size) +
                      " bytes it was sized at: it changed while it was checked";
        return;
    }
    file.hashed = true;
    file.sha256 = sha256_hex(hasher.finish());
    if (file.sha256 == file.expected.sha256) {
        file.status = AssetStatus::ok;
        file.detail.clear();
    } else {
        file.status = AssetStatus::wrong_hash;
        file.detail = "another version of the file";
    }
}

AssetVerdict verdict_of(const AssetCheckReport& report) noexcept {
    if (report.count(AssetStatus::missing) != 0) {
        return AssetVerdict::missing_files;
    }
    if (report.count(AssetStatus::wrong_size) != 0) {
        return AssetVerdict::partial;
    }
    if (report.count(AssetStatus::wrong_hash) != 0) {
        return AssetVerdict::wrong_version;
    }
    if (report.count(AssetStatus::unreadable) != 0 || report.count(AssetStatus::ambiguous) != 0) {
        return AssetVerdict::unreadable;
    }
    return AssetVerdict::complete;
}

std::string upper(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

const char* status_label(AssetStatus status) noexcept {
    switch (status) {
    case AssetStatus::ok:
        return "ok";
    case AssetStatus::missing:
        return "MISSING";
    case AssetStatus::wrong_size:
        return "WRONG SIZE";
    case AssetStatus::wrong_hash:
        return "WRONG VERSION";
    case AssetStatus::unreadable:
        return "UNREADABLE";
    case AssetStatus::ambiguous:
        return "AMBIGUOUS";
    }
    return "UNKNOWN";
}

std::string padded(const std::string& text, std::size_t width) {
    return text.size() >= width ? text + " " : text + std::string(width - text.size(), ' ');
}

// Checks a root path from a config file; see parse_asset_config.
bool normalise_console_root(const std::string& value, std::string& root, std::string& why) {
    static const std::string prefix = "sdmc:/";
    if (value.empty()) {
        why = "is empty";
        return false;
    }
    if (value.size() > kAssetRootMaxBytes) {
        why = "is " + std::to_string(value.size()) + " bytes long; at most " +
              std::to_string(kAssetRootMaxBytes) + " are allowed";
        return false;
    }
    if (value.compare(0, prefix.size(), prefix) != 0) {
        why = "must start with sdmc:/ - an absolute path on the SD card - and reads " +
              quoted(value);
        return false;
    }
    if (value.find('\\') != std::string::npos) {
        why = "holds a backslash; separate folders with /";
        return false;
    }
    std::vector<std::string> parts = split(value.substr(prefix.size()), '/');
    if (!parts.empty() && parts.back().empty()) {
        parts.pop_back();
    }
    std::string normal = prefix;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part.empty()) {
            why = "holds an empty folder name (two / in a row)";
            return false;
        }
        if (part == "." || part == "..") {
            why = "holds the folder name " + part + ", which is not allowed";
            return false;
        }
        for (const char c : part) {
            if (!printable(c)) {
                why = std::string(c == '\t' ? "holds a tab" : "holds the byte " +
                                               hex_byte(static_cast<unsigned char>(c))) +
                      ", which a folder name on the SD card cannot";
                return false;
            }
            if (c == '"' || c == '*' || c == ':' || c == '<' || c == '>' || c == '?' ||
                c == '|') {
                why = std::string("holds the character ") + c +
                      ", which a folder name on the SD card cannot";
                return false;
            }
        }
        normal += (i == 0 ? "" : "/") + part;
    }
    root = normal;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Manifests
// ---------------------------------------------------------------------------

std::vector<std::string> retail_asset_manifest_names() {
    std::vector<std::string> names;
    for (const RetailSet& set : retail_sets()) {
        names.push_back(set.name);
    }
    return names;
}

bool retail_asset_manifest(const std::string& name, AssetManifest& manifest) {
    for (const RetailSet& set : retail_sets()) {
        if (name == set.name) {
            AssetManifest out;
            out.name = set.name;
            out.title = set.title;
            for (const std::size_t index : set.files) {
                if (index >= kRetailFileCount) {
                    return false;
                }
                const RetailFile& file = kRetailFiles[index];
                out.files.push_back(AssetFile{file.path, file.size, file.sha256, file.purpose});
            }
            manifest = std::move(out);
            return true;
        }
    }
    return false;
}

std::uint64_t asset_manifest_bytes(const AssetManifest& manifest) noexcept {
    std::uint64_t total = 0;
    for (const AssetFile& file : manifest.files) {
        total += file.size;
    }
    return total;
}

bool asset_names_match(const std::string& a, const std::string& b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) {
            return false;
        }
    }
    return true;
}

bool validate_asset_manifest(const AssetManifest& manifest, std::string& reason) {
    if (manifest.name.empty()) {
        reason = "the manifest has no name";
        return false;
    }
    if (manifest.files.empty() || manifest.files.size() > kAssetMaxManifestFiles) {
        reason = "the manifest " + manifest.name + " holds " +
                 std::to_string(manifest.files.size()) + " files; it must hold 1 to " +
                 std::to_string(kAssetMaxManifestFiles);
        return false;
    }
    for (std::size_t i = 0; i < manifest.files.size(); ++i) {
        const AssetFile& file = manifest.files[i];
        const std::string which = "file " + std::to_string(i) + " " + quoted(file.path);
        if (file.path.empty() || file.path.size() > kAssetMaxPathBytes) {
            reason = which + " has a path of " + std::to_string(file.path.size()) +
                     " bytes; it must have 1 to " + std::to_string(kAssetMaxPathBytes);
            return false;
        }
        for (const char c : file.path) {
            if (!printable(c) || c == '\\' || c == ':') {
                reason = which + " holds a byte a relative path here cannot: " +
                         hex_byte(static_cast<unsigned char>(c));
                return false;
            }
        }
        for (const std::string& part : split(file.path, '/')) {
            if (part.empty() || part == "." || part == "..") {
                reason = which + " has an empty, \".\" or \"..\" part";
                return false;
            }
        }
        if (file.size > kAssetMaxFileBytes) {
            reason = which + " is " + std::to_string(file.size) + " bytes; at most " +
                     std::to_string(kAssetMaxFileBytes) + " are allowed";
            return false;
        }
        bool hex = file.sha256.size() == 64;
        for (const char c : file.sha256) {
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                hex = false;
            }
        }
        if (!hex) {
            reason = which + " has a SHA-256 that is not 64 upper-case hexadecimal digits";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const std::vector<std::string> a = split(manifest.files[j].path, '/');
            const std::vector<std::string> b = split(file.path, '/');
            bool same = a.size() == b.size();
            for (std::size_t k = 0; same && k < a.size(); ++k) {
                same = asset_names_match(a[k], b[k]);
            }
            if (same) {
                reason = which + " names the same file as file " + std::to_string(j) + " " +
                         quoted(manifest.files[j].path);
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Finding and checking
// ---------------------------------------------------------------------------

AssetResolution resolve_asset_path(
    FileSystemService& files, const fs::path& root, const std::string& relative) {
    DirectoryIndex index(files, root);
    return index.resolve(relative);
}

const char* asset_status_name(AssetStatus status) noexcept {
    switch (status) {
    case AssetStatus::ok:
        return "ok";
    case AssetStatus::missing:
        return "missing";
    case AssetStatus::wrong_size:
        return "wrong-size";
    case AssetStatus::wrong_hash:
        return "wrong-hash";
    case AssetStatus::unreadable:
        return "unreadable";
    case AssetStatus::ambiguous:
        return "ambiguous";
    }
    return "unknown";
}

const char* asset_verdict_name(AssetVerdict verdict) noexcept {
    switch (verdict) {
    case AssetVerdict::complete:
        return "complete";
    case AssetVerdict::missing_files:
        return "missing files";
    case AssetVerdict::partial:
        return "partial";
    case AssetVerdict::wrong_version:
        return "wrong version";
    case AssetVerdict::unreadable:
        return "unreadable";
    }
    return "unknown";
}

std::size_t AssetCheckReport::count(AssetStatus status) const noexcept {
    const std::size_t index = static_cast<std::size_t>(status);
    return index < counts.size() ? counts[index] : 0;
}

bool check_asset_install(
    FileSystemService& files,
    const fs::path& root,
    const AssetManifest& manifest,
    AssetCheckReport& report,
    std::string& reason,
    AssetCheckObserver* observer) {
    if (!validate_asset_manifest(manifest, reason)) {
        return false;
    }
    AssetCheckReport out;
    out.manifest = manifest.name;
    out.title = manifest.title;
    out.root_listed = true;
    std::vector<unsigned char> buffer(kAssetReadChunkBytes);
    DirectoryIndex index(files, root);
    for (std::size_t i = 0; i < manifest.files.size(); ++i) {
        const AssetFile& expected = manifest.files[i];
        if (observer != nullptr) {
            observer->file_started(i, manifest.files.size(), expected);
        }
        AssetFileReport file;
        file.expected = expected;
        const AssetResolution found = index.resolve(expected.path);
        file.found = found.lookup == AssetLookup::found;
        file.found_as = found.found_as;
        if (!found.root_listed) {
            static const std::string prefix = "the root ";
            out.root_listed = false;
            out.root_problem = found.detail.compare(0, prefix.size(), prefix) == 0
                                   ? found.detail.substr(prefix.size())
                                   : found.detail;
        }
        switch (found.lookup) {
        case AssetLookup::found:
            measure_file(files, found.path, i, buffer, file, out, observer);
            break;
        case AssetLookup::missing:
            file.status = AssetStatus::missing;
            file.detail = found.detail;
            break;
        case AssetLookup::ambiguous:
            file.status = AssetStatus::ambiguous;
            file.detail = found.detail;
            break;
        case AssetLookup::unreadable:
            file.status = AssetStatus::unreadable;
            file.detail = found.detail;
            break;
        }
        ++out.counts[static_cast<std::size_t>(file.status)];
        out.files.push_back(std::move(file));
    }
    out.listings = index.listings();
    out.verdict = verdict_of(out);
    report = std::move(out);
    return true;
}

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------

std::vector<AssetLine> asset_report_lines(
    const AssetCheckReport& report, const std::string& root_text, AssetReportForm form) {
    const bool console = form == AssetReportForm::console;
    std::uint64_t total = 0;
    for (const AssetFileReport& file : report.files) {
        total += file.expected.size;
    }
    std::vector<AssetLine> head;
    head.push_back(AssetLine{"set " + report.manifest + ": " + report.title + ", " +
                                 std::to_string(report.files.size()) + " files, " +
                                 std::to_string(total) + " bytes",
                             AssetTone::plain});
    if (!report.root_listed) {
        head.push_back(AssetLine{"root " + root_text + " cannot be listed: it " +
                                     report.root_problem,
                                 AssetTone::bad});
    }

    std::vector<AssetLine> files;
    constexpr std::size_t kLabelWidth = 14;
    constexpr std::size_t kConsoleHashDigits = 8;
    for (const AssetFileReport& file : report.files) {
        std::string text = padded(status_label(file.status), kLabelWidth) + file.expected.path;
        switch (file.status) {
        case AssetStatus::ok:
            text += "  " + std::to_string(file.size) + " bytes, sha256 " +
                    (console ? std::string("matches") : file.sha256);
            break;
        case AssetStatus::missing:
            text += "  " + file.detail;
            break;
        case AssetStatus::wrong_size:
            text += "  " + std::to_string(file.size) + " bytes, expected " +
                    std::to_string(file.expected.size) + ": " +
                    (!console ? file.detail
                              : file.size < file.expected.size
                                    ? std::string("shorter: an interrupted copy?")
                                    : std::string("longer"));
            break;
        case AssetStatus::wrong_hash:
            text += "  " + std::to_string(file.size) + " bytes, sha256 " +
                    (console ? file.sha256.substr(0, kConsoleHashDigits) + "..." : file.sha256) +
                    " expected " +
                    (console ? file.expected.sha256.substr(0, kConsoleHashDigits) + "..."
                             : file.expected.sha256);
            break;
        case AssetStatus::unreadable:
        case AssetStatus::ambiguous:
            text += "  " + file.detail;
            break;
        }
        // Only a file that was found has a place to name; a lookup that
        // stopped part way names the folder it stopped at in its detail.
        if (file.found && file.found_as != file.expected.path) {
            text += " (found as " + file.found_as + ")";
        }
        files.push_back(AssetLine{text, file.status == AssetStatus::ok ? AssetTone::good
                                                                       : AssetTone::bad});
    }

    std::vector<AssetLine> verdict;
    const std::size_t ok = report.count(AssetStatus::ok);
    if (report.verdict == AssetVerdict::complete) {
        verdict.push_back(AssetLine{"install: COMPLETE - all " + std::to_string(ok) +
                                        " files match by name, size and SHA-256",
                                    AssetTone::good});
        verdict.push_back(AssetLine{"nothing to copy", AssetTone::good});
    } else {
        verdict.push_back(AssetLine{
            "install: " + upper(asset_verdict_name(report.verdict)) + " - " +
                std::to_string(ok) + " of " + std::to_string(report.files.size()) +
                " files ok: " + std::to_string(report.count(AssetStatus::missing)) +
                " missing, " + std::to_string(report.count(AssetStatus::wrong_size)) +
                " wrong size, " + std::to_string(report.count(AssetStatus::wrong_hash)) +
                " wrong version, " +
                std::to_string(report.count(AssetStatus::unreadable) +
                               report.count(AssetStatus::ambiguous)) +
                " unreadable",
            AssetTone::bad});
        const std::size_t copy = report.files.size() - ok;
        const std::string what = copy == 1 ? std::string("this file")
                                           : "these " + std::to_string(copy) + " files";
        verdict.push_back(AssetLine{
            "copy " + what + " from the game's installation folder" +
                (console ? " to the same " + std::string(copy == 1 ? "path" : "paths") +
                               " under " + root_text + ":"
                         : std::string(", keeping the folders:")),
            AssetTone::note});
        for (const AssetFileReport& file : report.files) {
            if (file.status != AssetStatus::ok) {
                verdict.push_back(AssetLine{
                    "  " + file.expected.path +
                        (console ? "  " + std::to_string(file.expected.size) + " bytes"
                                 : " -> " + join_root(root_text, file.expected.path)),
                    AssetTone::plain});
            }
        }
    }

    const AssetLine read{"read " + std::to_string(report.bytes_read) + " bytes in " +
                             std::to_string(report.reads) + " reads and " +
                             std::to_string(report.listings) + " directory listings",
                         AssetTone::plain};

    std::vector<AssetLine> lines = head;
    const std::vector<AssetLine>& first = console ? verdict : files;
    const std::vector<AssetLine>& second = console ? files : verdict;
    lines.insert(lines.end(), first.begin(), first.end());
    lines.insert(lines.end(), second.begin(), second.end());
    lines.push_back(read);
    return lines;
}

// ---------------------------------------------------------------------------
// The console
// ---------------------------------------------------------------------------

bool parse_asset_config(const std::string& text, std::string& root, std::string& reason) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEFU &&
        static_cast<unsigned char>(text[1]) == 0xBBU &&
        static_cast<unsigned char>(text[2]) == 0xBFU) {
        reason = "starts with a UTF-8 byte order mark; save it as plain ASCII";
        return false;
    }
    std::size_t line = 1;
    std::size_t column = 1;
    for (const char c : text) {
        if (c == '\n') {
            ++line;
            column = 1;
            continue;
        }
        if (!printable(c) && c != '\t' && c != '\r') {
            reason = "line " + std::to_string(line) + " column " + std::to_string(column) +
                     " holds the byte " + hex_byte(static_cast<unsigned char>(c)) +
                     ", which is not printable ASCII; the file must be plain ASCII";
            return false;
        }
        ++column;
    }
    std::vector<std::string> lines = split(text, '\n');
    std::string found;
    std::size_t found_line = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string content = lines[i];
        const std::string number = "line " + std::to_string(i + 1);
        if (!content.empty() && content.back() == '\r') {
            content.pop_back();
        }
        if (content.find('\r') != std::string::npos) {
            reason = number + " holds a carriage return that does not end it";
            return false;
        }
        const std::string trimmed = trim(content);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            reason = number + " is neither a comment nor a setting: " + quoted(trimmed);
            return false;
        }
        const std::string key = trim(trimmed.substr(0, equals));
        const std::string value = trim(trimmed.substr(equals + 1));
        if (key != "root") {
            reason = number + " sets " + quoted(key) +
                     ", which is not a setting; the only one is root";
            return false;
        }
        if (found_line != 0) {
            reason = number + " sets root again; line " + std::to_string(found_line) +
                     " already set it";
            return false;
        }
        std::string why;
        if (!normalise_console_root(value, found, why)) {
            reason = number + ": the root " + why;
            return false;
        }
        found_line = i + 1;
    }
    if (found_line == 0) {
        reason = "sets no root; write a line such as root = sdmc:/nfsu, or delete the "
                 "file to use the default";
        return false;
    }
    root = found;
    return true;
}

AssetConfig load_asset_config(
    FileSystemService& files,
    const std::string& sd_root,
    const std::string& relative,
    const std::string& default_root) {
    AssetConfig config;
    config.file = join_root(sd_root, relative);
    const AssetResolution found = resolve_asset_path(files, fs::path(sd_root), relative);
    if (found.lookup == AssetLookup::missing && found.root_listed) {
        config.state = AssetConfigState::absent;
        config.root = default_root;
        config.detail = found.detail;
        return config;
    }
    config.state = AssetConfigState::refused;
    config.advice = "Fix the file, or delete it to use " + default_root + ". Nothing was checked.";
    if (!found.root_listed || found.lookup == AssetLookup::unreadable) {
        // A folder the file would be in could not be listed - review comment
        // 3761, F1: the card's top level too - so the file may be there.
        static const std::string prefix = "the root ";
        config.detail = "cannot be looked for: " +
                        (found.root_listed ? found.detail
                         : found.detail.compare(0, prefix.size(), prefix) == 0
                             ? sd_root + " " + found.detail.substr(prefix.size())
                             : found.detail);
        config.advice = "Whether there is a config file cannot be told, so nothing was checked.";
        return config;
    }
    if (found.lookup != AssetLookup::found) {
        config.detail = "cannot be read: " + found.detail;
        return config;
    }
    std::string reason;
    std::uint64_t size = 0;
    if (!files.file_size(found.path, size, reason)) {
        config.detail = "cannot be read: it " + reason;
        return config;
    }
    if (size > kAssetConfigMaxBytes) {
        config.detail = "is " + std::to_string(size) + " bytes; a config file holds at most " +
                        std::to_string(kAssetConfigMaxBytes);
        return config;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size) + 1);
    std::size_t got = 0;
    if (!files.read_file(found.path, 0, bytes.data(), bytes.size(), got, reason)) {
        config.detail = "cannot be read: it " + reason;
        return config;
    }
    if (got != size) {
        config.detail = "changed while it was read: " + std::to_string(got) +
                        " bytes where it was sized at " + std::to_string(size);
        return config;
    }
    const std::string text(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(got));
    std::string root;
    if (!parse_asset_config(text, root, reason)) {
        config.detail = reason;
        return config;
    }
    config.state = AssetConfigState::applied;
    config.root = root;
    return config;
}

ConsoleAssetCheck run_console_asset_check(
    FileSystemService& files,
    const ConsoleAssetSetup& setup,
    const AssetManifest& manifest,
    AssetCheckObserver* observer) {
    ConsoleAssetCheck result;
    result.config = load_asset_config(files, setup.sd_root, setup.config, setup.default_root);
    const AssetConfig& config = result.config;
    switch (config.state) {
    case AssetConfigState::absent:
        result.lines.push_back(AssetLine{"config: " + config.file + " not found; root " +
                                             config.root + " (the default)",
                                         AssetTone::note});
        break;
    case AssetConfigState::applied:
        result.lines.push_back(
            AssetLine{"config: " + config.file + " sets root " + config.root, AssetTone::note});
        break;
    case AssetConfigState::refused:
        result.lines.push_back(
            AssetLine{"config: " + config.file + " REFUSED: " + config.detail, AssetTone::bad});
        result.lines.push_back(AssetLine{config.advice, AssetTone::bad});
        result.lines.push_back(AssetLine{"set " + manifest.name + " needs these " +
                                             std::to_string(manifest.files.size()) +
                                             " files under the root:",
                                         AssetTone::note});
        for (const AssetFile& file : manifest.files) {
            result.lines.push_back(AssetLine{"  " + file.path + "  " +
                                                 std::to_string(file.size) + " bytes",
                                             AssetTone::plain});
        }
        return result;
    }
    std::string reason;
    if (!check_asset_install(files, fs::path(config.root), manifest, result.report, reason,
                             observer)) {
        result.lines.push_back(AssetLine{"nothing checked: " + reason, AssetTone::bad});
        return result;
    }
    result.checked = true;
    for (AssetLine& line :
         asset_report_lines(result.report, config.root, AssetReportForm::console)) {
        result.lines.push_back(std::move(line));
    }
    return result;
}

} // namespace nfsu
