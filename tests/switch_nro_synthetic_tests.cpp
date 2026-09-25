// A desktop stand-in for the Switch NRO header checks (review comment 2856,
// F3 and N3), which otherwise only run against a real build-switch output
// and so never run at all on a machine without the Switch SDK.
//
// tests/switch_nro_check.cmake is a CMake script, not C++, and CMake's own
// string(ASCII 0 ...) refuses to produce a NUL byte - there is no way to
// build an arbitrary binary fixture like an NRO from pure CMake script code.
// This is a compiled test instead, which writes the fixture with a plain
// std::ofstream in binary mode and then runs the checker script exactly as
// switch/CMakeLists.txt does, as a subprocess, over each fixture in turn.
//
// The synthetic NRO is built from the real measurements PART 2 of the
// review gives for an actual devkitA64/uam build (166085 bytes total,
// a 143360-byte image, a 0x4000-byte NACP); the individual text, ro and
// data segment sizes are not given there, so this picks a self-consistent
// split rather than the real one, which the checks below never depend on
// past their own consistency.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef NFSU_CMAKE_COMMAND
#error "NFSU_CMAKE_COMMAND must be defined (the cmake executable this build uses)"
#endif
#ifndef NFSU_SWITCH_NRO_CHECKER
#error "NFSU_SWITCH_NRO_CHECKER must be defined (tests/switch_nro_check.cmake)"
#endif

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

// A fixture name no other process shares, built the way
// tests/track_object_tests.cpp builds one (CLAUDE.md, issue #122): two
// sessions running ctest at once, each in its own build directory, is
// routine here, and a shared name has already cost this suite a real,
// intermittently reproducing crash once.
std::string unique_fixture(const std::string& label) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    return "nfsu_switch_nro_synthetic_" + label + "_" + std::to_string(pid);
}

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

struct CheckResult {
    int exit_code = -1;
    std::string output;
};

// Every checker run passes a path from fs::temp_directory_path() or from
// CMAKE_CURRENT_SOURCE_DIR, either of which can hold a space (this
// project's own checkout can: "Game Dev") or a non-ASCII byte. That rules
// out a single command STRING run through a shell: cmd.exe's /c parsing
// keeps quote characters only in narrow cases (cmd /?, under "quote
// characters on the command line are preserved"; this project's fixture
// paths do not meet them, an executable path with no space inside its own
// quotes among them), and otherwise strips only the first and the last
// quote character on the whole line, which desyncs every other quoted
// argument's pairing rather than failing cleanly. Argument vectors passed
// straight to CreateProcess or execvp are not parsed by any shell at all,
// so this runs the checker that way on both platforms instead.
#ifdef _WIN32
// The quoting CreateProcess's own command-line parser (and every C
// runtime's argv startup code) expects: a backslash is literal unless it
// immediately precedes a quote, N of them before a quote fold to N/2
// backslashes plus, for odd N, one literal quote; unquoted, an argument
// with no space, tab or quote needs nothing added.
std::string win32_quote_arg(const std::string& arg) {
    if (!arg.empty() &&
        arg.find_first_of(" \t\"\n\v\r") == std::string::npos) {
        return arg;
    }
    std::string result = "\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++backslashes;
            ++i;
        }
        if (i == arg.size()) {
            result.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            result.append(backslashes * 2 + 1, '\\');
            result.push_back('"');
        } else {
            result.append(backslashes, '\\');
            result.push_back(arg[i]);
        }
    }
    result.push_back('"');
    return result;
}

CheckResult run_process(const std::vector<std::string>& argv) {
    CheckResult result;
    std::string command_line;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) {
            command_line.push_back(' ');
        }
        command_line += win32_quote_arg(argv[i]);
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &attributes, 0)) {
        result.output = "CreatePipe failed";
        return result;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_end;
    startup.hStdError = write_end;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    std::vector<char> mutable_line(command_line.begin(), command_line.end());
    mutable_line.push_back('\0');
    const BOOL started =
        CreateProcessA(nullptr, mutable_line.data(), nullptr, nullptr, TRUE,
                        0, nullptr, nullptr, &startup, &process);
    CloseHandle(write_end);
    if (!started) {
        CloseHandle(read_end);
        result.output = "CreateProcess failed for: " + command_line;
        return result;
    }
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(read_end, buffer.data(),
                     static_cast<DWORD>(buffer.size()), &read, nullptr) &&
           read > 0) {
        result.output.append(buffer.data(), read);
    }
    CloseHandle(read_end);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    result.exit_code = static_cast<int>(exit_code);
    return result;
}
#else
CheckResult run_process(const std::vector<std::string>& argv) {
    CheckResult result;
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        result.output = "pipe() failed";
        return result;
    }
    const pid_t child = fork();
    if (child < 0) {
        result.output = "fork() failed";
        return result;
    }
    if (child == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const std::string& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);
        execvp(c_argv[0], c_argv.data());
        _exit(127);
    }
    close(pipe_fds[1]);
    std::array<char, 4096> buffer{};
    ssize_t read_count = 0;
    while ((read_count = read(pipe_fds[0], buffer.data(), buffer.size())) >
           0) {
        result.output.append(buffer.data(),
                              static_cast<std::size_t>(read_count));
    }
    close(pipe_fds[0]);
    int status = 0;
    waitpid(child, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}
