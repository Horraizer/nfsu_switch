#include "nfsu/audio.h"

#include <algorithm>
#include <array>
#include <istream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace nfsu::audio {
namespace {

using Byte = std::uint8_t;
constexpr std::uint32_t kMaxChunk = 1U << 20;

[[noreturn]] void fail(std::uint64_t offset, const char* reason) {
    throw std::runtime_error("audio at " + std::to_string(offset) + ": " + reason);
}

void require_range(std::size_t size, std::uint64_t off, std::uint64_t count) {
    if (off > size || count > size - off) fail(off, "truncated input");
}

std::uint16_t u16(const std::vector<Byte>& b, std::uint64_t p) {
    require_range(b.size(), p, 2);
    return static_cast<std::uint16_t>(b[static_cast<std::size_t>(p)] |
        (static_cast<std::uint16_t>(b[static_cast<std::size_t>(p + 1)]) << 8));
}

std::uint32_t u32(const std::vector<Byte>& b, std::uint64_t p) {
    require_range(b.size(), p, 4);
    return static_cast<std::uint32_t>(b[static_cast<std::size_t>(p)]) |
        (static_cast<std::uint32_t>(b[static_cast<std::size_t>(p + 1)]) << 8) |
        (static_cast<std::uint32_t>(b[static_cast<std::size_t>(p + 2)]) << 16) |
        (static_cast<std::uint32_t>(b[static_cast<std::size_t>(p + 3)]) << 24);
}

struct Patches {
    std::array<std::uint32_t, 256> value{};
    std::array<bool, 256> present{};
    std::uint32_t get(Byte tag, std::uint32_t fallback = 0) const {
        return present[tag] ? value[tag] : fallback;
    }
};

Patches parse_patches(const std::vector<Byte>& b, std::uint64_t begin,
                      std::uint64_t limit) {
    require_range(b.size(), begin, 4);
    if (b[static_cast<std::size_t>(begin)] != 'P' ||
        b[static_cast<std::size_t>(begin + 1)] != 'T') fail(begin, "missing PT header");
    std::uint64_t pos = begin + 4;
    Patches result;
    while (pos < limit) {
        require_range(b.size(), pos, 1);
        const Byte tag = b[static_cast<std::size_t>(pos++)];
        if (tag == 0xFF || tag == 0xFE) return result;
        if (tag == 0xFC || tag == 0xFD) continue;
        require_range(b.size(), pos, 1);
        std::uint64_t length = b[static_cast<std::size_t>(pos++)];
        if (length == 0xFF) {
            require_range(b.size(), pos, 4);
            length = (static_cast<std::uint32_t>(b[static_cast<std::size_t>(pos)]) << 24) |
                (static_cast<std::uint32_t>(b[static_cast<std::size_t>(pos + 1)]) << 16) |
                (static_cast<std::uint32_t>(b[static_cast<std::size_t>(pos + 2)]) << 8) |
                b[static_cast<std::size_t>(pos + 3)];
            pos += 4;
        }
        if (pos > limit || length > limit - pos) fail(pos, "oversized header patch");
        require_range(b.size(), pos, length);
        if (length <= 4) {
            std::uint32_t value = 0;
            for (std::uint64_t i = 0; i < length; ++i)
                value = (value << 8) | b[static_cast<std::size_t>(pos + i)];
            result.present[tag] = true;
            result.value[tag] = value;
        }
        pos += length;
    }
    fail(pos, "header lacks end marker");
}

Codec codec_of(const Patches& p, std::uint64_t off) {
    const auto code = p.get(0xA0, 0x0A);
    if (code == 8) return Codec::pcm16le;
    if (code == 10) return p.get(0x80, 0) >= 2 ? Codec::ea_xa_v2 : Codec::ea_xa_v1;
    fail(off, "unknown codec");
}

void check_audio_header(std::uint32_t frames, std::uint32_t rate,
                        std::uint32_t channels, std::uint64_t off) {
    if (!frames || frames > 100000000U) fail(off, "bad sample count");
    if (rate < 8000 || rate > 192000) fail(off, "bad sample rate");
    if (channels < 1 || channels > 2) fail(off, "bad channel count");
}

std::uint64_t input_size(std::istream& input) {
    input.clear();
    input.seekg(0, std::ios::end);
    const auto pos = input.tellg();
    if (pos < 0) fail(0, "cannot determine stream size");
    return static_cast<std::uint64_t>(pos);
}

std::vector<Byte> read_at(std::istream& input, std::uint64_t off,
                          std::size_t count, std::uint64_t size) {
    if (off > size || count > size - off ||
        off > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        fail(off, "truncated input");
    std::vector<Byte> bytes(count);
    input.clear();
    input.seekg(static_cast<std::streamoff>(off));
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data()),
                              static_cast<std::streamsize>(count)))
        fail(off, "stream read failed");
    return bytes;
}

