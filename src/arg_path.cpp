#include "nfsu/arg_path.h"

#include "nfsu/platform.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <limits>
#include <new>
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

// What this file decides, and why it is a decision rather than a detail:
// docs/PATH_ARGUMENTS.md. The short form is that a Windows process with a
// narrow main() never sees the characters the user typed. It sees them
// re-encoded in the active ANSI code page, with every character that code
// page cannot express replaced by '?'. Decoding those bytes with the same
// code page is the whole of the fix, and it is what makes a path under a
// Cyrillic directory open on a Cyrillic machine.
//
// Two arguments are refused instead. Bytes that do not decode at all have
// no second reading to fall back on. And a '?' that survived into the
// argument vector is either a character that was typed or the wreck of one
// the code page could not carry, and nothing here can tell those apart.
//
// The second rule is narrower than it looks, and docs/PATH_ARGUMENTS.md
// measures it rather than asserting it. The C runtime this project builds
// with expands wildcards in the argument vector before main() runs, so a
// '?' only reaches this code when it matched no file - which is also when
// it could not have opened the wrong one. What the refusal buys is a
// diagnostic that names the substitution, in place of a later "failed to
// read file size" that does not. It is not a guard against opening the
// wrong file, and it is not written here as one.
//
// The Windows extended-length prefix is exempt. "\\?\C:\dir\out.tga" is a
// legitimate spelling that strip_extended_prefix in src/render.cpp already
// handles in the overwrite and same-file alias checks - and only there:
// the prefix does not open a file through this standard library, which
// src/render.cpp:316 records and docs/PATH_ARGUMENTS.md measures. Refusing
// it here would take away what the alias checks accept today.

namespace nfsu {

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
// Printable ASCII kept as it is; quote, backslash and every other byte as
// \xHH. The reason text goes to a console whose encoding is not this
// program's business, and it has to stay ASCII whatever the argument was.
std::string ascii_quoted(const char* text) {
    std::string out = "\"";
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const unsigned char byte = static_cast<unsigned char>(*cursor);
        if (byte >= 0x20U && byte <= 0x7EU && byte != '"' && byte != '\\') {
            out += static_cast<char>(byte);
        } else {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\x%02X",
                          static_cast<unsigned int>(byte));
            out += buffer;
        }
    }
    return out + "\"";
}

bool is_separator(char byte) {
    return byte == '\\' || byte == '/';
}

// How many leading bytes the '?' scan below has to skip. Four for the
// Windows extended-length prefix "\\?\", whose third character is a '?'
// that means the prefix and nothing else; zero otherwise. The same shape
// strip_extended_prefix in src/render.cpp reads, including its acceptance
// of either separator, so that the two agree on what a prefix is.
std::size_t extended_prefix_length(const char* text) {
    if (text[0] != '\0' && is_separator(text[0]) && text[1] != '\0' &&
        is_separator(text[1]) && text[2] == '?' && text[3] != '\0' &&
        is_separator(text[3])) {
        return 4;
    }
    return 0;
}
#endif

// The desktop FileSystemService (include/nfsu/platform.h). This file is
// where the conversion has always lived, and it stays the only place that
// asks Windows for the code page; the class only puts it behind the
// interface, so that the programs below and a console that implements the
// interface differently reach it the same way.
//
// Its three read-only calls (issue #47) are std::filesystem and
// std::ifstream, which take the wide path argument_to_path builds on
// Windows, so a root under a Cyrillic directory lists and reads as any
// other. Every error is taken as a std::error_code rather than thrown, and
// no reason quotes the operating system's own message: on this machine that
// message is localised, and a reason has to stay ASCII.
class ArgumentPathFileSystem final : public FileSystemService {
public:
    bool argument_to_path(const char* text, fs::path& out, std::string& reason) override {
#ifdef _WIN32
        return argument_to_path_in_code_page(text, GetACP(), out, reason);
#else
        return argument_to_path_in_code_page(text, 0U, out, reason);
#endif
    }

    bool list_directory(const fs::path& directory, std::size_t max_names,
                        std::vector<std::string>& names, std::string& reason) override {
        names.clear();
        std::error_code error;
        const fs::file_status status = fs::status(directory, error);
        if (status.type() == fs::file_type::not_found) {
            reason = "does not exist";
            return false;
        }
        if (!error && status.type() != fs::file_type::directory) {
            reason = "is not a directory";
            return false;
        }
        error.clear();
        fs::directory_iterator entry(directory, error);
        if (error) {
            reason = "cannot be listed (error " + std::to_string(error.value()) + ")";
            return false;
        }
        std::size_t seen = 0;
        for (; entry != fs::directory_iterator(); entry.increment(error)) {
            if (error) {
                break;
            }
            ++seen;
            if (seen > max_names) {
                names.clear();
                reason = "holds more than " + std::to_string(max_names) + " names";
                return false;
            }
            // A name this standard library cannot spell in UTF-8 is not
            // listed: it cannot equal any name a caller asks for, all of
            // which are ASCII, and it still counts against the bound.
            try {
                names.push_back(entry->path().filename().string());
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception&) {
            }
        }
        if (error) {
            names.clear();
            reason = "cannot be listed (error " + std::to_string(error.value()) + ")";
            return false;
        }
        return true;
    }

