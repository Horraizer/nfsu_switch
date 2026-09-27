#include "nfsu/race_audio.h"

#include "nfsu/audio_playback.h"
#include "nfsu/audio_output.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace nfsu::audio {
namespace {

double finite_clamp(double value, double low, double high) noexcept {
    if (!std::isfinite(value)) return low;
    return std::max(low, std::min(high, value));
}

std::uint32_t q15(double gain) noexcept {
    return static_cast<std::uint32_t>(finite_clamp(gain, 0, 1) * 32768);
}

std::uint32_t q16(double pitch) noexcept {
    return static_cast<std::uint32_t>(finite_clamp(pitch, 0.01, 4) * 65536);
}

std::vector<std::uint8_t> bank_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("missing bank");
    const auto length = input.tellg();
    if (length < 0 || length > 32 * 1024 * 1024)
        throw std::runtime_error("bank exceeds 32 MiB");
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
        throw std::runtime_error("bank read failed");
    return bytes;
}

PcmClip load_clip(const std::filesystem::path& path, std::uint32_t index) {
    const auto bytes = bank_bytes(path);
    const auto bank = parse_abk(bytes);
    const auto found = std::find_if(bank.samples.begin(), bank.samples.end(),
        [&](const Sample& sample) { return sample.index == index; });
    if (found == bank.samples.end()) throw std::runtime_error("sample index absent");
    PcmClip clip;
    clip.rate = found->rate;
    clip.channels = found->channels;
    clip.loop_start = found->loop_start;
    clip.loop_end = found->loop_end;
    clip.pcm.reserve(static_cast<std::size_t>(found->frames) * found->channels);
    decode_bank_sample(bytes, *found, [&](const std::uint8_t* pcm, std::size_t size) {
        if (size % 2) throw std::runtime_error("odd PCM byte count");
        for (std::size_t i = 0; i < size; i += 2)
            clip.pcm.push_back(static_cast<std::int16_t>(
                static_cast<std::uint16_t>(pcm[i]) |
                (static_cast<std::uint16_t>(pcm[i + 1]) << 8)));
    });
    if (!clip.valid() || clip.frames() != found->frames)
        throw std::runtime_error("invalid decoded clip");
    return clip;
}

} // namespace

RaceAudioSnapshot race_audio_snapshot(const RaceSession& session) noexcept {
    RaceAudioSnapshot snapshot;
    const auto& car = session.car();
    snapshot.rpm = finite_clamp(car.engine_rpm, 0, 20000);
    const auto& telemetry = session.telemetry().model;
    snapshot.throttle = finite_clamp(telemetry.throttle, 0, 1);
    snapshot.speed_mps = finite_clamp(
        std::sqrt(car.velocity.x * car.velocity.x + car.velocity.y * car.velocity.y),
        0, 1000);
    for (const auto& wheel : telemetry.wheels)
        if (wheel.contact)
            snapshot.wheel_slip = std::max(snapshot.wheel_slip,
                std::max(std::abs(wheel.slip_ratio), std::abs(wheel.slip_lateral)));
    snapshot.wheel_slip = finite_clamp(snapshot.wheel_slip, 0, 100);
    for (const auto& step : session.physics_telemetry()) {
        snapshot.contact_events += step.impulses;
        const auto& impulse = step.impulse;
        snapshot.impact += std::sqrt(impulse.x * impulse.x +
                                     impulse.y * impulse.y + impulse.z * impulse.z);
    }
    snapshot.impact = finite_clamp(snapshot.impact, 0, 10000);
    return snapshot;
}

std::array<double, 4> engine_layer_weights(double rpm) noexcept {
    constexpr std::array<double, 4> centers{800, 2500, 4500, 7000};
    std::array<double, 4> weight{};
    rpm = finite_clamp(rpm, centers[0], 8000);
    if (rpm <= centers[0]) weight[0] = 1;
    else if (rpm >= centers[3]) weight[3] = 1;
    else for (std::size_t i = 0; i + 1 < centers.size(); ++i)
        if (rpm >= centers[i] && rpm < centers[i + 1]) {
            const double upper = (rpm - centers[i]) / (centers[i + 1] - centers[i]);
            weight[i] = 1 - upper;
            weight[i + 1] = upper;
            break;
        }
    return weight;
}