std::uint32_t block_tag(const std::vector<Byte>& b) {
    return (static_cast<std::uint32_t>(b[0]) << 24) |
        (static_cast<std::uint32_t>(b[1]) << 16) |
        (static_cast<std::uint32_t>(b[2]) << 8) | b[3];
}

constexpr std::uint32_t kSCHl = 0x5343486CU;
constexpr std::uint32_t kSCCl = 0x5343436CU;
constexpr std::uint32_t kSCDl = 0x5343446CU;
constexpr std::uint32_t kSCEl = 0x5343456CU;

void append_sample(std::vector<Byte>& pcm, std::int32_t sample) {
    sample = std::max(-32768, std::min(32767, sample));
    pcm.push_back(static_cast<Byte>(sample & 0xFF));
    pcm.push_back(static_cast<Byte>((sample >> 8) & 0xFF));
}

// C++ division truncates toward zero; EA's signed right shift floors instead.
std::int32_t floor_256(std::int32_t x) {
    return x >= 0 ? x / 256 : -static_cast<std::int32_t>((-static_cast<std::int64_t>(x) + 255) / 256);
}

std::int16_t be_s16(const Byte* b) {
    return static_cast<std::int16_t>((static_cast<std::uint16_t>(b[0]) << 8) | b[1]);
}

// EA-XA v2 frame rules are identified by the 0xEE PCM reset frames and
// checked against vgmstream's ea_xa_decoder.c. The coefficients are codec
// constants, not bytes copied from the game.
struct XaState { std::int32_t h1 = 0, h2 = 0; };

std::vector<Byte> decode_xa(const Byte* data, std::size_t available,
                            std::uint32_t frames, std::uint64_t off,
                            Codec codec, XaState& state) {
    constexpr std::int32_t c1[4] = {0, 240, 460, 392};
    constexpr std::int32_t c2[4] = {0, 0, -208, -220};
    std::vector<Byte> pcm;
    pcm.reserve(static_cast<std::size_t>(frames) * 2);
    std::size_t pos = 0;
    std::uint32_t done = 0;
    while (done < frames) {
        if (pos >= available) fail(off + pos, "truncated EA-XA frame");
        const Byte control = data[pos];
        const std::uint32_t take = std::min(28U, frames - done);
        if (control == 0xEE && codec == Codec::ea_xa_v2) {
            if (available - pos < 61) fail(off + pos, "truncated EA-XA PCM frame");
            state.h1 = be_s16(data + pos + 1);
            state.h2 = be_s16(data + pos + 3);
            for (std::uint32_t i = 0; i < take; ++i)
                append_sample(pcm, be_s16(data + pos + 5 + i * 2));
            pos += 61;
        } else {
            if (available - pos < 15) fail(off + pos, "truncated EA-XA ADPCM frame");
            const Byte predictor = control >> 4;
            if (predictor > 3) fail(off + pos, "bad EA-XA predictor");
            const Byte shift = control & 15;
            for (std::uint32_t i = 0; i < take; ++i) {
                const Byte packed = data[pos + 1 + i / 2];
                const Byte raw = (i & 1U) ? (packed & 15U) : (packed >> 4);
                const std::int32_t nibble = raw < 8 ? raw : static_cast<std::int32_t>(raw) - 16;
                const std::int32_t base = nibble * (1 << (20 - shift));
                const std::int32_t rounding = codec == Codec::ea_xa_v1 ? 128 : 0;
                const std::int32_t mixed = floor_256(base + c1[predictor] * state.h1 +
                    c2[predictor] * state.h2 + rounding);
                state.h2 = state.h1;
                state.h1 = std::max(-32768, std::min(32767, mixed));
                append_sample(pcm, state.h1);
            }
            pos += 15;
        }
        done += take;
    }
    return pcm;
}

} // namespace

