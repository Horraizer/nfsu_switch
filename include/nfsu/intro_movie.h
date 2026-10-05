#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nfsu {
struct MovieFrame {
    unsigned width = 0, height = 0;
    // Frontend object geometry, independent of encoded sample dimensions.
    // Both zero means square pixels for callers without display metadata.
    unsigned display_width = 0, display_height = 0;
    // Native MAD duration units: 65535 units per millisecond. Keep the
    // fractional word and accumulate before conversion. Integer ms are
    // diagnostic only; scheduling uses the exact units below.
    std::uint32_t duration_units = 0;
    std::uint64_t timestamp_units = 0;
    std::uint32_t duration_ms = 0;
    std::uint64_t timestamp_ms = 0;
    std::vector<std::uint8_t> yuv;
    std::vector<std::uint8_t> rgba;
};
// Each reader uses independent video/audio file cursors. Storage is bounded by
// two YUV reference frames, one RGBA frame and one audio chunk, not movie length.
class MovieReader {
public:
    explicit MovieReader(const std::string& path);
    ~MovieReader();
    MovieReader(const MovieReader&) = delete;
    MovieReader& operator=(const MovieReader&) = delete;
    bool next_video(MovieFrame& frame);
    bool next_audio(std::vector<std::int16_t>& stereo);
    unsigned audio_rate() const noexcept;
    std::uint64_t audio_frames() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
enum class IntroControl { continue_playback, skip, quit };
enum class IntroOutcome { completed, skipped, quit, error };
struct IntroResult {
    IntroOutcome outcome = IntroOutcome::completed;
    std::string error;
    std::size_t movies_completed = 0;
};
using IntroPoll = std::function<IntroControl()>;
using IntroPresent = std::function<void(const MovieFrame&)>;
IntroResult play_original_intro(const std::string& retail_root,
                               const IntroPoll& poll, const IntroPresent& present);
// Fit the frontend display aspect inside the destination, sample the encoded
// image into that rectangle and paint unused pixels opaque black.
void blit_movie(const MovieFrame& frame, std::uint8_t* destination,
                unsigned width, unsigned height, std::size_t stride);
} // namespace nfsu