struct RaceAudioService::Impl {
    std::array<PcmClip, 4> engine;
    PcmClip skid, impact;
    std::array<bool, 4> has_engine{};
    bool has_skid = false, has_impact = false, has_music = false, active = false;
    unsigned impact_cooldown = 0;
    Mixer mixer;
    AstStreamer stream;
    Output output;
    std::atomic<bool> suspended{false};
    mutable std::mutex mixer_mutex;
    std::string note;
    std::size_t fade_frames = 0;

    void render(std::int16_t* stereo, std::size_t frames) {
        if (suspended) {
            fade_frames = 0;
            std::fill_n(stereo, frames * 2, std::int16_t{0});
            return;
        }
        std::array<std::int16_t, 1024 * 2> music{};
        std::size_t offset = 0;
        while (offset < frames) {
            const auto count = std::min<std::size_t>(1024, frames - offset);
            const bool stream_ready = has_music &&
                (!stream.finished() || stream.ring().size());
            if (stream_ready) stream.pop(music.data(), count);
            {
                std::lock_guard<std::mutex> lock(mixer_mutex);
                mixer.render(stereo + offset * 2, count,
                             stream_ready ? music.data() : nullptr, 16384);
            }
            for (std::size_t i = 0; i < count && fade_frames < 240; ++i) {
                ++fade_frames;
                stereo[(offset + i) * 2] = static_cast<std::int16_t>(
                    static_cast<std::int32_t>(stereo[(offset + i) * 2]) *
                    static_cast<std::int32_t>(fade_frames) / 240);
                stereo[(offset + i) * 2 + 1] = static_cast<std::int16_t>(
                    static_cast<std::int32_t>(stereo[(offset + i) * 2 + 1]) *
                    static_cast<std::int32_t>(fade_frames) / 240);
            }
            offset += count;
        }
    }
};

RaceAudioService::RaceAudioService() : impl_(std::make_unique<Impl>()) {}
RaceAudioService::~RaceAudioService() { stop(); }

bool RaceAudioService::start(const std::filesystem::path& root,
                             std::string& diagnostic,
                             const std::function<void(const char*)>& progress,
                             const std::function<bool()>& decoder_thread_setup) {
    auto& p = *impl_;
    if (p.active) { diagnostic = "audio already running"; return false; }
    std::ostringstream notes;
    const auto engine_path = root / "Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk";
    for (std::size_t i = 0; i < p.engine.size(); ++i) {
        if (progress) progress("audio engine");
        try {
            p.engine[i] = load_clip(engine_path, static_cast<std::uint32_t>(i + 1));
            p.has_engine[i] = p.mixer.play(i, &p.engine[i], true, 0);
        } catch (const std::exception& e) {
            notes << "engine sample " << (i + 1) << ": " << e.what() << "; ";
        }
    }
    if (progress) progress("audio skid");
    try {
        p.skid = load_clip(root / "Sound/SKIDS/SKID_PAV_MB.abk", 1);
        p.has_skid = p.mixer.play(4, &p.skid, true, 0);
    } catch (const std::exception& e) { notes << "skid: " << e.what() << "; "; }
    if (progress) progress("audio impact");
    try {
        p.impact = load_clip(root / "Sound/IG_GLOBAL/IMPACT_MB.abk", 1);
        p.has_impact = true;
    } catch (const std::exception& e) { notes << "impact: " << e.what() << "; "; }
    if (progress) progress("audio music index");
    try {
        auto input = std::make_unique<std::ifstream>(
            root / "SNDSTREAMS/STRM_Music_UG0_MB.ast", std::ios::binary);
        if (!*input) throw std::runtime_error("missing AST");
        // The supported retail segment 1 ends at 5,343,716. A 6 MiB
        // prefix covers it without scanning the rest of the 149 MB file.
        const auto segments = scan_ast_prefix(*input, 6 * 1024 * 1024, 2, [&] {
            if (progress) progress("audio music read");
        });
        if (segments.size() < 2) throw std::runtime_error("music segment 1 absent");
        std::string error;
        p.has_music = p.stream.start(std::move(input), segments[1], error,
                                      decoder_thread_setup);
        if (!p.has_music) throw std::runtime_error(error);
    } catch (const std::exception& e) { notes << "music: " << e.what() << "; "; }
    for (unsigned attempt = 0; p.has_music && attempt < 100 &&
            p.stream.ring().size() < 3 * 1024 && !p.stream.finished(); ++attempt) {
        if (progress) progress("audio music prefill");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!p.has_skid && !p.has_impact && !p.has_music &&
        std::none_of(p.has_engine.begin(), p.has_engine.end(), [](bool x) { return x; })) {
        diagnostic = "no supported retail audio: " + notes.str();
        p.note = diagnostic;
        return false;
    }
    if (progress) progress("audio output");
    std::string device_error;
    if (!p.output.start([&p](std::int16_t* pcm, std::size_t frames) {
            p.render(pcm, frames);
        }, device_error)) {
        p.stream.stop();
        diagnostic = "audio device unavailable: " + device_error + "; " + notes.str();
        p.note = diagnostic;
        return false;
    }
    p.active = true;
    p.note = notes.str().empty() ? "audio ready" : "audio ready; " + notes.str();
    diagnostic = p.note;
    return true;
}

