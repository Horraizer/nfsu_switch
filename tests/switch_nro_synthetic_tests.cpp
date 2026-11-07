// A desktop stand-in for the Switch NRO header checks (review comment 2856,
// F3 and N3), which otherwise only run against a real build-switch output
// and so never run at all on a machine without the Switch SDK.
//
// tests/switch_nro_check.cmake is a CMake script, not C++, and CMake's own
// string(ASCII 0 ...) refuses to produce a NUL byte - there is no way to
// build an arbitrary binary fixture like an NRO from pure CMake script code.
// This program writes the fixtures with a plain std::ofstream in binary
// mode; tests/switch_nro_synthetic_guards.cmake is what actually runs the
// checker against them and asserts the results (review comment 2942, R2).
//
// This file used to run the checker itself, as a child process: the Win32
// process-launch API under _WIN32, the POSIX pair of process-launch calls
// under #else. The #else half had never been built anywhere this branch was
// developed, which is exactly how issue #132 happened - code for the
// platform nobody was building broke the first Linux build the moment it
// was. So this file now launches no process of any kind and has no
// platform #ifdef: it only builds the fixture bytes and writes them to a
// directory its caller names. Invoked with no arguments it also runs a
// small self-check of the write path itself (below main()'s comment).
//
// The synthetic NRO is built from the real measurements PART 2 of the
// review gives for an actual devkitA64/uam build (166085 bytes total,
// a 143360-byte image, a 0x4000-byte NACP); the individual text, ro and
// data segment sizes are not given there, so this picks a self-consistent
// split rather than the real one, which the checks below never depend on
// past their own consistency.

#include "nfsu/arg_path.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<unsigned char>;

void write_binary(const fs::path& path, const Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void push_bytes(Bytes& out, std::size_t count, unsigned char value) {
    out.insert(out.end(), count, value);
}

void push_text(Bytes& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

void push_u32_le(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
    }
}

void push_u64_le(Bytes& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
    }
}

void patch_u32_le(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes.at(offset + static_cast<std::size_t>(i)) =
            static_cast<unsigned char>((value >> (8 * i)) & 0xFF);
    }
}

void patch_u64_le(Bytes& bytes, std::size_t offset, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        bytes.at(offset + static_cast<std::size_t>(i)) =
            static_cast<unsigned char>((value >> (8 * i)) & 0xFF);
    }
}

// The NRO0 header layout (0x80 bytes), as switchbrew and
// tests/switch_nro_check.cmake both read it: Unused(4), ModOffset(4),
// Padding(8), Magic(4), Version(4), Size(4), Flags(4), then the text, ro
// and data NroSegment pairs (offset u32, size u32, 8 bytes each), BssSize
// (4), Reserved (4), BuildId (32), Padding (8) and the three extended
// segment headers (24, unused here).
constexpr std::size_t kModOffsetAt = 0x04;
constexpr std::size_t kVersionAt = 0x14;
constexpr std::size_t kImageSizeAt = 0x18;
constexpr std::size_t kTextOffsetAt = 0x20;
constexpr std::size_t kTextSizeAt = 0x24;
constexpr std::size_t kRoOffsetAt = 0x28;
constexpr std::size_t kRoSizeAt = 0x2C;
constexpr std::size_t kDataOffsetAt = 0x30;
constexpr std::size_t kDataSizeAt = 0x34;
constexpr std::size_t kBssSizeAt = 0x38;
constexpr std::size_t kHeaderSize = 0x80;

// The chosen segment split. Only their sums and offsets matter to the
// checks; these are not the real build's sizes, which PART 2 of the review
// does not give.
constexpr std::uint32_t kModOffset = 256;
constexpr std::uint32_t kTextOffset = 0;
constexpr std::uint32_t kTextSize = 96000;
constexpr std::uint32_t kRoOffset = kTextOffset + kTextSize;
constexpr std::uint32_t kRoSize = 32000;
constexpr std::uint32_t kDataOffset = kRoOffset + kRoSize;
constexpr std::uint32_t kDataSize = 15360;
constexpr std::uint32_t kBssSize = 8192;
constexpr std::uint32_t kImageSize = kDataOffset + kDataSize; // 143360, PART 2

// ASET header layout (0x38 bytes): Magic(4), Version(4), then
// {offset,size} u64 pairs for the icon, the NACP and a romfs this
// application does not carry.
constexpr std::size_t kAsetIconOffsetAt = 0x08;
constexpr std::size_t kAsetIconSizeAt = 0x10;
constexpr std::size_t kAsetNacpOffsetAt = 0x18;
constexpr std::size_t kAsetNacpSizeAt = 0x20;
constexpr std::size_t kAsetSize = 0x38;