#endif

// CMake's own message() wraps a long line at a column width and indents the
// continuation, so a two- or three-word expected substring this file looks
// for can straddle a wrap point that has nothing to do with the checker's
// own logic. Every whitespace run - the wrap's newline and indentation
// included - collapses to one space before any of this file's checks
// search the output, so a wrap never turns a real match into a miss.
std::string normalize_whitespace(const std::string& text) {
    std::string result;
    bool in_space = false;
    for (char ch : text) {
        const bool is_space =
            ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v';
        if (is_space) {
            if (!in_space && !result.empty()) {
                result.push_back(' ');
            }
            in_space = true;
        } else {
            result.push_back(ch);
            in_space = false;
        }
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

CheckResult run_checker(const fs::path& nro, const fs::path& elf,
                         const std::string& title) {
    const std::vector<std::string> argv = {
        fs::path(NFSU_CMAKE_COMMAND).string(),
        "-DNRO=" + nro.string(),
        "-DELF=" + elf.string(),
        "-DTITLE=" + title,
        "-P",
        fs::path(NFSU_SWITCH_NRO_CHECKER).string(),
    };
    CheckResult result = run_process(argv);
    result.output = normalize_whitespace(result.output);
    return result;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path();
    const fs::path nro_path = dir / (unique_fixture("nro") + ".nro");
    const fs::path elf_path = dir / (unique_fixture("elf") + ".elf");

    const Bytes control = build_control_nro();
    const Bytes elf = build_synthetic_elf();
    write_binary(elf_path, elf);

    // The control case: the checker must accept the fixture built from the
    // review's real measurements, exactly as it accepts a real build.
    write_binary(nro_path, control);
    {
        const CheckResult result = run_checker(nro_path, elf_path, kTitle);
        check(result.exit_code == 0,
              "control: the synthetic NRO passes (exit " +
                  std::to_string(result.exit_code) + "): " + result.output);
        check(result.output.find("switch NRO check:") != std::string::npos,
              "control: the checker prints its passing summary line");
    }

    // Each corruption is applied to a fresh copy of the control bytes, so
    // cases never compound. must_fail asserts a non-zero exit and the given
    // substring in a failure line; a case documented as a known gap asserts
    // the opposite, that the corrupted file still passes, so a future fix
    // that starts catching it is noticed here rather than silently assumed.
    auto must_fail = [&](const std::string& label, Bytes bytes,
                          const std::string& expect_substring) {
        write_binary(nro_path, bytes);
        const CheckResult result = run_checker(nro_path, elf_path, kTitle);
        check(result.exit_code != 0,
              label + ": the checker refuses it (exit " +
                  std::to_string(result.exit_code) + "): " + result.output);
        check(result.output.find(expect_substring) != std::string::npos,
              label + ": the failure line names it - looked for `" +
                  expect_substring + "` in: " + result.output);
    };
    auto must_pass = [&](const std::string& label, Bytes bytes) {
        write_binary(nro_path, bytes);
        const CheckResult result = run_checker(nro_path, elf_path, kTitle);
        check(result.exit_code == 0,
              label + ": still passes (exit " +
                  std::to_string(result.exit_code) + "): " + result.output);
    };

    // The 5 header corruptions PART 2 already found caught, kept here as a
    // regression check against the reordering this fix did.
    {
        Bytes bytes = control;
        bytes.at(0x13) = static_cast<unsigned char>('1'); // NRO0 -> NRO1
        must_fail("magic changed to NRO1", bytes, "no NRO0 magic");
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kImageSizeAt, 0xFFFFFFFFu);
        must_fail("image size 0xFFFFFFFF", bytes, "is outside the file's");
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kImageSizeAt, kImageSize - 0x1000u);
        must_fail("image size - 0x1000", bytes, "the image size");
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kImageSizeAt, 0);
        must_fail("image size 0", bytes, "is outside the file's");
    }
    {
        Bytes bytes = control;
        for (std::size_t i = 0; i < kHeaderSize; ++i) {
            bytes.at(i) = 0;
        }
        must_fail("first 0x80 bytes zeroed", bytes, "no NRO0 magic");
    }

    // The 4 header corruptions F3 adds checks for (review comment 2856):
    // version, the MOD0 offset, and the two segment-table corruptions PART
    // 2 recorded as missed.
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kVersionAt, 7);
        must_fail("version 7", bytes, "the NRO version is 7, not 0");
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kModOffsetAt, 0xFFFFFFF0u);
        must_fail("MOD0 offset 0xFFFFFFF0", bytes,
                   "the MOD0 offset 4294967280 is outside the text segment");
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kTextSizeAt, 0);
        must_fail("text size 0", bytes, "the ro segment is at offset");
    }
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kDataOffsetAt, 0xFFFFF000u);
        must_fail("data offset 0xFFFFF000", bytes,
                   "the data segment is at offset");
    }

    // bss size (0x38) is the fifth field PART 2 found unread, and it stays
    // unread: there is no other value in the file to check it against, the
    // same way the review's own suggested fix leaves it out. Documented as
    // a known gap rather than silently assumed: this asserts the corrupted
    // file still passes, so a change that starts catching it is noticed
    // here.
    {
        Bytes bytes = control;
        patch_u32_le(bytes, kBssSizeAt, 0xFFFFFFFFu);
        must_pass("bss size 0xFFFFFFFF (known gap, not read)", bytes);
    }

    // The asset and NACP corruptions PART 2 already found caught, kept here
    // as a regression check.
    {
        Bytes bytes = control;
        const std::size_t at = kImageSize + kAsetIconSizeAt;
        patch_u64_le(bytes, at, 0);
        must_fail("icon size 0", bytes, "carries no icon");
    }
    {
        Bytes bytes = control;
        const std::size_t at = kImageSize + kAsetNacpSizeAt;
        patch_u64_le(bytes, at, 0x3000);
        must_fail("NACP size 0x3000", bytes, "is 12288 bytes, not 0x4000");
    }
    {
        Bytes bytes = control;
        const std::size_t at = kImageSize + kAsetNacpOffsetAt;
        patch_u64_le(bytes, at, kAsetNacpOffset + 1);
        must_fail("NACP offset + 1", bytes, "runs past the file");
    }
    {
        Bytes bytes = control;
        const std::size_t title_at =
            static_cast<std::size_t>(kImageSize + kAsetNacpOffset);
        bytes.at(title_at + 4) = static_cast<unsigned char>('a'); // switch -> swatch
        bytes.at(title_at + 5) = static_cast<unsigned char>('t');
        must_fail("title nfsu_switch -> nfsu_swatch", bytes,
                   "the NACP's first title is not");
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
        write_binary(nro_path, bytes);
        const CheckResult result = run_checker(nro_path, elf_path, kTitle);
        check(result.exit_code != 0,
              "N3 icon offset 2^63: the checker refuses it (exit " +
                  std::to_string(result.exit_code) + "): " + result.output);
        check(result.output.find("runs past the file") != std::string::npos,
              "N3 icon offset 2^63: a failure line names it: " +
                  result.output);
        check(result.output.find("cannot evaluate") == std::string::npos,
              "N3 icon offset 2^63: no CMake math error leaked through: " +
                  result.output);
    }

    // The ELF must still be refused as an NRO, and accepted as an ELF -
    // unrelated to F3, but the checker reads both files every run, and a
    // change here would otherwise go unnoticed by this file's cases alone.
    {
        const CheckResult result = run_checker(elf_path, elf_path, kTitle);
        check(result.exit_code != 0,
              "the checker still refuses the ELF as an NRO: " +
                  result.output);
    }

    std::error_code ignored;
    fs::remove(nro_path, ignored);
    fs::remove(elf_path, ignored);

    if (g_failures != 0) {
        std::cerr << g_failures << " switch NRO synthetic test failures of "
                  << g_checks << " checks\n";
        return 1;
    }
    std::cout << "switch NRO synthetic tests passed (" << g_checks
              << " checks)\n";
    return 0;
}