void RaceAudioService::on_tick(const RaceAudioSnapshot& snapshot) {
    auto& p = *impl_;
    if (!p.active) return;
    std::lock_guard<std::mutex> lock(p.mixer_mutex);
    // Approximation: four equally ordered sample layers with linear RPM
    // crossfades. speed.exe proves the bank lookup, not these breakpoints.
    constexpr std::array<double, 4> centers{800, 2500, 4500, 7000};
    const double rpm = finite_clamp(snapshot.rpm, centers[0], 8000);
    const auto weight = engine_layer_weights(rpm);
    const double throttle = finite_clamp(snapshot.throttle, 0, 1);
    for (std::size_t i = 0; i < p.engine.size(); ++i)
        if (p.has_engine[i]) {
            const double pitch = finite_clamp(rpm / centers[i], 0.5, 2.0);
            p.mixer.control(i, q15(weight[i] * (0.22 + 0.33 * throttle)), q16(pitch));
        }
    const double slip = finite_clamp(snapshot.wheel_slip, 0, 10);
    const double skid_gain = snapshot.speed_mps > 2 ?
        finite_clamp((slip - 0.25) * 0.35, 0, 0.45) : 0;
    if (p.has_skid) p.mixer.control(4, q15(skid_gain), 65536);
    if (p.impact_cooldown) --p.impact_cooldown;
    if (p.has_impact && !p.impact_cooldown && snapshot.contact_events &&
        snapshot.impact > 2) {
        const double gain = finite_clamp(snapshot.impact / 30, 0.1, 0.7);
        p.mixer.play(5, &p.impact, false, q15(gain));
        p.impact_cooldown = 10;
    }
}

void RaceAudioService::set_suspended(bool suspended) noexcept {
    impl_->suspended = suspended;
}

void RaceAudioService::stop() {
    auto& p = *impl_;
    p.active = false;
    p.output.stop();
    p.stream.stop();
}

std::string RaceAudioService::diagnostic() const {
    const auto output_error = impl_->output.error();
    const auto stream_error = impl_->stream.error();
    std::string result = impl_->note;
    if (!output_error.empty()) result += "; " + output_error;
    if (!stream_error.empty()) result += "; " + stream_error;
    return result;
}

RaceAudioService::Stats RaceAudioService::stats() const noexcept {
    const auto& p = *impl_;
    const auto output = p.output.stats();
    std::lock_guard<std::mutex> lock(p.mixer_mutex);
    return {p.stream.ring().underruns(), p.stream.ring().size(),
            p.stream.ring().capacity(), output.late, output.queued_frames,
            output.work_us, p.mixer.clipped_samples()};
}

std::size_t RaceAudioService::resident_bytes() const noexcept {
    const auto& p = *impl_;
    std::size_t bytes = p.stream.ring().capacity() * 2 * sizeof(std::int16_t) + 3 * 4096;
    for (const auto& clip : p.engine) bytes += clip.pcm.capacity() * sizeof(std::int16_t);
    bytes += p.skid.pcm.capacity() * sizeof(std::int16_t);
    bytes += p.impact.pcm.capacity() * sizeof(std::int16_t);
    return bytes;
}

} // namespace nfsu::audio
