#include "nfsu/audio_output.h"

#include <switch.h>
#include <malloc.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace nfsu::audio {
namespace {
constexpr std::size_t kFrames = 1024;
constexpr std::size_t kBytes = kFrames * 2 * sizeof(std::int16_t);
} // namespace

struct Output::Impl {
    std::int16_t* pcm = nullptr;
    bool initialized = false;
    Render render;
    std::thread thread;
    std::atomic<bool> running{false};
    mutable std::mutex error_mutex;
    std::string failure;

    void fail(const char* message) {
        { std::lock_guard<std::mutex> lock(error_mutex); failure = message; }
        running = false;
    }
    void run() {
        AudioOutBuffer buffer{};
        buffer.buffer = pcm;
        buffer.buffer_size = kBytes;
        buffer.data_size = kBytes;
        try {
            while (running) {
                render(pcm, kFrames);
                AudioOutBuffer* released = nullptr;
                if (R_FAILED(audoutPlayBuffer(&buffer, &released))) {
                    fail("audoutPlayBuffer failed");
                    break;
                }
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
    if (p.initialized || !render) {
        error = "audio output already open or callback missing";
        return false;
    }
    if (R_FAILED(audoutInitialize())) { error = "audoutInitialize failed"; return false; }
    p.initialized = true;
    if (audoutGetSampleRate() != 48000 || audoutGetChannelCount() != 2 ||
        audoutGetPcmFormat() != PcmFormat_Int16) {
        error = "audout format is not 48 kHz stereo int16";
        stop();
        return false;
    }
    p.pcm = static_cast<std::int16_t*>(memalign(0x1000, kBytes));
    if (!p.pcm) { error = "audout buffer allocation failed"; stop(); return false; }
    if (R_FAILED(audoutStartAudioOut())) {
        error = "audoutStartAudioOut failed";
        stop();
        return false;
    }
    p.render = std::move(render);
    p.running = true;
    p.thread = std::thread([&p] { p.run(); });
    return true;
}

void Output::stop() {
    auto& p = *impl_;
    p.running = false;
    if (p.thread.joinable()) p.thread.join();
    if (p.initialized) {
        audoutStopAudioOut();
        audoutExit();
        p.initialized = false;
    }
    free(p.pcm);
    p.pcm = nullptr;
}

std::string Output::error() const {
    std::lock_guard<std::mutex> lock(impl_->error_mutex);
    return impl_->failure;
}

} // namespace nfsu::audio