    bool file_size(const fs::path& path, std::uint64_t& size, std::string& reason) override {
        std::error_code error;
        const fs::file_status status = fs::status(path, error);
        if (status.type() == fs::file_type::not_found) {
            reason = "does not exist";
            return false;
        }
        if (error) {
            reason = "cannot be sized (error " + std::to_string(error.value()) + ")";
            return false;
        }
        if (status.type() == fs::file_type::directory) {
            reason = "is a directory, not a file";
            return false;
        }
        if (status.type() != fs::file_type::regular) {
            reason = "is not a regular file";
            return false;
        }
        const std::uintmax_t bytes = fs::file_size(path, error);
        if (error) {
            reason = "cannot be sized (error " + std::to_string(error.value()) + ")";
            return false;
        }
        size = static_cast<std::uint64_t>(bytes);
        return true;
    }

    bool read_file(const fs::path& path, std::uint64_t offset, unsigned char* buffer,
                   std::size_t count, std::size_t& got, std::string& reason) override {
        got = 0;
        std::ifstream stream(path, std::ios::in | std::ios::binary);
        if (!stream) {
            reason = "cannot be opened for reading";
            return false;
        }
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            reason = "cannot be read at byte " + std::to_string(offset);
            return false;
        }
        stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream) {
            reason = "cannot be positioned at byte " + std::to_string(offset);
            return false;
        }
        if (count == 0) {
            return true;
        }
        stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(count));
        const std::streamsize taken = stream.gcount();
        if (stream.bad() || (!stream && !stream.eof()) || taken < 0) {
            reason = "cannot be read at byte " + std::to_string(offset);
            return false;
        }
        got = static_cast<std::size_t>(taken);
        return true;
    }
};

} // namespace

bool argument_to_path_in_code_page(
    const char* text,
    [[maybe_unused]] unsigned int code_page,
    fs::path& out,
    std::string& reason) {
    if (text == nullptr) {
        reason = "is missing";
        return false;
    }
#ifdef _WIN32
    // The substitution '?' first, because it decodes cleanly and so would
    // never be caught below. The leading extended-length prefix is skipped:
    // its '?' is the prefix, not a file name character.
    const std::size_t scan_from = extended_prefix_length(text);
    const char* const substituted = std::strchr(text + scan_from, '?');
    if (substituted != nullptr) {
        reason = "holds '?' at offset " + std::to_string(substituted - text) +
                 ": " + ascii_quoted(text) +
                 ". Windows writes '?' in place of every character the active "
                 "ANSI code page cannot express, so this may not be the text "
                 "that was typed, and a '?' outside a leading \\\\?\\ prefix "
                 "cannot appear in a Windows file name";
        return false;
    }
    const int length = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, text, -1,
                                           nullptr, 0);
    if (length <= 0) {
        const unsigned long error = GetLastError();
        reason = "is not a valid sequence in code page " + std::to_string(code_page) +
                 ", the encoding this process receives its arguments in: " +
                 ascii_quoted(text) +
                 ", with every byte outside printable ASCII shown as \\xHH";
        if (error != ERROR_NO_UNICODE_TRANSLATION) {
            reason += " (conversion failed with Windows error " +
                      std::to_string(error) + ")";
        }
        return false;
    }
    // length counts the terminating null, because the input length was -1,
    // so the buffer holds it and the string is built from the rest. Writing
    // that null into a std::wstring's own terminator would be a write to
    // the one position a std::wstring does not own.
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, text, -1, buffer.data(),
                            length) != length) {
        reason = "could not be converted from code page " + std::to_string(code_page) +
                 ": " + ascii_quoted(text);
        return false;
    }
    out = fs::path(std::wstring(buffer.data(), static_cast<std::size_t>(length - 1)));
    return true;
#else
    // std::filesystem::path stores these bytes unchanged here, so there is
    // nothing to decode and nothing that can fail. '?' is an ordinary
    // character in a POSIX file name and is not refused.
    out = fs::path(text);
    return true;
#endif
}

FileSystemService& desktop_file_system() {
    static ArgumentPathFileSystem files;
    return files;
}

// The three calls the programs make, each now a call through the desktop
// service. check_arguments and argument_path were the loop and the fallback
// that src/platform.cpp now holds as check_argument_paths and
// argument_path_or_empty, moved there unchanged; tests/arg_path_tests.cpp
// and tests/path_arguments.cmake pin that nothing a program prints or
// returns moved with them.
bool argument_to_path(const char* text, fs::path& out, std::string& reason) {
    return desktop_file_system().argument_to_path(text, out, reason);
}

bool check_arguments(int argc, char** argv, int first, std::string& reason) {
    return check_argument_paths(desktop_file_system(), argc, argv, first, reason);
}

fs::path argument_path(const char* text) {
    return argument_path_or_empty(desktop_file_system(), text);
}

} // namespace nfsu
