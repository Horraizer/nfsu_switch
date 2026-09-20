#include "nfsu/arg_path.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
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
// wrong file, and it is not written here as one. The Windows
// extended-length prefix is exempt, because "\\?\C:\dir\out.tga" is a
// spelling this project supports: see strip_extended_prefix in
// src/render.cpp.

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

bool argument_to_path(const char* text, fs::path& out, std::string& reason) {
#ifdef _WIN32
    return argument_to_path_in_code_page(text, GetACP(), out, reason);
#else
    return argument_to_path_in_code_page(text, 0U, out, reason);
#endif
}

bool check_arguments(int argc, char** argv, int first, std::string& reason) {
    if (argv == nullptr) {
        return true;
    }
    for (int i = first; i < argc; ++i) {
        fs::path ignored;
        std::string detail;
        if (!argument_to_path(argv[i], ignored, detail)) {
            reason = "argument " + std::to_string(i) + " " + detail;
            return false;
        }
    }
    return true;
}

fs::path argument_path(const char* text) {
    fs::path path;
    std::string ignored;
    if (!argument_to_path(text, path, ignored)) {
        return fs::path();
    }
    return path;
}

} // namespace nfsu
