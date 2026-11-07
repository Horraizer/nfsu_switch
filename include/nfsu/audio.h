#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <vector>

namespace nfsu::audio {

enum class Codec : std::uint8_t { pcm16le = 8, ea_xa_v2 = 10, ea_xa_v1 = 11 };

struct Sample {
    std::uint32_t index = 0;
    std::uint32_t frames = 0;
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    std::uint32_t loop_start = 0;
    std::uint32_t loop_end = 0;
    std::uint32_t data_offset = 0;
    Codec codec = Codec::ea_xa_v2;
};

struct BankEntry {
    std::uint8_t type = 0;
    std::uint32_t value = 0;
    std::uint32_t loop_value = 0;
};

struct Bank {
    std::uint32_t modules = 0;
    std::vector<Sample> samples;
    std::vector<BankEntry> entries;
};

struct Segment {
    std::uint64_t offset = 0;
    std::uint64_t end = 0;
    std::uint32_t frames = 0;
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    std::uint32_t blocks = 0;
    Codec codec = Codec::ea_xa_v2;
};

using PcmSink = std::function<void(const std::uint8_t*, std::size_t)>;

// Exceptions contain the offending offset and reason. No game assets are retained
// by these results; the caller owns bank bytes and stream input.
Bank parse_abk(const std::vector<std::uint8_t>& bytes);
std::vector<Segment> scan_ast(std::istream& input,
                              std::size_t max_segments = SIZE_MAX,
                              const std::function<void()>& progress = {});
std::vector<Segment> scan_ast_prefix(std::istream& input,
                                     std::size_t byte_limit,
                                     std::size_t max_segments,
                                     const std::function<void()>& progress = {});
Segment index_ast_segment_by_boundary(std::istream& input,
                                      std::uint64_t offset,
                                      std::uint64_t next_offset);
void decode_bank_sample(const std::vector<std::uint8_t>& bytes,
                        const Sample& sample, const PcmSink& sink);
void decode_ast_segment(std::istream& input, const Segment& segment,
                        const PcmSink& sink);

} // namespace nfsu::audio
