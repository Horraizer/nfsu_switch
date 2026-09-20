#include "nfsu/arg_path.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        ++g_failures;
    }
}

// Not one non-ASCII byte appears in this file. Every byte above 0x7E that
// these tests need is written as a numeric escape and assembled here, so
// that the source stays inside the repository's ASCII-only artifact rule
// while the values reaching the code under test do not.
std::string bytes(const std::vector<unsigned char>& values) {
    std::string text;
    for (const unsigned char value : values) {
        text.push_back(static_cast<char>(value));
    }
    return text;
}

// "Razrabotka" in Cyrillic, as code page 1251 spells it. This is what the
// Windows argument vector holds for this repository's own checkout
// directory on a Russian-language machine, and constructing a
// std::filesystem::path from it directly is what threw.
const std::vector<unsigned char> kCyrillic1251 = {0xD0, 0xE0, 0xE7, 0xF0, 0xE0,
                                                  0xE1, 0xEE, 0xF2, 0xEA, 0xE0};

// The same ten letters as UTF-16 code units, which is what a correct
// conversion out of code page 1251 has to produce.
const std::wstring kCyrillicWide = {0x0420, 0x0430, 0x0437, 0x0440, 0x0430,
                                    0x0431, 0x043E, 0x0442, 0x043A, 0x0430};

void test_ascii_passes_through() {
    fs::path path;
    std::string reason;
    check(nfsu::argument_to_path("build/240sx.tga", path, reason),
          "an ASCII argument is accepted");
    check(reason.empty(), "an accepted argument leaves no reason");
    check(path == fs::path("build/240sx.tga"), "an ASCII argument converts unchanged");

    check(nfsu::argument_to_path("", path, reason), "an empty argument is accepted");
    check(nfsu::argument_path("") == fs::path(), "an empty argument is an empty path");

    // Every byte the rule names as printable ASCII, minus '?', which is
    // refused on Windows and is the one exception the boundary carries.
    std::string printable;
    for (unsigned char byte = 0x20U; byte <= 0x7EU; ++byte) {
        if (byte != static_cast<unsigned char>('?')) {
            printable.push_back(static_cast<char>(byte));
        }
    }
    check(nfsu::argument_to_path(printable.c_str(), path, reason),
          "every printable ASCII byte but '?' is accepted: " + reason);
}

void test_missing_argument() {
    fs::path path;
    std::string reason;
    check(!nfsu::argument_to_path(nullptr, path, reason),
          "a null argument is refused rather than dereferenced");
    check(reason == "is missing", "a null argument says what is wrong: " + reason);
}

void test_check_arguments_names_the_argument() {
    // argv as a program receives it: a program name, then arguments. The
    // one that is refused is argument 2, and the reason has to say so,
    // because a run with eight paths on it is otherwise a guessing game.
    char arg0[] = "nfsu_viewer";
    char arg1[] = "GEOMETRY.BIN";
    char arg2[] = "out?.tga";
    char arg3[] = "TEXTURES.BIN";
    char* argv[] = {arg0, arg1, arg2, arg3};
    std::string reason;
    const bool accepted = nfsu::check_arguments(4, argv, 1, reason);
#ifdef _WIN32
    check(!accepted, "an argument holding '?' is refused");
    check(reason.rfind("argument 2 ", 0) == 0,
          "the reason names the argument by position: " + reason);
    check(reason.find("holds '?' at offset 3") != std::string::npos,
          "the reason names the offending character and where it is: " + reason);
    check(reason.find("\"out?.tga\"") != std::string::npos,
          "the reason quotes the argument as given: " + reason);
#else
    check(accepted, "'?' is an ordinary character in a POSIX file name");
#endif

    // The same vector without the refused argument is accepted whole.
    char arg2_ok[] = "out.tga";
    char* clean[] = {arg0, arg1, arg2_ok, arg3};
    std::string clean_reason;
    check(nfsu::check_arguments(4, clean, 1, clean_reason),
          "a vector with nothing wrong with it is accepted: " + clean_reason);

    // `first` is honoured: nfsu_probe's argv[0] is a path this check has no
    // business reading, and a caller that starts later must be able to.
    char* only_bad[] = {arg2, arg1};
    std::string skipped_reason;
    check(nfsu::check_arguments(2, only_bad, 1, skipped_reason),
          "checking from index 1 does not read index 0");
}

