#include "nfsu/audio_output.h"

#include <switch.h>
#include <switch/audio/driver.h>
#include <malloc.h>

#include <array>
#include <atomic>
#include <exception>
#include <mutex>
#include <utility>

namespace nfsu::audio {
namespace {
constexpr std::size_t kFrames = 1024;
constexpr std::size_t kBuffers = 3;
constexpr std::size_t kBufferBytes = kFrames * 2 * sizeof(std::int16_t);
constexpr std::size_t kPoolBytes = kBuffers * kBufferBytes;
constexpr int kVoiceChannels = 2;
constexpr int kChannelPool = 2;
static_assert(kChannelPool >= kVoiceChannels,
              "audren channel pool must cover the stereo voice");
} // namespace

struct Output::Impl {
    AudioDriver driver{};
    std::array<AudioDriverWaveBuf, kBuffers> buffers{};
    std::int16_t* pcm = nullptr;
    bool audren_initialized = false;
    bool driver_initialized = false;
    bool renderer_started = false;
    Render render;
    Thread thread{};
    bool thread_started = false;
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> late{0};
    std::atomic<std::uint64_t> played_frames{0};
    std::atomic<std::uint32_t> queued_frames{0};
    std::atomic<std::uint32_t> work_us{0};
    mutable std::mutex error_mutex;
    std::string failure;

