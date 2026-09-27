#include "nfsu/audio.h"

#include <cstdint>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace audio = nfsu::audio;
using Bytes = std::vector<std::uint8_t>;

namespace {

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

void rejected(const std::function<void()>& action, const char* reason) {
    try { action(); }
    catch (const std::runtime_error& e) {
        check(std::string(e.what()).find(reason) != std::string::npos,
              "wrong refusal diagnostic");
        return;
    }
    throw std::runtime_error("malformed audio accepted");
}

void set16(Bytes& b, std::size_t off, std::uint16_t value) {
    b.at(off) = static_cast<std::uint8_t>(value);
    b.at(off + 1) = static_cast<std::uint8_t>(value >> 8);
}

void set32(Bytes& b, std::size_t off, std::uint32_t value) {
    set16(b, off, static_cast<std::uint16_t>(value));
    set16(b, off + 2, static_cast<std::uint16_t>(value >> 16));
}

void append32(Bytes& b, std::uint32_t value) {
    b.push_back(static_cast<std::uint8_t>(value));
    b.push_back(static_cast<std::uint8_t>(value >> 8));
    b.push_back(static_cast<std::uint8_t>(value >> 16));
    b.push_back(static_cast<std::uint8_t>(value >> 24));
}

Bytes bank(std::uint8_t codec) {
    Bytes b(0x200);
    b[0] = 'A'; b[1] = 'B'; b[2] = 'K'; b[3] = 'C';
    set16(b, 0x0A, 1);
    set32(b, 0x1C, 0x5C);
    set32(b, 0x20, 0x100);
    b[0x5C + 0x24] = 1;
    set32(b, 0x5C + 0x2C, 0xA0);
    set32(b, 0xA4, 0xC0);
    set32(b, 0xC0, 1);
    set32(b, 0xC8, 1);
    b[0x100] = 'B'; b[0x101] = 'N'; b[0x102] = 'K'; b[0x103] = 'l';
    b[0x104] = 5;
    set16(b, 0x106, 2);
    set32(b, 0x118, 8);
    const Bytes header = {
        'P','T',0,0,
        0x80,1,2, 0x85,1,28, 0xA0,1,codec,
        0x84,2,0x5D,0xC0, 0x88,1,0x80,
        0x86,1,0, 0x87,1,27, 0xFF
    };
    for (std::size_t i = 0; i < header.size(); ++i) b[0x120 + i] = header[i];
    b[0x180] = 0;
    b[0x181] = 0x78;
    return b;
}

void chunk(Bytes& out, const char* tag, const Bytes& body) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(tag[i]));
    append32(out, static_cast<std::uint32_t>(body.size() + 8));
    out.insert(out.end(), body.begin(), body.end());
}

Bytes ast(std::uint8_t version, std::uint8_t channels,
          const Bytes& encoded, std::uint32_t frames) {
    Bytes out;
    Bytes header = {'P','T',0,0, 0x80,1,version, 0x82,1,channels,
                    0x84,2,0x5D,0xC0, 0x85,1,static_cast<std::uint8_t>(frames), 0xFF};
    chunk(out, "SCHl", header);
    Bytes count; append32(count, 1); chunk(out, "SCCl", count);
    Bytes body; append32(body, frames);
    for (std::uint8_t i = 0; i < channels; ++i)
        append32(body, static_cast<std::uint32_t>(i * encoded.size()));
    for (std::uint8_t i = 0; i < channels; ++i)
        body.insert(body.end(), encoded.begin(), encoded.end());
    chunk(out, "SCDl", body);
    chunk(out, "SCEl", {});
    out.resize((out.size() + 127U) & ~127U, 0);
    return out;
}

std::istringstream stream(const Bytes& bytes) {
    return std::istringstream(std::string(bytes.begin(), bytes.end()),
                              std::ios::in | std::ios::binary);
}

Bytes decode_segment(const Bytes& bytes) {
    auto input = stream(bytes);
    const auto segments = audio::scan_ast(input);
    check(segments.size() == 1, "wrong synthetic segment count");
    Bytes pcm;
    audio::decode_ast_segment(input, segments[0],
        [&](const std::uint8_t* b, std::size_t n) { pcm.insert(pcm.end(), b, b + n); });
    return pcm;
}