const std::string kTitle = "nfsu_switch"; // switch/CMakeLists.txt's nx_generate_nacp title
constexpr std::uint64_t kNacpSize = 16384; // 0x4000, PART 2 and the NACP format both fix this
// The icon fills the ASET header's leftover space up to the NACP; its
// size is derived, not measured, the same way the segment split above is:
// PART 2 gives the full file size (166085) and the image size (143360),
// and every other length here, so the icon is whatever is left.
constexpr std::uint64_t kTotalFileSize = 166085; // PART 2
constexpr std::uint64_t kAsetIconOffset = kAsetSize; // right after the ASET header
constexpr std::uint64_t kAsetNacpOffset =
    kTotalFileSize - kImageSize - kAsetSize - kNacpSize + kAsetIconOffset;
constexpr std::uint64_t kAsetIconSize = kAsetNacpOffset - kAsetIconOffset;

Bytes build_header() {
    Bytes h;
    push_u32_le(h, 0); // Unused
    push_u32_le(h, kModOffset);
    push_bytes(h, 8, 0); // Padding
    push_text(h, "NRO0");
    push_u32_le(h, 0); // Version
    push_u32_le(h, kImageSize);
    push_u32_le(h, 0); // Flags
    push_u32_le(h, kTextOffset);
    push_u32_le(h, kTextSize);
    push_u32_le(h, kRoOffset);
    push_u32_le(h, kRoSize);
    push_u32_le(h, kDataOffset);
    push_u32_le(h, kDataSize);
    push_u32_le(h, kBssSize);
    push_u32_le(h, 0); // Reserved
    push_bytes(h, 32, 0); // BuildId
    push_bytes(h, 8, 0); // Padding
    push_bytes(h, 24, 0); // ApiInfo, Dynstr, Dynsym
    if (h.size() != kHeaderSize) {
        std::cerr << "internal error: synthetic header is " << h.size()
                  << " bytes, not " << kHeaderSize << '\n';
    }
    return h;
}

Bytes build_aset() {
    Bytes a;
    push_text(a, "ASET");
    push_u32_le(a, 0); // Version
    push_u64_le(a, kAsetIconOffset);
    push_u64_le(a, kAsetIconSize);
    push_u64_le(a, kAsetNacpOffset);
    push_u64_le(a, kNacpSize);
    push_u64_le(a, 0); // romfs offset
    push_u64_le(a, 0); // romfs size
    if (a.size() != kAsetSize) {
        std::cerr << "internal error: synthetic ASET header is " << a.size()
                  << " bytes, not " << kAsetSize << '\n';
    }
    return a;
}

// The control NRO, byte for byte: header, the rest of the image, the ASET
// header, the icon and the NACP with the title the checker is told to
// expect at its start.
Bytes build_control_nro() {
    Bytes bytes = build_header();
    push_bytes(bytes, kImageSize - kHeaderSize, 0x00);
    const Bytes aset = build_aset();
    bytes.insert(bytes.end(), aset.begin(), aset.end());
    push_bytes(bytes, static_cast<std::size_t>(kAsetIconSize), 0xFF);
    push_text(bytes, kTitle);
    bytes.push_back(0x00);
    push_bytes(bytes,
               static_cast<std::size_t>(kNacpSize) - kTitle.size() - 1, 0xCC);
    return bytes;
}

// Just enough of an AArch64 ELF header for nfsu_read_elf: the identification
// bytes and a little-endian e_machine of 183 (EM_AARCH64). Nothing else in
// the file is a real ELF.
Bytes build_synthetic_elf() {
    Bytes bytes;
    bytes.push_back(0x7F);
    push_text(bytes, "ELF");
    bytes.push_back(0x02); // ELFCLASS64
    bytes.push_back(0x01); // ELFDATA2LSB
    push_bytes(bytes, 12, 0x00); // rest of e_ident, up to offset 18
    push_u32_le(bytes, 183); // e_type (2 bytes) + e_machine (2 bytes): both
                              // read as one LE 32-bit word by nfsu_le, low
                              // half zero for e_type, 183 for e_machine
    push_bytes(bytes, 48, 0x00); // pad to 64 bytes, nfsu_read_elf's LIMIT
    return bytes;
}

// A text segment that does not start at file offset 0 (review comment
// 2942, R3): every other field is shifted by the same amount so the fixture
// stays otherwise self-consistent, and the real checker's text_offset check
// - the very first segment check it runs - is the only thing that can catch
// it. kShift is well under kModOffset (256), so MOD0 stays inside the
// shifted text segment and that check does not also fire.
Bytes build_text_offset_shifted_nro(const Bytes& control) {
    constexpr std::uint32_t kShift = 64;
    Bytes bytes = control;
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                 kShift, 0x00);
    patch_u32_le(bytes, kTextOffsetAt, kShift);
    patch_u32_le(bytes, kRoOffsetAt, kShift + kTextSize);
    patch_u32_le(bytes, kDataOffsetAt, kShift + kTextSize + kRoSize);
    patch_u32_le(bytes, kImageSizeAt, kImageSize + kShift);
    return bytes;
}

