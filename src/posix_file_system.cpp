#include "nfsu/posix_file_system.h"

#include "nfsu/platform.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <new>
#include <string>
#include <vector>

// The console's FileSystemService; include/nfsu/posix_file_system.h says
// what it is for and include/nfsu/platform.h what each call promises. The
// reasons use the same words as the desktop service in src/arg_path.cpp,
// so that a report reads the same wherever it was made.

namespace nfsu {

namespace {

namespace fs = std::filesystem;

// The path as the narrow string the C library takes. On the console that is
// the path's own bytes. On the desktop, where this file is compiled only
// for its tests, it is the UTF-8 spelling, which the tests keep to ASCII.
bool narrow_path(const fs::path& path, std::string& out, std::string& reason) {
    try {
        out = path.string();
        return true;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception&) {
        reason = "cannot be spelled as a narrow path";
        return false;
    }
}

class PosixFileSystem final : public FileSystemService {
public:
    bool argument_to_path(const char* text, fs::path& out, std::string& reason) override {
        if (text == nullptr) {
            reason = "is missing";
            return false;
        }
        try {
            out = fs::path(std::string(text));
            return true;
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception&) {
            reason = "cannot be spelled as a path";
            return false;
        }
    }

    bool list_directory(const fs::path& directory, std::size_t max_names,
                        std::vector<std::string>& names, std::string& reason) override {
        names.clear();
        std::string native;
        if (!narrow_path(directory, native, reason)) {
            return false;
        }
        errno = 0;
        DIR* handle = opendir(native.c_str());
        if (handle == nullptr) {
            const int error = errno;
            if (error == ENOENT) {
                reason = "does not exist";
            } else if (error == ENOTDIR) {
                reason = "is not a directory";
            } else {
                reason = "cannot be listed (errno " + std::to_string(error) + ")";
            }
            return false;
        }
        bool failed = false;
        for (;;) {
            errno = 0;
            const dirent* entry = readdir(handle);
            if (entry == nullptr) {
                // The end of the listing. POSIX leaves errno alone there, and
                // devkitPro's newlib does too: its readdir_r turns the device
                // reader's ENOENT into the end with errno set back to 0 (read
                // from the linked code, docs/SWITCH_ASSETS.md). ENOENT is
                // taken as the end as well, in case a reader passes it on, so
                // only another value is a failure.
                const int error = errno;
                if (error != 0 && error != ENOENT) {
                    reason = "cannot be listed (errno " + std::to_string(error) + ")";
                    failed = true;
                }
                break;
            }
            const std::string name(entry->d_name);
            if (name == "." || name == "..") {
                continue;
            }
            if (names.size() == max_names) {
                reason = "holds more than " + std::to_string(max_names) + " names";
                failed = true;
                break;
            }
            names.push_back(name);
        }
        closedir(handle);
        if (failed) {
            names.clear();
            return false;
        }
        return true;
    }

    bool file_size(const fs::path& path, std::uint64_t& size, std::string& reason) override {
        std::string native;
        if (!narrow_path(path, native, reason)) {
            return false;
        }
        struct stat info {};
        errno = 0;
        if (stat(native.c_str(), &info) != 0) {
            const int error = errno;
            if (error == ENOENT || error == ENOTDIR) {
                reason = "does not exist";
            } else {
                reason = "cannot be sized (errno " + std::to_string(error) + ")";
            }
            return false;
        }
        if (S_ISDIR(info.st_mode)) {
            reason = "is a directory, not a file";
            return false;
        }
        if (!S_ISREG(info.st_mode)) {
            reason = "is not a regular file";
            return false;
        }
        if (info.st_size < 0) {
            reason = "cannot be sized (a negative size)";
            return false;
        }
        size = static_cast<std::uint64_t>(info.st_size);
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

FileSystemService& posix_file_system() {
    static PosixFileSystem files;
    return files;
}

} // namespace nfsu
