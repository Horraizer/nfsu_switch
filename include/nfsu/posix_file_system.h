#pragma once

// The FileSystemService of the console applications (issue #47).
//
// libnx mounts the SD card as "sdmc:" in newlib's device table, so the
// POSIX calls newlib provides - opendir, readdir, stat - and std::ifstream,
// which the stock-car application already reads the card with, reach it
// like any other file system. This implementation is those calls and
// nothing else: it holds no state, creates, writes, renames and removes
// nothing, and lives for the whole program.
//
// It is compiled on the desktop as well, where MinGW supplies the same
// calls, so that nfsu_asset_tests can run it over real directories before
// a console ever does. There it takes narrow paths in the active code page
// and so is only asked about ASCII relative paths; the desktop programs use
// desktop_file_system() (include/nfsu/arg_path.h), which takes wide paths.
// docs/PLATFORM_SERVICES.md and docs/SWITCH_ASSETS.md.

namespace nfsu {

class FileSystemService;

// argument_to_path stores an argument's bytes unchanged and refuses only a
// null one, as POSIX needs; the three read-only calls are described in
// include/nfsu/platform.h.
FileSystemService& posix_file_system();

} // namespace nfsu