    void fail(const char* message) {
        { std::lock_guard<std::mutex> lock(error_mutex); failure = message; }
        running = false;
    }
    void run() {
        try {
            bool primed = false;
            while (running) {
                const auto begin = armGetSystemTick();
                std::size_t pending = 0;
                for (const auto& buffer : buffers)
                    if (buffer.state == AudioDriverWaveBufState_Waiting ||
                        buffer.state == AudioDriverWaveBufState_Queued ||
                        buffer.state == AudioDriverWaveBufState_Playing) ++pending;
                if (primed && !pending) ++late;
                for (std::size_t i = 0; i < kBuffers && running; ++i) {
                    auto& buffer = buffers[i];
                    if (buffer.state != AudioDriverWaveBufState_Free &&
                        buffer.state != AudioDriverWaveBufState_Done)
                        continue;
                    auto* destination = pcm + i * kFrames * 2;
                    render(destination, kFrames);
                    armDCacheFlush(destination, kBufferBytes);
                    if (!audrvVoiceAddWaveBuf(&driver, 0, &buffer)) {
                        fail("audrvVoiceAddWaveBuf failed");
                        return;
                    }
                }
                if (R_FAILED(audrvUpdate(&driver))) {
                    fail("audrvUpdate failed");
                    return;
                }
                pending = 0;
                for (const auto& buffer : buffers)
                    if (buffer.state == AudioDriverWaveBufState_Waiting ||
                        buffer.state == AudioDriverWaveBufState_Queued ||
                        buffer.state == AudioDriverWaveBufState_Playing) ++pending;
                queued_frames = static_cast<std::uint32_t>(pending * kFrames);
                work_us = static_cast<std::uint32_t>(armTicksToNs(
                    armGetSystemTick() - begin) / 1000);
                played_frames = audrvVoiceGetPlayedSampleCount(&driver, 0);
                primed = true;
                eventWait(audrenGetFrameEvent(), 50000000ULL);
            }
        } catch (const std::exception&) {
            fail("audio render failed");
        }
    }
};

Output::Output() : impl_(std::make_unique<Impl>()) {}
Output::~Output() { stop(); }

bool Output::start(Render render, std::string& error) {
    auto& p = *impl_;
    if (p.audren_initialized || !render) {
        error = "audio output already open or callback missing";
        return false;
    }
    // Each start is a new playback session, including after backend failure.
    { std::lock_guard<std::mutex> lock(p.error_mutex); p.failure.clear(); }
    error.clear();
    p.late = 0;
    p.played_frames = 0;
    p.queued_frames = 0;
    p.work_us = 0;
    const AudioRendererConfig config{
        AudioRendererOutputRate_48kHz, kChannelPool, 0, 1, 1, 2};
    if (R_FAILED(audrenInitialize(&config))) {
        error = "audrenInitialize failed";
        return false;
    }
    p.audren_initialized = true;
    p.pcm = static_cast<std::int16_t*>(memalign(AUDREN_MEMPOOL_ALIGNMENT,
                                               kPoolBytes));
    if (!p.pcm) {
        error = "audren mempool allocation failed";
        stop();
        return false;
    }
    if (R_FAILED(audrvCreate(&p.driver, &config, 2))) {
        error = "audrvCreate failed";
        stop();
        return false;
    }
    p.driver_initialized = true;
    const int pool = audrvMemPoolAdd(&p.driver, p.pcm, kPoolBytes);
    if (pool < 0 || !audrvMemPoolAttach(&p.driver, pool)) {
        error = "audren mempool attach failed";
        stop();
        return false;
    }
    static const u8 channels[] = {0, 1};
    if (audrvDeviceSinkAdd(&p.driver, AUDREN_DEFAULT_DEVICE_NAME,
                           2, channels) < 0 ||
        R_FAILED(audrvUpdate(&p.driver))) {
        error = "audren sink setup failed";
        stop();
        return false;
    }
    if (R_FAILED(audrenStartAudioRenderer())) {
        error = "audrenStartAudioRenderer failed";
        stop();
        return false;
    }
    p.renderer_started = true;
    if (!audrvVoiceInit(&p.driver, 0, kVoiceChannels, PcmFormat_Int16, 48000)) {
        error = "audren stereo voice init failed";
        stop();
        return false;
    }
    audrvVoiceSetDestinationMix(&p.driver, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&p.driver, 0, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&p.driver, 0, 0.0f, 0, 1);
    audrvVoiceSetMixFactor(&p.driver, 0, 0.0f, 1, 0);
    audrvVoiceSetMixFactor(&p.driver, 0, 1.0f, 1, 1);
    audrvVoiceStart(&p.driver, 0);
    for (std::size_t i = 0; i < kBuffers; ++i) {
        p.buffers[i].data_pcm16 = p.pcm;
        p.buffers[i].size = kPoolBytes;
        p.buffers[i].start_sample_offset = static_cast<s32>(i * kFrames);
        p.buffers[i].end_sample_offset = static_cast<s32>((i + 1) * kFrames);
    }
    p.render = std::move(render);
    p.running = true;
    const auto main_core = svcGetCurrentProcessorNumber();
    const int audio_core = main_core == 2 ? 1 : 2;
    if (R_FAILED(threadCreate(&p.thread,
            [](void* context) { static_cast<Impl*>(context)->run(); },
            &p, nullptr, 0x20000, 0x28, audio_core))) {
        error = "audio output threadCreate failed";
        stop();
        return false;
    }
    if (R_FAILED(threadStart(&p.thread))) {
        threadClose(&p.thread);
        error = "audio output threadStart failed";
        stop();
        return false;
    }
    p.thread_started = true;
    return true;
}

void Output::stop() {
    auto& p = *impl_;
    p.running = false;
    if (p.thread_started) {
        threadWaitForExit(&p.thread);
        threadClose(&p.thread);
        p.thread_started = false;
    }
    if (p.renderer_started) {
        audrenStopAudioRenderer();
        p.renderer_started = false;
    }
    if (p.driver_initialized) {
        audrvClose(&p.driver);
        p.driver_initialized = false;
    }
    if (p.audren_initialized) {
        audrenExit();
        p.audren_initialized = false;
    }
    // audrvClose owns only driver storage, not these caller-owned records.
    // Reset only after the thread and renderer no longer reference them.
    p.buffers = {};
    p.render = {};
    p.queued_frames = 0;
    p.work_us = 0;
    free(p.pcm);
    p.pcm = nullptr;
}

std::string Output::error() const {
    std::lock_guard<std::mutex> lock(impl_->error_mutex);
    return impl_->failure;
}

Output::Stats Output::stats() const noexcept {
    return {impl_->late.load(), impl_->played_frames.load(), impl_->queued_frames.load(),
            impl_->work_us.load()};
}

} // namespace nfsu::audio