// One fixture this program can write: <name>.nro (or <name>.elf) inside
// whatever directory it is told to use.
struct Fixture {
    std::string filename;
    Bytes bytes;
};

// Every fixture tests/switch_nro_synthetic_guards.cmake checks. The byte
// patches mirror what PART 2 and F3 and N3 of review comment 2856, and R3
// of review comment 2942, found; the CMake guard script names the exact
// exit status and failure line each one expects, which has to be kept in
// sync with the patch made here by hand - there is no way to derive one
// language's expectation from the other's code.
std::vector<Fixture> build_fixtures() {
    std::vector<Fixture> out;
    const Bytes control = build_control_nro();
    out.push_back({"control.nro", control});
    out.push_back({"control.elf", build_synthetic_elf()});

    // The 5 header corruptions PART 2 already found caught.
    {
        Bytes bytes = control;
        bytes.at(0x13) = static_cast<unsigned char>('1'); // NRO0 -> NRO1
        out.push_back({"magic_nro1.nro", bytes});
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kImageSizeAt, 0xFFFFFFFFu);
        out.push_back({"image_size_ffffffff.nro", bytes});
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kImageSizeAt, kImageSize - 0x1000u);
        out.push_back({"image_size_minus_0x1000.nro", bytes});
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kImageSizeAt, 0);
        out.push_back({"image_size_zero.nro", bytes});
    }
    {
        Bytes bytes = control;
        for (std::size_t i = 0; i < kHeaderSize; ++i) {
            bytes.at(i) = 0;
        }
        out.push_back({"header_zeroed.nro", bytes});
    }

    // The 4 header corruptions F3 adds checks for (review comment 2856):
    // version, the MOD0 offset, and the two segment-table corruptions PART
    // 2 recorded as missed.
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kVersionAt, 7);
        out.push_back({"version_7.nro", bytes});
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kModOffsetAt, 0xFFFFFFF0u);
        out.push_back({"mod_offset_bad.nro", bytes});
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kTextSizeAt, 0);
        out.push_back({"text_size_zero.nro", bytes});
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kDataOffsetAt, 0xFFFFF000u);
        out.push_back({"data_offset_bad.nro", bytes});
    }

    // bss size (0x38) is the fifth field PART 2 found unread, and it stays
    // unread: there is no other value in the file to check it against, the
    // same way the review's own suggested fix leaves it out. This fixture
    // is a known gap, not a regression: the guard script asserts it still
    // passes, so a change that starts catching it is noticed there.
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kBssSizeAt, 0xFFFFFFFFu);
        out.push_back({"bss_size_ffffffff.nro", bytes});
    }

    // The asset and NACP corruptions PART 2 already found caught.
    {
        Bytes bytes = control;
        const std::size_t at = kImageSize + kAsetIconSizeAt;
        patch_u64_le(bytes, at, 0);
        out.push_back({"icon_size_zero.nro", bytes});
    }
    {
        Bytes bytes = control;
        const std::size_t at = kImageSize + kAsetNacpSizeAt;
        patch_u64_le(bytes, at, 0x3000);
        out.push_back({"nacp_size_3000.nro", bytes});
    }
    {
        Bytes bytes = control;
        const std::size_t at = kImageSize + kAsetNacpOffsetAt;
        patch_u64_le(bytes, at, kAsetNacpOffset + 1);
        out.push_back({"nacp_offset_plus1.nro", bytes});
    }
    {
        Bytes bytes = control;
        const std::size_t title_at =
            static_cast<std::size_t>(kImageSize + kAsetNacpOffset);
        bytes.at(title_at + 4) = static_cast<unsigned char>('a'); // switch -> swatch
        bytes.at(title_at + 5) = static_cast<unsigned char>('t');
        out.push_back({"title_mismatch.nro", bytes});
    }

    // N3: an asset offset of 2^63 or more used to make nfsu_le call
    // math(EXPR) on a hex value it cannot parse, which stops the whole
    // script with "cannot evaluate the expression" rather than a failure
    // line. The fix routes it to the same out-of-range sentinel a read past
    // the end of the file already returns, so this must fail with an
    // ordinary failure line and never print CMake's own math error.
    {
        Bytes bytes = control;
        const std::size_t at = kImageSize + kAsetIconOffsetAt;
        patch_u64_le(bytes, at, 0x8000000000000000ULL);
        out.push_back({"icon_offset_2pow63.nro", bytes});
    }

    // R3 (review comment 2942): a non-zero text segment offset, which used
    // to have no case of its own - every other fixture here keeps text
    // segment at offset 0, so a mutant that disabled that one check alone
    // survived the sweep.
    out.push_back(
        {"text_offset_shifted.nro", build_text_offset_shifted_nro(control)});

    return out;
}

