#include "nfsu/audio_output.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace nfsu::audio {
namespace {
constexpr std::size_t kFrames = 1024;
constexpr std::size_t kBuffers = 3;
} // namespace

struct Output::Impl {
    HWAVEOUT device = nullptr;
    HANDLE event = nullptr;
    std::array<std::array<std::int16_t, kFrames * 2>, kBuffers> pcm{};
    std::array<WAVEHDR, kBuffers> headers{};
    std::size_t prepared = 0;
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
        std::size_t index = 0;
        try {
            while (running) {
                auto& header = headers[index];
                while (running && (header.dwFlags & WHDR_INQUEUE))
                    WaitForSingleObject(event, 50);
                if (!running) break;
                render(pcm[index].data(), kFrames);
                if (waveOutWrite(device, &header, sizeof(header)) != MMSYSERR_NOERROR) {
                    fail("waveOutWrite failed");
                    break;
                }
                index = (index + 1) % kBuffers;
            }
        } catch (const std::exception&) {
            fail("audio render failed");
        }
    }
};

Output::Output() : impl_(std::make_unique<Impl>()) {}
Output::~Output() { stop(); }

bool Output::start(Render render, std::string& error) {
    if (impl_->device || !render) { error = "audio output already open or callback missing"; return false; }
    auto& p = *impl_;
    p.event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!p.event) { error = "CreateEvent failed"; return false; }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 48000;
    format.nAvgBytesPerSec = 48000 * 4;
    format.nBlockAlign = 4;
    format.wBitsPerSample = 16;
    if (waveOutOpen(&p.device, WAVE_MAPPER, &format,
            reinterpret_cast<DWORD_PTR>(p.event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        error = "waveOutOpen failed";
        stop();
        return false;
    }
    for (std::size_t i = 0; i < kBuffers; ++i) {
        p.headers[i].lpData = reinterpret_cast<LPSTR>(p.pcm[i].data());
        p.headers[i].dwBufferLength = static_cast<DWORD>(p.pcm[i].size() * sizeof(std::int16_t));
        if (waveOutPrepareHeader(p.device, &p.headers[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            error = "waveOutPrepareHeader failed";
            stop();
            return false;
        }
        ++p.prepared;
    }
    p.render = std::move(render);
    p.running = true;
    p.thread = std::thread([&p] { p.run(); });
    return true;
}

void Output::stop() {
    auto& p = *impl_;
    p.running = false;
    if (p.event) SetEvent(p.event);
    if (p.thread.joinable()) p.thread.join();
    if (p.device) {
        waveOutReset(p.device);
        for (std::size_t i = 0; i < p.prepared; ++i)
            waveOutUnprepareHeader(p.device, &p.headers[i], sizeof(WAVEHDR));
        p.prepared = 0;
        waveOutClose(p.device);
        p.device = nullptr;
    }
    if (p.event) { CloseHandle(p.event); p.event = nullptr; }
}

std::string Output::error() const {
    std::lock_guard<std::mutex> lock(impl_->error_mutex);
    return impl_->failure;
}
Output::Stats Output::stats() const noexcept { return {}; }

} // namespace nfsu::audio
