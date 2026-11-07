#include "nfsu/texture_export.h"

#include "nfsu/image.h"
#include "nfsu/texture.h"

#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace nfsu {
namespace {

namespace fs = std::filesystem;

bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code error;
    if (fs::equivalent(a, b, error) && !error) {
        return true;
    }
    const fs::path abs_a = fs::absolute(a, error).lexically_normal();
    if (error) {
        return false;
    }
    const fs::path abs_b = fs::absolute(b, error).lexically_normal();
    return !error && abs_a == abs_b;
}

// True unless the path is known not to exist; paths that cannot be checked
// count as existing so that they are never overwritten.
bool path_exists(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    return status.type() != fs::file_type::not_found;
}

// Removes a file this export created; returns false if it is still there.
bool remove_created(const fs::path& path) {
    std::error_code error;
    fs::remove(path, error);
    return !path_exists(path);
}

// On failure, leftover is set to a .partial file this call created and could
// not remove.
bool write_one(
    const std::vector<unsigned char>& bytes,
    const TextureRecord& texture,
    const fs::path& target,
    ExportFormat format,
    ExportedTexture& exported,
    std::optional<fs::path>& leftover,
    std::string& error) {
    std::vector<unsigned char> payload;
    std::size_t unused = 0;
    P8Palette palette{};
    P8Palette* selected_palette = texture.format == PixelFormat::p8 ? &palette : nullptr;
    if (!texture_payload(bytes.data(), bytes.size(), texture, payload, unused,
                         error, selected_palette)) {
        return false;
    }

    fs::path partial = target;
    partial += ".partial";
    if (path_exists(partial)) {
        error = "temporary file already exists: " + partial.filename().string();
        return false;
    }
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "failed to open " + partial.filename().string();
        return false;
    }

    bool written = false;
    if (format == ExportFormat::dds) {
        written = write_dds(output, texture.format, texture.width,
                            texture.height, texture.mip_count, payload.data(),
                            payload.size(), error);
    } else {
        std::uint64_t top = 0;
        std::vector<unsigned char> rgba;
        written = level_size(texture.format, texture.width, texture.height, top) &&
                  top <= payload.size() &&
                  decode_level(texture.format, texture.width, texture.height,
                               payload.data(), static_cast<std::size_t>(top),
                               rgba, error, selected_palette) &&
                  write_tga(output, texture.width, texture.height, rgba, error);
    }
    output.close();
    if (!written || !output) {
        if (error.empty()) {
            error = "failed to write " + partial.filename().string();
        }
        if (!remove_created(partial)) {
            leftover = partial;
        }
        return false;
    }

    // rename replaces an existing target on some platforms, so re-check just
    // before it; a file that appeared since planning is left untouched.
    if (path_exists(target)) {
        error = "target appeared during export, not replaced: " +
                target.filename().string();
        if (!remove_created(partial)) {
            leftover = partial;
        }
        return false;
    }
    std::error_code rename_error;
    fs::rename(partial, target, rename_error);
    if (rename_error) {
        error = "failed to rename " + partial.filename().string();
        if (!remove_created(partial)) {
            leftover = partial;
        }
        return false;
    }
    exported.index = texture.index;
    exported.path = target;
    exported.jdlz_unused_bytes = unused;
    return true;
}

} // namespace

std::vector<std::filesystem::path> rollback_created_files(
    const std::vector<std::filesystem::path>& created,
    std::string& summary) {
    std::vector<fs::path> failed;
    for (const fs::path& path : created) {
        if (!remove_created(path)) {
            failed.push_back(path);
        }
    }
    const std::size_t removed = created.size() - failed.size();
    if (failed.empty()) {
        summary = "removed all " + std::to_string(created.size()) +
                  " files created by this export";
    } else {
        summary = "removed " + std::to_string(removed) + " of " +
                  std::to_string(created.size()) +
                  " files created by this export; could not remove:";
        for (const fs::path& path : failed) {
            summary += " " + path.u8string();
        }
    }
    return failed;
}

bool export_textures(
    const std::vector<unsigned char>& bytes,
    const TexturePack& pack,
    const std::filesystem::path& input,
    const std::filesystem::path& output_dir,
    ExportFormat format,
    TextureExportResult& result) {
    result = TextureExportResult{};
    if (same_file(input, output_dir)) {
        result.error = "output directory must differ from the input file";
        return false;
    }

    // Plan every target first so that nothing is written if any name
    // collides with an existing file or with the input.
    const char* extension = format == ExportFormat::dds ? ".dds" : ".tga";
    std::vector<std::pair<const TextureRecord*, fs::path>> plan;
    for (const TextureRecord& texture : pack.textures) {
        if (!texture.valid) {
            result.skipped.push_back(texture.index);
            continue;
        }
        const fs::path target =
            output_dir / (texture_file_stem(texture) + extension);
        if (path_exists(target) || same_file(input, target)) {
            result.error = "refusing to overwrite " + target.filename().string();
            return false;
        }
        for (const auto& planned : plan) {
            if (planned.second == target) {
                result.error = "duplicate output name " +
                               target.filename().string();
                return false;
            }
        }
        plan.emplace_back(&texture, target);
    }

    std::error_code error;
    fs::create_directories(output_dir, error);
    if (error || !fs::is_directory(output_dir, error)) {
        result.error = "output path is not a usable directory";
        return false;
    }

    for (const auto& [texture, target] : plan) {
        ExportedTexture exported;
        std::optional<fs::path> leftover;
        std::string message;
        if (!write_one(bytes, *texture, target, format, exported, leftover,
                       message)) {
            // Only files created by this call: completed targets and a
            // .partial that write_one could not remove.
            std::vector<fs::path> created;
            for (const ExportedTexture& file : result.files) {
                created.push_back(file.path);
            }
            if (leftover) {
                created.push_back(*leftover);
            }
            std::string summary;
            result.not_removed = rollback_created_files(created, summary);
            result.error = "texture " + std::to_string(texture->index) + ": " +
                           message + "; " + summary;
            result.files.clear();
            return false;
        }
        result.files.push_back(std::move(exported));
    }
    return true;
}

} // namespace nfsu
