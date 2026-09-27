#include "nfsu/audio_playback.h"

#include <array>
#include <chrono>
#include <cmath>
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

Bytes pcm_ast(std::uint16_t rate = 24000, std::int16_t offset = 0) {
    Bytes bytes;
    const Bytes header = {'P','T',0,0, 0x80,1,2, 0x82,1,2,
        0x84,2,static_cast<std::uint8_t>(rate >> 8),
        static_cast<std::uint8_t>(rate), 0x85,1,4, 0xA0,1,8, 0xFF};
    chunk(bytes, "SCHl", header);
    Bytes count; u32(count, 2); chunk(bytes, "SCCl", count);
    for (const auto base : {1000, 3000}) {
        Bytes body; u32(body, 2); u32(body, 0); u32(body, 4);
        pcm16(body, static_cast<std::int16_t>(base + offset));
        pcm16(body, static_cast<std::int16_t>(base + 1000 + offset));
        pcm16(body, static_cast<std::int16_t>(-base - offset));
        pcm16(body, static_cast<std::int16_t>(-base - 1000 - offset));
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
    check(out[0] > 5000 && out[0] < 5100 && out[1] == out[0],
          "voice gain ramp begins smoothly");
    std::array<std::int16_t, 239 * 2> ramp{};
    mixer.render(ramp.data(), 239);
    check(ramp[ramp.size() - 2] == 10000 && ramp.back() == 10000,
          "voice gain ramp reaches target in five milliseconds");
    mixer.stop(0);
    mixer.render(out.data(), 1);
    check(out[0] == 0 && out[1] == 0, "voice stop");

    audio::PcmClip loud{{30000}, 48000, 1};
    check(mixer.play(0, &loud, true) && mixer.play(1, &loud, true),
          "start two voices");
    mixer.render(out.data(), 1);
    check(out[0] == 32767 && out[1] == 32767 &&
          mixer.clipped_samples() == 2, "positive mix clipping");
    const std::array<std::int16_t,2> opposite{-30000,-30000};
    mixer.render(out.data(), 1, opposite.data(), 32768);
    check(out[0] == 30000 && out[1] == 30000,
          "stream is summed before final clipping");
    mixer.stop(0); mixer.stop(1);
    audio::PcmClip negative{{-30000}, 48000, 1};
    check(mixer.play(0, &negative, true) && mixer.play(1, &negative, true),
          "start negative voices");
    mixer.render(out.data(), 1);
    check(out[0] == -32768 && out[1] == -32768 &&
          mixer.clipped_samples() == 4, "negative mix clipping");
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
    check(out[0] == 1000 && out[2] == 1500 && out[4] == 2000 &&
          out[6] == 2000 && out[8] == 0 && !mixer.active(0),
          "linear resample and non-looping end");
    audio::PcmClip stereo{{1000,-2000,3000,-4000}, 24000, 2};
    check(mixer.play(0, &stereo, false), "start stereo voice");
    out.fill(0);
    mixer.render(out.data(), 4);
    check(out == std::array<std::int16_t,10>{1000,-2000,2000,-3000,
                                             3000,-4000,3000,-4000,0,0},
          "stereo channels and linear midpoint");
    audio::PcmClip boundary{{1000,2000,4000}, 24000, 1, 1, 3};
    check(mixer.play(0, &boundary, true), "start loop boundary voice");
    std::array<std::int16_t, 14> boundary_out{};
    mixer.render(boundary_out.data(), 7);
    check(boundary_out == std::array<std::int16_t, 14>{
        1000,1000,1500,1500,2000,2000,3000,3000,
        4000,4000,3000,3000,2000,2000},
        "loop-end interpolation uses loop-start neighbour");
    mixer.stop(0);

    audio::PcmClip impact;
    impact.rate = 48000;
    impact.channels = 1;
    impact.pcm.assign(1000, 10000);
    check(mixer.play(5, &impact, false, 32768, 65536, true),
          "start impact with attack fade");
    std::array<std::int16_t, 240 * 2> impact_out{};
    mixer.render(impact_out.data(), 120);
    check(impact_out[0] < 100 && impact_out[238] == 5000,
          "impact attack reaches half level smoothly");
    mixer.control(5, 0, 65536);
    check(mixer.play(8, &impact, false, 32768, 65536, true),
          "start second impact on alternating slot");
    mixer.render(impact_out.data(), 240);
    check(impact_out[0] > 4900 && impact_out[0] < 5100 &&
          impact_out[478] == 10000,
          "restarted impacts crossfade without a step");
}

void test_output_fade() {
    audio::OutputFade fade;
    std::array<std::int16_t, 240 * 2> output{};
    output.fill(24000);
    fade.apply(output.data(), 240, false);
    check(output[0] == 100 && output[478] == 24000 && !fade.silent(),
          "output fades in over five milliseconds");
    output.fill(24000);
    fade.apply(output.data(), 240, true);
    check(output[0] == 23900 && output[478] == 0 && fade.silent(),
          "pause and finish fade to silence");
    output.fill(24000);
    fade.apply(output.data(), 1, false);
    check(output[0] == 100 && !fade.silent(),
          "output resumes without an abrupt step");
}

void test_alias_rejection() {
    constexpr double pi = 3.14159265358979323846;
    constexpr int source_rate = 22050, output_rate = 48000;
    constexpr int tone_hz = 5000, image_hz = source_rate - tone_hz;
    audio::PcmClip tone;
    tone.rate = source_rate;
    tone.channels = 1;
    tone.pcm.resize(source_rate);
    for (int i = 0; i < source_rate; ++i)
        tone.pcm[i] = static_cast<std::int16_t>(std::lround(
            12000 * std::sin(2 * pi * tone_hz * i / source_rate)));
    audio::Mixer mixer;
    check(mixer.play(0, &tone, true), "start sine voice");
    std::vector<std::int16_t> output(output_rate * 2);
    mixer.render(output.data(), output_rate);
    const auto amplitude = [&](int frequency) {
        double real = 0, imaginary = 0;
        for (int i = 0; i < output_rate; ++i) {
            const double angle = 2 * pi * frequency * i / output_rate;
            real += output[i * 2] * std::cos(angle);
            imaginary += output[i * 2] * std::sin(angle);
        }
        return std::hypot(real, imaginary);
    };
    const auto sinc = [&](double x) { return std::sin(pi * x) / (pi * x); };
    // A zero-order hold has an image/main amplitude ratio given by its sinc
    // response. Linear interpolation must suppress that image by >= 6 dB.
    const double zoh_image_ratio = std::abs(sinc(double(image_hz) / source_rate) /
                                            sinc(double(tone_hz) / source_rate));
    check(amplitude(image_hz) < amplitude(tone_hz) * zoh_image_ratio * 0.5,
          "linear interpolation rejects 22.05 kHz image");
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
    Bytes two_segments = bytes;
    two_segments.insert(two_segments.end(), bytes.begin(), bytes.end());
    auto limited_input = input_of(two_segments);
    unsigned polls = 0;
    check(audio::scan_ast(*limited_input, 1, [&] { ++polls; }).size() == 1 &&
          polls > 0, "bounded AST scan stops after wanted segment");
    auto prefix_input = input_of(two_segments);
    check(audio::scan_ast_prefix(*prefix_input, two_segments.size(), 1,
          [&] { ++polls; }).size() == 1 && polls > 1,
          "buffered AST prefix stops after wanted segment");
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
    check(first == std::array<std::int16_t,8>{1000,-1000,1500,-1500,
                                              2000,-2000,2500,-2500} &&
          second == std::array<std::int16_t,8>{3000,-3000,3500,-3500,
                                               4000,-4000,4000,-4000},
          "AST blocks resampled into ordered 48 kHz stereo");
    streamer.stop();
    check(streamer.error().empty(), "stream decoder error");

    Bytes playlist = pcm_ast();
    playlist.resize((playlist.size() + 127U) & ~std::size_t{127}, 0);
    const auto next = pcm_ast(24000, 5000);
    playlist.insert(playlist.end(), next.begin(), next.end());
    auto playlist_scan = input_of(playlist);
    const auto parts = audio::scan_ast(*playlist_scan);
    check(parts.size() == 2, "two playlist segments");
    auto indexed_input = input_of(playlist);
    const auto indexed = audio::index_ast_segment_by_boundary(
        *indexed_input, parts[0].offset, parts[1].offset);
    check(indexed.end == parts[0].end && indexed.frames == parts[0].frames &&
          indexed.blocks == parts[0].blocks && indexed.rate == parts[0].rate,
          "boundary AST index matches full scan");
    audio::AstStreamer repeated(4);
    check(repeated.start_playlist(input_of(playlist), parts, true, error),
          "start looping playlist");
    for (const auto value : {1000, 3000, 6000, 8000, 1000}) {
        await_size(repeated.ring(), 4);
        std::array<std::int16_t, 8> chunk{};
        check(repeated.pop(chunk.data(), 4) == 4 &&
              chunk[0] == value && chunk[1] == -value,
              "playlist segment order, stereo and loop");
    }
    repeated.stop();
    check(repeated.error().empty(), "playlist decoder error");
}

void test_ast_22050() {
    const auto bytes = pcm_ast(22050);
    auto scan = input_of(bytes);
    const auto segments = audio::scan_ast(*scan);
    audio::AstStreamer streamer(16);
    std::string error;
    check(streamer.start(input_of(bytes), segments[0], error),
          "start 22.05 kHz AST worker");
    await_size(streamer.ring(), 9);
    std::array<std::int16_t, 18> output{};
    check(streamer.pop(output.data(), 9) == 9,
          "22.05 kHz AST output count");
    check(output[0] == 1000 && output[1] == -1000 &&
          output[2] == 1459 && output[3] == -1459 &&
          output[4] == 1918 && output[5] == -1918 &&
          output[12] == 3756 && output[13] == -3756 &&
          output[16] == 4000 && output[17] == -4000,
          "22.05 to 48 kHz rational interpolation and channel order");
    streamer.stop();
    check(streamer.error().empty(), "22.05 kHz decoder error");
}

void test_decoder_thread_setup_failure() {
    const auto bytes = pcm_ast();
    auto scan = input_of(bytes);
    const auto segments = audio::scan_ast(*scan);
    audio::AstStreamer streamer(16);
    std::string error;
    check(streamer.start(input_of(bytes), segments[0], error,
          [] { return false; }), "start decoder with rejected thread setup");
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (!streamer.finished() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(streamer.finished(), "decoder thread setup returns promptly");
    streamer.stop();
    check(streamer.error() == "audio decoder core setup failed",
          "decoder thread setup failure is reported");
}

} // namespace

int main() {
    try {
        test_mixer();
        test_output_fade();
        test_alias_rejection();
        test_ring();
        test_ast_streamer();
        test_ast_22050();
        test_decoder_thread_setup_failure();
        std::cout << "audio playback synthetic tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "audio playback synthetic tests: " << e.what() << '\n';
        return 1;
    }
}