Bank parse_abk(const std::vector<Byte>& b) {
    require_range(b.size(), 0, 0x24);
    if (b[0] != 'A' || b[1] != 'B' || b[2] != 'K' || b[3] != 'C')
        fail(0, "missing ABKC magic");
    Bank bank;
    bank.modules = u16(b, 0x0A);
    if (!bank.modules || bank.modules > 1024) fail(0x0A, "bad module count");
    std::uint64_t module = u32(b, 0x1C);
    const std::uint64_t bnk = u32(b, 0x20);
    std::vector<std::uint32_t> seen_tables;
    for (std::uint32_t i = 0; i < bank.modules; ++i) {
        require_range(b.size(), module, 0x3C);
        const std::uint32_t players = b[static_cast<std::size_t>(module + 0x24)];
        const std::uint32_t controllers = b[static_cast<std::size_t>(module + 0x27)];
        if (players > 128 || controllers > 128) fail(module, "bad player count");
        const std::uint64_t data = u32(b, module + 0x2C);
        require_range(b.size(), module + 0x3C, players * 4U);
        for (std::uint32_t j = 0; j < players; ++j) {
            const std::uint64_t player = data + u32(b, module + 0x3C + j * 4U);
            const std::uint32_t table = u32(b, player + 4);
            if (std::find(seen_tables.begin(), seen_tables.end(), table) != seen_tables.end())
                continue;
            seen_tables.push_back(table);
            const std::uint32_t count = u32(b, table);
            if (count > 100000 || count > (b.size() - table - 4) / 12)
                fail(table, "bad entry count");
            for (std::uint32_t k = 0; k < count; ++k) {
                const std::uint64_t off = static_cast<std::uint64_t>(table) + 4 + k * 12ULL;
                const Byte type = b[static_cast<std::size_t>(off)];
                if (type > 2) fail(off, "unknown bank entry type");
                const BankEntry entry{type, u32(b, off + 4), u32(b, off + 8)};
                bank.entries.push_back(entry);
            }
        }
        module += 0x3C + 4U * (players + controllers);
    }
    if (bnk) {
        require_range(b.size(), bnk, 0x14);
        if (b[static_cast<std::size_t>(bnk)] != 'B' ||
            b[static_cast<std::size_t>(bnk + 1)] != 'N' ||
            b[static_cast<std::size_t>(bnk + 2)] != 'K' ||
            b[static_cast<std::size_t>(bnk + 3)] != 'l' ||
            b[static_cast<std::size_t>(bnk + 4)] != 5)
            fail(bnk, "unsupported BNK header");
        const std::uint32_t count = u16(b, bnk + 6);
        if (count > 10000) fail(bnk + 6, "bad BNK count");
        require_range(b.size(), bnk + 0x14, count * 4ULL);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint64_t entry = bnk + 0x14 + i * 4ULL;
            const std::uint32_t rel = u32(b, entry);
            if (!rel) continue;
            const std::uint64_t head = entry + rel;
            const auto p = parse_patches(b, head, std::min<std::uint64_t>(b.size(), head + 512));
            Sample s;
            s.index = i;
            s.frames = p.get(0x85);
            s.rate = p.get(0x84, 22050);
            s.channels = p.get(0x82, 1);
            s.codec = codec_of(p, head);
            s.loop_start = p.get(0x86);
            if (p.present[0x87] && p.value[0x87] ==
                    std::numeric_limits<std::uint32_t>::max())
                fail(head, "loop outside sample");
            s.loop_end = p.present[0x87] ? p.value[0x87] + 1 : 0;
            s.data_offset = static_cast<std::uint32_t>(bnk + p.get(0x88));
            check_audio_header(s.frames, s.rate, s.channels, head);
            if (s.loop_end && (s.loop_end > s.frames || s.loop_start >= s.loop_end))
                fail(head, "loop outside sample");
            require_range(b.size(), s.data_offset, 1);
            bank.samples.push_back(s);
        }
    }
    for (const auto& entry : bank.entries) {
        if (entry.type != 0 || entry.value == 0) continue;
        if (std::none_of(bank.samples.begin(), bank.samples.end(),
            [&](const Sample& s) { return s.index == entry.value; }))
            fail(entry.value, "bank entry references missing sample");
    }
    return bank;
}