#ifdef _WIN32

void test_code_page_1251_round_trip() {
    // The whole point of the change, pinned against a stated code page so
    // that the answer does not depend on the machine the test runs on.
    const std::string text = bytes(kCyrillic1251);
    fs::path path;
    std::string reason;
    check(nfsu::argument_to_path_in_code_page(text.c_str(), 1251U, path, reason),
          "code page 1251 decodes its own Cyrillic bytes: " + reason);
    check(path.wstring() == kCyrillicWide,
          "the decoded path holds the ten Cyrillic letters, not the ten bytes");

    // The same bytes read as UTF-8 are not a valid sequence, which is
    // exactly what std::filesystem::path did with them before this change,
    // and what it threw over.
    fs::path utf8_path;
    std::string utf8_reason;
    check(!nfsu::argument_to_path_in_code_page(text.c_str(), 65001U, utf8_path,
                                               utf8_reason),
          "the same bytes are not valid UTF-8");
    check(utf8_reason.find("is not a valid sequence in code page 65001") !=
              std::string::npos,
          "the refusal names the code page it tried: " + utf8_reason);
    check(utf8_reason.find("\\xD0\\xE0\\xE7") != std::string::npos,
          "the refusal shows the bytes as \\xHH: " + utf8_reason);
    check(utf8_path.empty(), "a refused argument leaves the output path untouched");
}

void test_invalid_sequence_is_refused() {
    // A lead byte with nothing after it, in a double-byte code page: 0x81
    // opens a two-byte character in code page 932 and there is no second
    // byte here.
    const std::string truncated = bytes({0x81});
    fs::path dbcs_path;
    std::string dbcs_reason;
    check(!nfsu::argument_to_path_in_code_page(truncated.c_str(), 932U, dbcs_path,
                                               dbcs_reason),
          "a truncated double-byte sequence is refused");
    check(dbcs_reason.find("is not a valid sequence in code page 932") !=
              std::string::npos,
          "the refusal names the code page it tried: " + dbcs_reason);
    check(dbcs_path.empty(), "a refused argument leaves the output path untouched");

    // The counter-case, so that the one above is not passing because the
    // function refuses everything: the same lead byte with a legal trail
    // byte is one character and is accepted.
    const std::string whole = bytes({0x81, 0x40});
    fs::path whole_path;
    std::string whole_reason;
    check(nfsu::argument_to_path_in_code_page(whole.c_str(), 932U, whole_path,
                                              whole_reason),
          "a complete double-byte sequence is accepted: " + whole_reason);
    check(whole_path.wstring().size() == 1,
          "two bytes of a double-byte code page are one character");

    // A single-byte code page can refuse too, but only where it leaves a
    // byte unassigned. 0xAA is unassigned in code page 1253 and 0xA1 in
    // code page 1257.
    const std::string greek = bytes({'x', 0xAA, 'y'});
    fs::path greek_path;
    std::string greek_reason;
    check(!nfsu::argument_to_path_in_code_page(greek.c_str(), 1253U, greek_path,
                                               greek_reason),
          "an unassigned byte of a single-byte code page is refused");
    check(greek_reason.find("\\xAA") != std::string::npos,
          "the refusal shows the offending byte as \\xHH: " + greek_reason);

    // And the half that is easy to assume the other way round. Code page
    // 1251, which is the one this repository's own machine runs, assigns a
    // character to all 256 of its bytes, so no argument can fail to decode
    // in it and '?' is the whole of what a run there refuses. That is a
    // measurement, not a property of single-byte code pages - the two
    // above are single-byte as well - and docs/PATH_ARGUMENTS.md says so
    // because of this loop.
    for (unsigned int value = 0x80U; value <= 0xFFU; ++value) {
        const std::string one = bytes({static_cast<unsigned char>(value)});
        fs::path decoded;
        std::string ignored;
        if (!nfsu::argument_to_path_in_code_page(one.c_str(), 1251U, decoded,
                                                 ignored)) {
            check(false, "code page 1251 refused byte " + std::to_string(value) +
                             ", so what docs/PATH_ARGUMENTS.md says about this "
                             "machine's own code page is wrong: " + ignored);
            break;
        }
    }
}

