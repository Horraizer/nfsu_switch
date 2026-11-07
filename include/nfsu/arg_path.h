#pragma once

#include <filesystem>
#include <string>

// One command-line argument, turned into a path the program can open.
//
// Windows hands a program that has a narrow main() its arguments already
// encoded in the process's active ANSI code page. libstdc++ stores a
// std::filesystem::path as wide characters on this platform and reads a
// narrow string as UTF-8, so std::filesystem::path(argv[k]) throws
// std::filesystem::filesystem_error for any argument that the ANSI code page
// spelled with a byte above 0x7E. Nothing catches it, so the program calls
// std::terminate before it prints anything. This repository's own checkout
// sits under such a directory.
//
// argument_to_path decodes the bytes the way the operating system encoded
// them, so those paths open normally, and reports the two arguments it
// refuses instead of throwing. docs/PATH_ARGUMENTS.md states the boundary,
// including what the '?' refusal does not do and why the Windows
// extended-length prefix "\\?\" is exempt from it.
//
// The conversion is the desktop FileSystemService of include/nfsu/platform.h:
// the three calls below go through desktop_file_system(), and code that
// must run on a console as well takes a FileSystemService& instead of
// calling them. docs/PLATFORM_SERVICES.md.
namespace nfsu {

class FileSystemService;

// The desktop implementation of FileSystemService: argument_to_path below,
// against the process's active ANSI code page on Windows. It holds no
// state and lives for the whole program.
FileSystemService& desktop_file_system();

// Converts one argument. Returns false and leaves `out` untouched when the
// argument cannot be converted, setting `reason` to a sentence that names
// the offending text and says what is wrong with it.
//
// On platforms other than Windows a narrow argument is already in the
// filesystem's own encoding: the path stores the bytes unchanged, nothing
// can fail, and every argument is accepted.
bool argument_to_path(const char* text, std::filesystem::path& out,
                      std::string& reason);

// The same conversion against a stated Windows code page rather than the
// process's active one, so that a test can pin both answers without
// depending on the code page the machine it runs on happens to use.
// argument_to_path calls this with the active ANSI code page.
//
// On other platforms `code_page` is ignored and this is argument_to_path.
bool argument_to_path_in_code_page(const char* text, unsigned int code_page,
                                   std::filesystem::path& out, std::string& reason);

// Checks arguments `first` through `argc - 1` in one pass, before any of
// them is used. Returns false and sets `reason` for the first argument that
// argument_to_path would refuse, naming it by position as well as by text.
//
// Every argument is checked, not only the ones that become paths: the rest
// are option names, decimal and hexadecimal numbers, and retail texture,
// mesh and car names, none of which has a spelling this refuses.
bool check_arguments(int argc, char** argv, int first, std::string& reason);

// The path for an argument check_arguments accepted. An argument it would
// have refused converts to an empty path, which names no file and fails at
// the first open, rather than throwing out of a program that cannot catch
// it. Nothing here reports an error, because the one report belongs at
// check_arguments, before any file is touched.
std::filesystem::path argument_path(const char* text);

} // namespace nfsu
