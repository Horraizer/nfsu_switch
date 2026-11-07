#pragma once

#include <cstdint>

namespace nfsu {
// GPU draw encoding is checked with the installed SDK capture API before
// first submission. Upload commands retain their independent mip allowance.
// These bounds preserve both fenced frame slots; they do not share storage.
constexpr std::uint64_t kDekoDrawCommandBytes = 1024;
constexpr std::uint64_t kDekoUploadCommandBytes = 4096;
constexpr std::uint64_t kDekoCommandFrameBytes = 65536;
constexpr std::uint64_t deko_command_memory(std::uint32_t draws,std::uint32_t uploads=0) {
    return kDekoCommandFrameBytes+std::uint64_t(draws)*kDekoDrawCommandBytes+
        std::uint64_t(uploads)*kDekoUploadCommandBytes;
}
} // namespace nfsu