void test_substitution_character() {
    // Windows writes '?' for every character the active ANSI code page
    // cannot express, so an argument holding one is not necessarily the
    // text that was typed. It is also reserved in a Windows path. Both
    // halves are why it is refused rather than passed on.
    fs::path path;
    std::string reason;
    check(!nfsu::argument_to_path_in_code_page("C:\\??\\x.bin", 1251U, path, reason),
          "an argument holding '?' is refused");
    check(reason.find("holds '?' at offset 3") != std::string::npos,
          "the first '?' is the one reported: " + reason);
    check(path.empty(), "a refused argument leaves the output path untouched");

    // The contract argument_path carries for the call sites: a refused
    // argument becomes an empty path, which names no file and fails at the
    // first open. Returning the raw bytes instead is the throwing call the
    // issue reported, and nothing else here would notice.
    check(nfsu::argument_path("C:\\?\\x.bin").empty(),
          "a refused argument converts to an empty path");
    check(nfsu::argument_path("build/240sx.tga") == fs::path("build/240sx.tga"),
          "an accepted argument converts to its own path");

    // The refusal is about '?' only. '*' is reserved in a Windows path too,
    // but Windows never produces one by substitution, so nothing here has
    // grounds to refuse it and the boundary does not claim to.
    fs::path star;
    std::string star_reason;
    check(nfsu::argument_to_path_in_code_page("C:\\*\\x.bin", 1251U, star, star_reason),
          "'*' is not refused by this rule: " + star_reason);
}

void test_extended_length_prefix() {
    // "\\?\C:\dir\out.tga" is the Windows extended-length prefix, the only
    // spelling that reaches a path longer than MAX_PATH. This project
    // supports it deliberately - strip_extended_prefix in src/render.cpp
    // exists for it, and docs/RUNTIME_SKIN.md records a review that fixed
    // --skin-dump for exactly this spelling - so the '?' rule has to let it
    // through. A rule that refused it would take a supported spelling away
    // while looking like a safety check.
    const char* const accepted[] = {
        "\\\\?\\C:\\dir\\out.tga",
        "\\\\?\\UNC\\server\\share\\out.tga",
        "//?/C:/dir/out.tga",
        "\\\\?\\C:\\",
    };
    for (const char* const text : accepted) {
        fs::path path;
        std::string reason;
        check(nfsu::argument_to_path_in_code_page(text, 1251U, path, reason),
              std::string("the extended-length prefix is accepted: ") + text + " " +
                  reason);
        check(path.wstring().size() == std::strlen(text),
              std::string("the prefix is kept, not stripped here: ") + text);
    }

    // Only the prefix's own '?' is exempt. One anywhere after it is the
    // substitution case again and is still refused, and the reported offset
    // is the real one rather than an offset into the remainder.
    fs::path path;
    std::string reason;
    check(!nfsu::argument_to_path_in_code_page("\\\\?\\C:\\a?b\\out.tga", 1251U, path,
                                               reason),
          "a '?' after the prefix is still refused");
    check(reason.find("holds '?' at offset 8") != std::string::npos,
          "the offset is counted from the start of the argument: " + reason);

    // And a lone "\\?" that is not a prefix, because nothing separates it
    // from a file name, is not exempt either.
    fs::path lone;
    std::string lone_reason;
    check(!nfsu::argument_to_path_in_code_page("\\\\?x\\out.tga", 1251U, lone,
                                               lone_reason),
          "\\\\?x is not the extended-length prefix and is refused");
}