std::vector<Segment> scan_ast(std::istream& input) {
    const std::uint64_t size = input_size(input);
    std::vector<Segment> result;
    std::uint64_t pos = 0;
    Segment current;
    bool active = false, counted = false;
    std::uint32_t expected_blocks = 0;
    std::uint64_t decoded_frames = 0;
    while (pos < size) {
        const auto first = read_at(input, pos, 1, size)[0];
        if (first == 0) {
            if (active) fail(pos, "padding inside stream segment");
            const std::uint64_t aligned = std::min<std::uint64_t>(
                size, (pos + 127U) & ~127ULL);
            const auto padding = read_at(input, pos, static_cast<std::size_t>(aligned - pos), size);
            if (std::any_of(padding.begin(), padding.end(), [](Byte b) { return b != 0; }))
                fail(pos, "nonzero AST padding");
            pos = aligned;
            continue;
        }
        const auto header = read_at(input, pos, 8, size);
        const auto tag = block_tag(header);
        const std::uint32_t n = u32(header, 4);
        if (n < 8 || n > kMaxChunk || n > size - pos) fail(pos, "bad AST chunk size");
        if (tag == kSCHl) {
            if (active || n > 512) fail(pos, "bad SCHl placement or size");
            const auto bytes = read_at(input, pos, n, size);
            const auto p = parse_patches(bytes, 8, n);
            current = {};
            current.offset = pos;
            current.frames = p.get(0x85);
            current.rate = p.get(0x84, 22050);
            current.channels = p.get(0x82, 1);
            current.codec = codec_of(p, pos);
            check_audio_header(current.frames, current.rate, current.channels, pos);
            expected_blocks = 0;
            decoded_frames = 0;
            active = true;
            counted = false;
        } else if (tag == kSCCl) {
            if (!active || counted || n != 12) fail(pos, "bad SCCl placement or size");
            expected_blocks = u32(read_at(input, pos, n, size), 8);
            if (!expected_blocks || expected_blocks > 10000000U)
                fail(pos, "bad block count");
            counted = true;
        } else if (tag == kSCDl) {
            if (!active || !counted || n < 16) fail(pos, "bad SCDl placement or size");
            const auto h = read_at(input, pos, 12, size);
            const std::uint32_t frames = u32(h, 8);
            if (!frames || frames > 1000000U) fail(pos, "bad block sample count");
            decoded_frames += frames;
            ++current.blocks;
            if (decoded_frames > current.frames || current.blocks > expected_blocks)
                fail(pos, "block totals exceed header");
        } else if (tag == kSCEl) {
            if (!active || !counted || decoded_frames != current.frames ||
                current.blocks != expected_blocks)
                fail(pos, "stream totals differ from header");
            current.end = pos + n;
            result.push_back(current);
            active = false;
        } else {
            fail(pos, "unknown AST chunk");
        }
        pos += n;
    }
    if (active || result.empty()) fail(pos, "unfinished AST segment");
    return result;
}

