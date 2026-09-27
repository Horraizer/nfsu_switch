#include "nfsu/audio_playback.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace audio = nfsu::audio;
using Bytes = std::vector<std::uint8_t>;
namespace {

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

void u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void chunk(Bytes& bytes, const char* tag, const Bytes& body) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(tag[i]));
    u32(bytes, static_cast<std::uint32_t>(body.size() + 8));
    bytes.insert(bytes.end(), body.begin(), body.end());
}

void pcm16(Bytes& bytes, std::int16_t sample) {
    bytes.push_back(static_cast<std::uint8_t>(sample));
    bytes.push_back(static_cast<std::uint8_t>(sample >> 8));
}

Bytes pcm_ast() {
    Bytes bytes;
    const Bytes header = {'P','T',0,0, 0x80,1,2, 0x82,1,2,
        0x84,2,0x5D,0xC0, 0x85,1,4, 0xA0,1,8, 0xFF};
    chunk(bytes, "SCHl", header);
    Bytes count; u32(count, 2); chunk(bytes, "SCCl", count);
    for (const auto pair : {std::array<std::int16_t,2>{1000,2000},
                            std::array<std::int16_t,2>{3000,4000}}) {
        Bytes body; u32(body, 2); u32(body, 0); u32(body, 4);
        for (int channel = 0; channel < 2; ++channel)
            for (auto sample : pair) pcm16(body, sample);
        chunk(bytes, "SCDl", body);
    }
    chunk(bytes, "SCEl", {});
    return bytes;
}

std::unique_ptr<std::istream> input_of(const Bytes& bytes) {
    return std::make_unique<std::istringstream>(
        std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::binary);
}

void await_size(const audio::StereoRing& ring, std::size_t wanted) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ring.size() < wanted && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(ring.size() >= wanted, "AST decoder did not fill ring");
}

void test_mixer() {
    audio::Mixer mixer;
    audio::PcmClip constant{{10000}, 48000, 1};
    check(mixer.play(0, &constant, true, 16384), "start mono voice");
    std::array<std::int16_t, 10> out{};
    mixer.render(out.data(), 5);
    for (auto sample : out) check(sample == 5000, "mono level or loop");
    mixer.control(0, 32768, 65536);
    mixer.render(out.data(), 1);
    check(out[0] == 10000 && out[1] == 10000, "voice gain control");
    mixer.stop(0);
    mixer.render(out.data(), 1);
    check(out[0] == 0 && out[1] == 0, "voice stop");

    audio::PcmClip loud{{30000}, 48000, 1};
    check(mixer.play(0, &loud, true) && mixer.play(1, &loud, true),
          "start two voices");
    mixer.render(out.data(), 1);
    check(out[0] == 32767 && out[1] == 32767, "positive mix clipping");
    const std::array<std::int16_t,2> opposite{-30000,-30000};
    mixer.render(out.data(), 1, opposite.data(), 32768);
    check(out[0] == 30000 && out[1] == 30000,
          "stream is summed before final clipping");
    mixer.stop(0); mixer.stop(1);
    audio::PcmClip negative{{-30000}, 48000, 1};
    check(mixer.play(0, &negative, true) && mixer.play(1, &negative, true),
          "start negative voices");
    mixer.render(out.data(), 1);
    check(out[0] == -32768 && out[1] == -32768, "negative mix clipping");
    mixer.stop(0); mixer.stop(1);

    audio::PcmClip loop{{1000,2000,3000}, 48000, 1, 1, 3};
    check(mixer.play(0, &loop, true), "start looped voice");
    mixer.render(out.data(), 5);
    check(out == std::array<std::int16_t,10>{1000,1000,2000,2000,3000,3000,
                                             2000,2000,3000,3000},
          "loop begin and exclusive end");
    mixer.stop(0);
    check(mixer.play(0, &loop, true, 32768, 131072), "start pitched voice");
    mixer.render(out.data(), 3);
    check(out[0] == 1000 && out[2] == 3000 && out[4] == 3000,
          "two-times pitch crosses loop boundary");
    mixer.stop(0);
    audio::PcmClip half_rate{{1000,2000}, 24000, 1};
    check(mixer.play(0, &half_rate, false), "start resampled voice");
    mixer.render(out.data(), 5);
    check(out[0] == 1000 && out[2] == 1000 && out[4] == 2000 &&
          out[6] == 2000 && out[8] == 0 && !mixer.active(0),
          "resample and non-looping end");
}

void test_ring() {
    for (const auto capacity : {std::size_t{0}, std::size_t{1} << 21}) {
        bool rejected = false;
        try { audio::StereoRing invalid(capacity); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "reject invalid ring capacity before allocation");
    }
    audio::StereoRing ring(3);
    const std::array<std::int16_t,8> input{1,2,3,4,5,6,7,8};
    check(ring.push(input.data(), 4) == 3 && ring.overruns() == 1,
          "bounded ring overrun");
    std::array<std::int16_t,10> output{};
    check(ring.pop(output.data(), 5) == 3 && ring.underruns() == 2,
          "ring underrun count");
    check(output == std::array<std::int16_t,10>{1,2,3,4,5,6,0,0,0,0},
          "ring ordering and underrun silence");
    audio::StereoRing blocked(2);
    check(blocked.push(input.data(), 2) == 2, "fill ring");
    bool completed = false;
    std::thread producer([&] { completed = blocked.push_wait(input.data() + 4, 2); });
    std::array<std::int16_t,4> first{};
    blocked.pop(first.data(), 2);
    producer.join();
    check(completed && blocked.pop(output.data(), 2) == 2 &&
          first == std::array<std::int16_t,4>{1,2,3,4} &&
          output[0] == 5 && output[3] == 8, "blocking producer backpressure");
}

void test_ast_streamer() {
    const auto bytes = pcm_ast();
    auto scan = input_of(bytes);
    const auto segments = audio::scan_ast(*scan);
    check(segments.size() == 1 && segments[0].blocks == 2,
          "two-block synthetic AST");
    audio::AstStreamer streamer(4);
    std::string error;
    check(streamer.start(input_of(bytes), segments[0], error), "start AST worker");
    await_size(streamer.ring(), 4);
    std::array<std::int16_t,8> first{};
    check(streamer.pop(first.data(), 4) == 4, "first streamed block");
    await_size(streamer.ring(), 4);
    std::array<std::int16_t,8> second{};
    check(streamer.pop(second.data(), 4) == 4, "second streamed block");
    check(first == std::array<std::int16_t,8>{1000,1000,1000,1000,
                                              2000,2000,2000,2000} &&
          second == std::array<std::int16_t,8>{3000,3000,3000,3000,
                                               4000,4000,4000,4000},
          "AST blocks resampled into ordered 48 kHz stereo");
    streamer.stop();
    check(streamer.error().empty(), "stream decoder error");
}

} // namespace

int main() {
    try {
        test_mixer();
        test_ring();
        test_ast_streamer();
        std::cout << "audio playback synthetic tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "audio playback synthetic tests: " << e.what() << '\n';
        return 1;
    }
}
