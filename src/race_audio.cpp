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

PcmClip decode_clip(const std::vector<std::uint8_t>& bytes,
                    const Bank& bank, std::uint32_t index) {
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

PcmClip load_clip(const std::filesystem::path& path, std::uint32_t index) {
    const auto bytes = bank_bytes(path);
    return decode_clip(bytes, parse_abk(bytes), index);
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
    snapshot.tick = session.timing().step;
    snapshot.physics_step = session.telemetry().step;
    for (const auto& step : session.physics_telemetry()) {
        snapshot.collision_reset |= step.reset != VehicleTrackReset::none;
        for (std::size_t i = 0; i < step.audio_contact_count && i < step.audio_contacts.size(); ++i) {
            if (snapshot.contact_count == snapshot.contacts.size()) break;
            const auto& c = step.audio_contacts[i];
            snapshot.contacts[snapshot.contact_count++] =
                {step.step,c.polygon,c.impulse,c.forward,c.up,c.slide_mps,3};
        }
    }
    const auto& timing = session.timing();
    snapshot.countdown = timing.countdown_shown;
    if (!timing.racers.empty())
        snapshot.completed_laps = timing.racers[0].highest_lap;
    snapshot.finished = timing.outcome == RaceOutcome::finished;
    return snapshot;
}

RaceInterfaceCue RaceCueScheduler::update(const RaceAudioSnapshot& snapshot) noexcept {
    RaceInterfaceCue cue = RaceInterfaceCue::none;
    if (snapshot.finished && !finished_)
        cue = RaceInterfaceCue::finish;
    else if (snapshot.completed_laps > completed_laps_)
        cue = RaceInterfaceCue::lap;
    else if (snapshot.countdown != countdown_) {
        if (snapshot.countdown >= 1 && snapshot.countdown <= 3)
            cue = RaceInterfaceCue::countdown;
        else if (snapshot.countdown == 4)
            cue = RaceInterfaceCue::go;
    }
    countdown_ = snapshot.countdown;
    completed_laps_ = snapshot.completed_laps;
    finished_ = snapshot.finished;
    return cue;
}

std::vector<Segment> race_music_playlist(const Bank& bank,
                                         const std::vector<Segment>& indexed) {
    if (indexed.size() < 2 || indexed.front().offset != 0)
        throw std::runtime_error("music segment 0 absent");
    std::vector<Segment> playlist;
    for (const auto& entry : bank.entries) {
        if (entry.type != 1 || entry.value == indexed.front().offset) continue;
        const auto found = std::find_if(indexed.begin(), indexed.end(),
            [&](const Segment& segment) { return segment.offset == entry.value; });
        if (found == indexed.end())
            throw std::runtime_error("music entry misses AST segment");
        playlist.push_back(*found);
    }
    if (playlist.empty()) throw std::runtime_error("music playlist empty");
    return playlist;
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
    PcmClip skid, scrape;
    std::array<PcmClip, 13> impacts;
    CollisionGraph collision;
    CollisionScheduler collisions;
    std::array<std::uint32_t, 2> impact_pairs{UINT32_MAX,UINT32_MAX};
    std::array<std::uint32_t, 2> scrape_pairs{UINT32_MAX,UINT32_MAX};
    std::array<PcmClip, 4> cue_clips;
    std::array<bool, 4> has_engine{};
    std::array<bool, 4> has_cue{};
    bool has_skid = false, has_impact = false, has_music = false, active = false;
    std::size_t music_tracks = 0;
    std::size_t impact_slot = 8;
    Mixer mixer;
    RaceCueScheduler cues;
    AstStreamer stream;
    Output output;
    std::atomic<bool> suspended{false};
    std::atomic<bool> finished{false};
    mutable std::mutex mixer_mutex;
    std::string note;
    OutputFade fade;
    OutputFade music_fade;

    Impl() { mixer.set_peak_limiter(true); }

    void render(std::int16_t* stereo, std::size_t frames) {
        const bool muted = suspended.load();
        if (muted && fade.silent()) {
            std::fill_n(stereo, frames * 2, std::int16_t{0});
            return;
        }
        std::array<std::int16_t, 1024 * 2> music{};
        std::size_t offset = 0;
        while (offset < frames) {
            const auto count = std::min<std::size_t>(1024, frames - offset);
            const bool ending = finished.load();
            const bool stream_ready = has_music &&
                (!ending || !music_fade.silent()) &&
                (!stream.finished() || stream.ring().size());
            if (stream_ready) {
                stream.pop(music.data(), count);
                music_fade.apply(music.data(), count, ending);
            }
            {
                std::lock_guard<std::mutex> lock(mixer_mutex);
                const auto music_gain = mixer.active(6) || mixer.active(7) ?
                    8192U : 16384U;
                mixer.render(stereo + offset * 2, count,
                             stream_ready ? music.data() : nullptr, music_gain);
            }
            fade.apply(stereo + offset * 2, count, muted);
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
    p.collisions.clear(); p.impact_pairs = {UINT32_MAX,UINT32_MAX};
    p.scrape_pairs = {UINT32_MAX,UINT32_MAX};
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
        const auto impact_bytes = bank_bytes(root / "Sound/IG_GLOBAL/IMPACT_MB.abk");
        const auto scrape_bytes = bank_bytes(root / "Sound/IG_GLOBAL/SCRAPE_MB.abk");
        p.collision = collision_graph(impact_bytes,scrape_bytes);
        const auto impact_bank = parse_abk(impact_bytes), scrape_bank = parse_abk(scrape_bytes);
        for (std::size_t i = 0; i < p.impacts.size(); ++i)
            p.impacts[i] = decode_clip(impact_bytes,impact_bank,30+std::uint32_t(i));
        p.scrape = decode_clip(scrape_bytes,scrape_bank,1);
        p.has_impact = true;
    } catch (const std::exception& e) { notes << "collision: " << e.what() << "; "; }
    if (progress) progress("audio interface cues");
    try {
        const auto bytes = bank_bytes(root / "Sound/FE/FE_MB.abk");
        const auto bank = parse_abk(bytes);
        // Provisional candidates pending the owner's listening check.
        constexpr std::array<std::uint32_t, 4> samples{25, 26, 29, 30};
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (progress) progress("audio interface cues");
            try {
                p.cue_clips[i] = decode_clip(bytes, bank, samples[i]);
                p.has_cue[i] = true;
            } catch (const std::exception& e) {
                notes << "cue sample " << samples[i] << ": " << e.what() << "; ";
            }
        }
    } catch (const std::exception& e) { notes << "cues: " << e.what() << "; "; }
    if (progress) progress("audio music index");
    try {
        auto input = std::make_unique<std::ifstream>(
            root / "SNDSTREAMS/STRM_Music_UG0_MB.ast", std::ios::binary);
        if (!*input) throw std::runtime_error("missing AST");
        const auto bank = parse_abk(bank_bytes(
            root / "Sound/STRMBANK/STRM_Music_UG0_MB.abk"));
        std::vector<std::uint64_t> offsets;
        for (const auto& entry : bank.entries)
            if (entry.type == 1) offsets.push_back(entry.value);
        std::sort(offsets.begin(), offsets.end());
        if (offsets.size() < 2 || offsets.size() > 128 ||
            offsets.front() != 0 ||
            std::adjacent_find(offsets.begin(), offsets.end()) != offsets.end())
            throw std::runtime_error("invalid music bank offsets");
        input->seekg(0, std::ios::end);
        const auto end = input->tellg();
        if (end < 0) throw std::runtime_error("music AST size unavailable");
        const auto file_size = static_cast<std::uint64_t>(end);
        std::vector<Segment> indexed;
        indexed.reserve(offsets.size());
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            if (progress) progress("audio playlist index");
            indexed.push_back(index_ast_segment_by_boundary(*input,
                offsets[i], i + 1 < offsets.size() ? offsets[i + 1] : file_size));
        }
        auto playlist = race_music_playlist(bank, indexed);
        std::string error;
        const auto track_count = playlist.size();
        p.has_music = p.stream.start_playlist(std::move(input),
            std::move(playlist), true, error, decoder_thread_setup);
        if (!p.has_music) throw std::runtime_error(error);
        p.music_tracks = track_count;
    } catch (const std::exception& e) { notes << "music: " << e.what() << "; "; }
    for (unsigned attempt = 0; p.has_music && attempt < 100 &&
            p.stream.ring().size() < 3 * 1024 && !p.stream.finished(); ++attempt) {
        if (progress) progress("audio music prefill");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!p.has_skid && !p.has_impact && !p.has_music &&
        std::none_of(p.has_engine.begin(), p.has_engine.end(), [](bool x) { return x; }) &&
        std::none_of(p.has_cue.begin(), p.has_cue.end(), [](bool x) { return x; })) {
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
    p.note = "audio ready; tracks=" + std::to_string(p.music_tracks);
    if (!notes.str().empty()) p.note += "; " + notes.str();
    diagnostic = p.note;
    return true;
}

void RaceAudioService::on_tick(const RaceAudioSnapshot& snapshot) {
    auto& p = *impl_;
    if (!p.active || p.suspended.load()) return;
    std::lock_guard<std::mutex> lock(p.mixer_mutex);
    const auto cue = p.cues.update(snapshot);
    if (cue != RaceInterfaceCue::none) {
        const std::size_t index = cue == RaceInterfaceCue::countdown ? 0 :
            cue == RaceInterfaceCue::go ? 1 :
            cue == RaceInterfaceCue::lap ? 2 : 3;
        if (p.has_cue[index])
            p.mixer.play(index < 2 ? 6 : 7, &p.cue_clips[index], false,
                         q15(index < 2 ? 0.55 : 0.65));
    }
    if (snapshot.finished) {
        p.finished = true;
        for (std::size_t i = 0; i < p.engine.size(); ++i)
            if (p.has_engine[i]) p.mixer.control(i, 0, 65536);
        if (p.has_skid) p.mixer.control(4, 0, 65536);
        p.mixer.control(9,0,65536); p.mixer.control(10,0,65536);
        return;
    }
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
    if (p.has_impact) {
        const auto events = p.collisions.update(snapshot.tick,snapshot.physics_step,snapshot.contacts.data(),
            std::min(snapshot.contact_count,snapshot.contacts.size()),p.collision,snapshot.collision_reset);
        if (!events.processed) return;
        if (snapshot.collision_reset) {
            p.mixer.stop(5); p.mixer.stop(8); p.mixer.stop(9); p.mixer.stop(10);
            p.impact_pairs = {UINT32_MAX,UINT32_MAX}; p.scrape_pairs = {UINT32_MAX,UINT32_MAX};
        }
        for (const auto& e : events.impacts) {
            // Two impact voices are a bounded port policy, not retail admission.
            if (e.start) {
                p.impact_slot = p.impact_slot == 5 ? 8 : 5;
                p.impact_pairs[p.impact_slot == 5 ? 0 : 1] = e.pair;
                p.mixer.play(p.impact_slot,&p.impacts[e.sample-30],false,e.gain_q15,e.pitch_q16,true);
            } else for (std::size_t i = 0; i < 2; ++i)
                if (p.impact_pairs[i] == e.pair) p.mixer.control(i == 0 ? 5 : 8,e.gain_q15,e.pitch_q16);
        }
        for (std::size_t i = 0; i < p.scrape_pairs.size(); ++i) {
            const auto found = std::find_if(events.scrapes.begin(),events.scrapes.end(),
                [&](const auto& e) { return e.pair == p.scrape_pairs[i]; });
            if (found == events.scrapes.end()) {
                p.mixer.control(9+i,0,65536); p.scrape_pairs[i] = UINT32_MAX;
            }
        }
        for (const auto& e : events.scrapes) {
            std::size_t slot = p.scrape_pairs.size();
            for (std::size_t i = 0; i < p.scrape_pairs.size(); ++i)
                if (p.scrape_pairs[i] == e.pair) { slot=i; break; }
            if (slot == p.scrape_pairs.size()) for (std::size_t i=0; i<p.scrape_pairs.size(); ++i)
                if (p.scrape_pairs[i] == UINT32_MAX) { slot=i; break; }
            if (slot == p.scrape_pairs.size()) continue;
            if (p.scrape_pairs[slot] != e.pair || e.start) {
                p.scrape_pairs[slot]=e.pair;
                p.mixer.play(9+slot,&p.scrape,true,e.gain_q15,e.pitch_q16,true);
            } else p.mixer.control(9+slot,e.gain_q15,e.pitch_q16);
        }
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
            output.work_us, p.mixer.clipped_samples(),
            p.mixer.overloaded_samples(), p.mixer.limited_frames()};
}

std::size_t RaceAudioService::resident_bytes() const noexcept {
    const auto& p = *impl_;
    std::size_t bytes = p.stream.ring().capacity() * 2 * sizeof(std::int16_t) + 3 * 4096;
    for (const auto& clip : p.engine) bytes += clip.pcm.capacity() * sizeof(std::int16_t);
    bytes += p.skid.pcm.capacity() * sizeof(std::int16_t);
    bytes += p.scrape.pcm.capacity() * sizeof(std::int16_t);
    for (const auto& clip : p.impacts) bytes += clip.pcm.capacity() * sizeof(std::int16_t);
    for (const auto& clip : p.cue_clips)
        bytes += clip.pcm.capacity() * sizeof(std::int16_t);
    return bytes;
}

} // namespace nfsu::audio
