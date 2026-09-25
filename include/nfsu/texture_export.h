#pragma once

#include "nfsu/texture.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace nfsu {

enum class ExportFormat { tga, dds };

struct ExportedTexture {
    std::size_t index = 0;
    std::filesystem::path path;
    std::size_t jdlz_unused_bytes = 0;
};

struct TextureExportResult {
    std::vector<ExportedTexture> files;
    std::vector<std::size_t> skipped;
    std::string error;
    // Files created by the failed call that are still present after the
    // rollback attempt, including leftover "<target>.partial" files.
    std::vector<std::filesystem::path> not_removed;
};

// Attempts to remove every path (none is skipped after a failure) and
// returns the paths that still exist afterwards, in input order. A path that
// cannot be checked counts as still existing. summary describes the result
// and never claims a complete removal unless the returned list is empty.
// Callers must pass only files they created themselves.
std::vector<std::filesystem::path> rollback_created_files(
    const std::vector<std::filesystem::path>& created,
    std::string& summary);

// Writes one file per valid texture into output_dir (created if missing):
// a TGA of the decoded top level, or a DDS header plus the decompressed
// payload of every level unchanged. Invalid textures are listed in skipped.
// Nothing is written if any target already exists or is the input file.
// Each file is written as "<target>.partial" and renamed when complete; a
// target that appears during the export is not replaced. On any failure,
// false is returned, files and the .partial file created by this call are
// passed to rollback_created_files, error states how many were removed, and
// not_removed lists the ones that could not be. Pre-existing files are
// never rollback targets.
bool export_textures(
    const std::vector<unsigned char>& bytes,
    const TexturePack& pack,
    const std::filesystem::path& input,
    const std::filesystem::path& output_dir,
    ExportFormat format,
    TextureExportResult& result);

} // namespace nfsu