bool path_exists(const fs::path& path) {
    std::error_code ignored;
    return fs::exists(path, ignored);
}

// Writes every fixture into `dir`, refusing to write any of them if any one
// of their target paths already exists (CLAUDE.md, issue #122: two sessions
// running ctest at once, each writing fixtures for this same program, is
// routine, and a fixture silently overwritten out from under a running
// checker is exactly how that issue's crash happened). Returns 0 on
// success, 1 otherwise.
int write_fixtures(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "could not create " << dir.u8string() << ": "
                  << ec.message() << '\n';
        return 1;
    }
    const std::vector<Fixture> fixtures = build_fixtures();
    for (const Fixture& fixture : fixtures) {
        const fs::path path = dir / fixture.filename;
        if (path_exists(path)) {
            std::cerr << "refusing to overwrite an existing fixture: "
                      << path.u8string() << '\n';
            return 1;
        }
    }
    for (const Fixture& fixture : fixtures) {
        write_binary(dir / fixture.filename, fixture.bytes);
    }
    std::cout << "wrote " << fixtures.size()
              << " switch NRO synthetic fixtures to " << dir.u8string()
              << '\n';
    return 0;
}

// A directory this process's own run does not share with another one:
// several sessions running this program's self-check at once, each in its
// own build directory, is routine here (CLAUDE.md, Shared checkout). This
// file has no platform #ifdef, so it cannot call GetCurrentProcessId() or
// getpid() the way tests/track_object_tests.cpp does for the same reason -
// both need a header only one platform has. A high-resolution timestamp
// combined with a std::random_device draw gives the same guarantee a
// process id would: two runs never choose the same suffix.
std::string unique_suffix() {
    const auto now = std::chrono::high_resolution_clock::now()
                          .time_since_epoch()
                          .count();
    std::random_device rd;
    std::ostringstream oss;
    oss << std::hex << static_cast<unsigned long long>(now) << '_' << rd();
    return oss.str();
}

} // namespace

// With no arguments: a self-check of write_fixtures() itself, run as an
// ordinary ctest entry. tests/switch_nro_synthetic_guards.cmake is what
// actually runs tests/switch_nro_check.cmake against these fixtures and
// checks its verdicts; this only checks that writing them behaves as
// documented - every fixture appears, and a second write into the same
// directory refuses rather than silently replacing the first.
//
// With `--write-fixtures <dir>`: writes every fixture into <dir> and exits,
// which is the mode tests/switch_nro_synthetic_guards.cmake drives.
int main(int argc, char** argv) {
    std::string argument_error;
    if (!nfsu::check_arguments(argc, argv, 1, argument_error)) {
        std::cerr << "error: " << argument_error << '\n';
        return 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--write-fixtures") {
        if (argc != 3) {
            std::cerr << "--write-fixtures needs exactly one output "
                         "directory argument\n";
            return 1;
        }
        return write_fixtures(nfsu::argument_path(argv[2]));
    }
    if (argc != 1) {
        std::cerr
            << "usage: nfsu_switch_nro_synthetic_tests [--write-fixtures <dir>]\n";
        return 1;
    }

    int checks = 0;
    int failures = 0;
    auto check = [&](bool condition, const std::string& message) {
        ++checks;
        if (!condition) {
            std::cerr << "test failure: " << message << '\n';
            ++failures;
        }
    };

    const fs::path dir = fs::temp_directory_path() /
                          ("nfsu_switch_nro_synthetic_" + unique_suffix());
    const int first = write_fixtures(dir);
    check(first == 0, "the first write into a fresh directory succeeds");

    const std::vector<Fixture> fixtures = build_fixtures();
    for (const Fixture& fixture : fixtures) {
        const fs::path path = dir / fixture.filename;
        check(path_exists(path), "fixture " + fixture.filename + " was written");
        if (path_exists(path)) {
            std::error_code ec;
            const std::uintmax_t size = fs::file_size(path, ec);
            check(!ec && size == fixture.bytes.size(),
                  "fixture " + fixture.filename + " is the expected size");
        }
    }

    const int second = write_fixtures(dir);
    check(second != 0,
          "a second write into the same directory refuses to overwrite");

    std::error_code ignored;
    for (const Fixture& fixture : fixtures) {
        fs::remove(dir / fixture.filename, ignored);
    }
    fs::remove(dir, ignored);

    if (failures != 0) {
        std::cerr << failures << " switch NRO synthetic test failures of "
                  << checks << " checks\n";
        return 1;
    }
    std::cout << "switch NRO synthetic tests passed (" << checks
              << " checks)\n";
    return 0;
}