void decode_bank_sample(const std::vector<Byte>& bytes, const Sample& sample,
                        const PcmSink& sink) {
    if (sample.channels != 1) fail(sample.data_offset, "unsupported bank channel count");
    const std::uint64_t count = static_cast<std::uint64_t>(sample.frames) * 2;
    if (sample.codec == Codec::pcm16le) {
        require_range(bytes.size(), sample.data_offset, count);
        sink(bytes.data() + sample.data_offset, static_cast<std::size_t>(count));
    } else {
        require_range(bytes.size(), sample.data_offset, 1);
        XaState state;
        const auto pcm = decode_xa(bytes.data() + sample.data_offset,
            bytes.size() - sample.data_offset, sample.frames, sample.data_offset,
            sample.codec, state);
        sink(pcm.data(), pcm.size());
    }
}

void decode_ast_segment(std::istream& input, const Segment& segment,
                        const PcmSink& sink) {
    const std::uint64_t size = input_size(input);
    if (segment.end > size || segment.offset >= segment.end)
        fail(segment.offset, "segment outside AST");
    std::uint64_t pos = segment.offset;
    std::uint64_t total = 0;
    std::array<XaState, 2> states{};
    while (pos < segment.end) {
        const auto h = read_at(input, pos, 8, size);
        const auto tag = block_tag(h);
        const std::uint32_t n = u32(h, 4);
        if (n < 8 || n > kMaxChunk || n > segment.end - pos)
            fail(pos, "bad segment chunk size");
        if (tag == kSCDl) {
            const auto block = read_at(input, pos, n, size);
            const auto frames = u32(block, 8);
            if (!frames || frames > 1000000U || total + frames > segment.frames)
                fail(pos, "bad block frame count");
            const std::uint64_t base = 12 + 4ULL * segment.channels;
            if (base > n) fail(pos, "truncated channel offsets");
            std::array<std::uint32_t, 2> starts{};
            for (std::uint32_t ch = 0; ch < segment.channels; ++ch) {
                starts[ch] = u32(block, 12 + 4U * ch);
                if (starts[ch] > n - base || (ch && starts[ch] < starts[ch - 1]))
                    fail(pos, "bad channel offset");
            }
            std::array<std::vector<Byte>, 2> channels;
            for (std::uint32_t ch = 0; ch < segment.channels; ++ch) {
                const std::uint64_t start = base + starts[ch];
                const std::uint64_t end = ch + 1 < segment.channels ?
                    base + starts[ch + 1] : n;
                if (end < start) fail(pos, "overlapping channels");
                if (segment.codec == Codec::pcm16le) {
                    if (static_cast<std::uint64_t>(frames) * 2 > end - start)
                        fail(pos, "truncated PCM block");
                    channels[ch].assign(block.begin() + static_cast<std::ptrdiff_t>(start),
                        block.begin() + static_cast<std::ptrdiff_t>(start + frames * 2ULL));
                } else {
                    const std::uint64_t skip = segment.codec == Codec::ea_xa_v1 ? 4 : 0;
                    if (end - start < skip) fail(pos + start, "truncated EA-XA history");
                    channels[ch] = decode_xa(block.data() + start + skip,
                        static_cast<std::size_t>(end - start - skip), frames,
                        pos + start + skip, segment.codec, states[ch]);
                }
            }
            std::vector<Byte> pcm(static_cast<std::size_t>(frames) * segment.channels * 2);
            for (std::uint32_t frame = 0; frame < frames; ++frame)
                for (std::uint32_t ch = 0; ch < segment.channels; ++ch) {
                    const std::size_t src = static_cast<std::size_t>(frame) * 2;
                    const std::size_t dst = (static_cast<std::size_t>(frame) * segment.channels + ch) * 2;
                    pcm[dst] = channels[ch][src];
                    pcm[dst + 1] = channels[ch][src + 1];
                }
            sink(pcm.data(), pcm.size());
            total += frames;
        } else if (tag != kSCHl && tag != kSCCl && tag != kSCEl) {
            fail(pos, "unexpected segment chunk");
        }
        pos += n;
    }
    if (total != segment.frames) fail(pos, "decoded frame total differs from header");
}

} // namespace nfsu::audio