void test_bank() {
    auto b = bank(10);
    const auto parsed = audio::parse_abk(b);
    check(parsed.modules == 1 && parsed.entries.size() == 1 &&
          parsed.samples.size() == 1, "bank table mapping");
    const auto& sample = parsed.samples[0];
    check(sample.index == 1 && sample.rate == 24000 && sample.frames == 28 &&
          sample.loop_start == 0 && sample.loop_end == 28,
          "sample header and loop");
    Bytes pcm;
    audio::decode_bank_sample(b, sample,
        [&](const std::uint8_t* data, std::size_t size) {
            pcm.insert(pcm.end(), data, data + size);
        });
    check(pcm.size() == 56 && pcm[0] == 0 && pcm[1] == 0x70 &&
          pcm[2] == 0 && pcm[3] == 0x80, "ADPCM signed nibbles");
    b = bank(8);
    b[0x180] = 0x34; b[0x181] = 0x12;
    const auto pcm_sample = audio::parse_abk(b).samples[0];
    pcm.clear();
    audio::decode_bank_sample(b, pcm_sample,
        [&](const std::uint8_t* data, std::size_t size) {
            pcm.insert(pcm.end(), data, data + size);
        });
    check(pcm[0] == 0x34 && pcm[1] == 0x12 && pcm.size() == 56,
          "PCM16LE bank branch");
    rejected([&] { audio::parse_abk(Bytes(12)); }, "truncated");
    b = bank(10); set16(b, 0x106, 0xFFFF);
    rejected([&] { audio::parse_abk(b); }, "bad BNK count");
    b = bank(10); b[0x120 + 25] = 28;
    rejected([&] { audio::parse_abk(b); }, "loop outside sample");
    b = bank(10); b[0x120 + 24] = 4;
    for (std::size_t i = 25; i < 29; ++i) b[0x120 + i] = 0xFF;
    b[0x120 + 29] = 0xFF;
    rejected([&] { audio::parse_abk(b); }, "loop outside sample");
    b = bank(0x7F);
    rejected([&] { audio::parse_abk(b); }, "unknown codec");
    b = bank(10); b.resize(0x18E);
    const auto short_sample = audio::parse_abk(b).samples[0];
    rejected([&] { audio::decode_bank_sample(b, short_sample,
        [](const std::uint8_t*, std::size_t) {}); }, "truncated EA-XA ADPCM frame");
}

void test_stream() {
    Bytes encoded(15);
    encoded[0] = 0x0C; encoded[1] = 0x78;
    auto b = ast(1, 2, Bytes(4, 0), 28);
    // Replace the synthetic history and frame with two channel copies.
    Bytes v1(4, 0); v1.insert(v1.end(), encoded.begin(), encoded.end());
    b = ast(1, 2, v1, 28);
    auto pcm = decode_segment(b);
    check(pcm.size() == 112 && pcm[0] == 7 && pcm[2] == 7 &&
          pcm[4] == 0xF8 && pcm[5] == 0xFF, "EA-XA v1 stereo and shift");

    Bytes reset(61, 0);
    reset[0] = 0xEE; reset[1] = 0x75; reset[2] = 0x30;
    Bytes adpcm(15, 0); adpcm[0] = 0x10; adpcm[1] = 0x70;
    reset.insert(reset.end(), adpcm.begin(), adpcm.end());
    b = ast(2, 1, reset, 56);
    pcm = decode_segment(b);
    check(pcm.size() == 112 && pcm[56] == 0xFF && pcm[57] == 0x7F,
          "EA-XA v2 PCM reset, predictor and clamp");

    b = ast(2, 1, encoded, 28);
    auto input = stream(b);
    const auto segments = audio::scan_ast(input);
    check(segments[0].rate == 24000 && segments[0].blocks == 1 &&
          segments[0].frames == 28, "AST header and block totals");
    b = ast(2, 1, encoded, 28);
    // The count chunk starts after the variable header.
    const std::size_t count = 8 + 18 + 8;
    set32(b, count, 2);
    rejected([&] { auto s = stream(b); audio::scan_ast(s); }, "stream totals differ");
    b = ast(2, 1, encoded, 28);
    set32(b, 8 + 18 + 12 + 4, 0xFFFFFFFFU);
    rejected([&] { auto s = stream(b); audio::scan_ast(s); }, "bad AST chunk size");
    b = ast(2, 1, Bytes(14, 0), 28);
    b[8 + 18 + 12 + 8 + 8] = 0;
    rejected([&] { decode_segment(b); }, "truncated EA-XA ADPCM frame");
}

} // namespace

int main() {
    try {
        test_bank();
        test_stream();
        std::cout << "audio synthetic tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "audio synthetic tests: " << e.what() << '\n';
        return 1;
    }
}