// The one thing every other check here cannot see: which code page
// argument_to_path picks. Every case above states a code page, and the two
// programs call nothing but argument_to_path, which states none - so a
// conversion that used the OEM code page instead of the ANSI one would
// satisfy all of them while opening a different file for every non-ASCII
// argument.
//
// The character is discovered rather than assumed, so that this runs on any
// machine: the first one above U+007F that this code page can carry and
// bring back unchanged. On a code page that can carry none, there is
// nothing to test and the loop says so.
void test_active_code_page_is_used() {
    std::wstring carried;
    std::string encoded;
    for (wchar_t value = 0x00A1; value < 0x0500 && carried.empty(); ++value) {
        const std::wstring candidate(1, value);
        BOOL substituted = FALSE;
        char narrow[16] = {0};
        const int written =
            WideCharToMultiByte(CP_ACP, 0, candidate.c_str(), -1, narrow, sizeof(narrow),
                                nullptr, &substituted);
        if (written <= 0 || substituted) {
            continue;
        }
        fs::path decoded;
        std::string reason;
        if (!nfsu::argument_to_path_in_code_page(narrow, CP_ACP, decoded, reason) ||
            decoded.wstring() != candidate) {
            continue;
        }
        carried = candidate;
        encoded = narrow;
    }
    if (carried.empty()) {
        std::cout << "note: this code page carries no character above U+007F, "
                     "so the active-code-page case has nothing to try\n";
        return;
    }

    // The argument as Windows would hand it over, and the path it has to
    // come back as.
    const std::string argument = "C:\\nfsu" + encoded + "\\GEOMETRY.BIN";
    const std::wstring expected = L"C:\\nfsu" + carried + L"\\GEOMETRY.BIN";
    fs::path path;
    std::string reason;
    check(nfsu::argument_to_path(argument.c_str(), path, reason),
          "an argument in the active code page is accepted: " + reason);
    check(path.wstring() == expected,
          "argument_to_path decodes with the active code page, not another one");
    check(nfsu::argument_path(argument.c_str()).wstring() == expected,
          "argument_path decodes with the active code page too");

    // And the same through a real file, so that a conversion producing the
    // right characters but a path nothing can open is not mistaken for a
    // working one.
    std::error_code error;
    const fs::path root = fs::temp_directory_path(error) / "nfsu_arg_path_active";
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    const fs::path directory = root / fs::path(L"nfsu" + carried);
    fs::create_directories(directory, error);
    check(!error, "a directory named in the active code page could be created");
    {
        std::ofstream out(directory / "probe.bin", std::ios::binary);
        out << "nfsu";
    }
    fs::path opened;
    std::string open_reason;
    // The file path as a narrow argument, encoded exactly the way Windows
    // encodes an argument vector.
    BOOL substituted = FALSE;
    const std::wstring wide_file = (directory / "probe.bin").wstring();
    const int size = WideCharToMultiByte(CP_ACP, 0, wide_file.c_str(), -1, nullptr, 0,
                                         nullptr, nullptr);
    check(size > 0, "the file path can be encoded in the active code page");
    std::string narrow(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide_file.c_str(), -1, &narrow[0], size, nullptr,
                        &substituted);
    check(!substituted, "the file path survives encoding without substitution");
    check(nfsu::argument_to_path(narrow.c_str(), opened, open_reason),
          "the encoded file path is accepted: " + open_reason);
    check(fs::exists(opened, error) && fs::file_size(opened, error) == 4U,
          "the decoded path opens the file that was created");

    fs::remove_all(root, error);
}

void test_reaches_the_real_filesystem() {
    // A conversion that produces the right wide string and still cannot
    // open a file would pass every check above. This one writes a file
    // under a directory whose name is the Cyrillic word, through the
    // converted path alone, and reads it back.
    std::error_code error;
    const fs::path root = fs::temp_directory_path(error) / "nfsu_arg_path_tests";
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    check(!error, "the test root could be created");

    const std::string name = bytes(kCyrillic1251);
    fs::path converted;
    std::string reason;
    check(nfsu::argument_to_path_in_code_page(name.c_str(), 1251U, converted, reason),
          "the directory name converts: " + reason);
    const fs::path directory = root / converted;
    fs::create_directories(directory, error);
    check(!error, "a directory with a Cyrillic name could be created");

    const fs::path file = directory / "probe.bin";
    {
        std::ofstream out(file, std::ios::binary);
        out << "nfsu";
    }
    check(fs::exists(file, error), "the file under the Cyrillic directory exists");
    check(fs::file_size(file, error) == 4U,
          "the file under the Cyrillic directory has the bytes written to it");

    fs::remove_all(root, error);
}

#endif // _WIN32

} // namespace

int main() {
    test_ascii_passes_through();
    test_missing_argument();
    test_check_arguments_names_the_argument();
#ifdef _WIN32
    test_code_page_1251_round_trip();
    test_invalid_sequence_is_refused();
    test_substitution_character();
    test_extended_length_prefix();
    test_active_code_page_is_used();
    test_reaches_the_real_filesystem();
#endif
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "argument path tests passed\n";
    return 0;
}
