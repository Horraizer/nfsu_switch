#include "nfsu/audio_output.h"
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate>
void wait_for(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(predicate(), message);
}
}

int main() {
    try {
        nfsu::audio::Output output;
        std::string error;
        std::atomic<unsigned> rendered{0};
        auto callback = [&](std::int16_t* pcm, std::size_t frames) {
            std::fill_n(pcm, frames * 2, std::int16_t{0});
            ++rendered;
        };
        for (unsigned round = 0; round < 3; ++round) {
            const auto before = probe_submitted.load();
            const auto before_render = rendered.load();
            error.clear();
            require(output.start(callback, error), "start failed");
            wait_for([&] { return probe_submitted.load() >= before + 3 && output.stats().queued_frames == 3072; },
                     "restart did not submit all three buffers");
            require(error.empty() && output.error().empty(), "stale session error");
            require(probe_submitted.load() - before == 3, "wrong submission count");
            require(rendered.load() - before_render == 3, "wrong callback count");
            // Buffers stay Playing/Queued/Waiting; a new driver cannot retire them.
            std::string duplicate;
            require(!output.start(callback, duplicate), "accepted duplicate start");
            output.stop();
            if (round == 2) require(output.stats().queued_frames == 0, "stopped output still queued");
            output.stop();
        }
        probe_fail_update = true;
        require(output.start(callback, error), "failure session did not start");
        wait_for([&] { return output.error() == "audrvUpdate failed"; },
                 "backend failure not surfaced");
        output.stop();
        require(output.error() == "audrvUpdate failed", "stop discarded failure evidence");
        probe_fail_update = false;
        const auto before = probe_submitted.load();
        require(output.start(callback, error), "backend failure recovery start failed");
        wait_for([&] { return probe_submitted.load() >= before + 3 && output.stats().queued_frames == 3072; },
                 "backend failure recovery silent");
        require(output.error().empty() && error.empty(), "backend error survived restart");
        require(probe_submitted.load() - before == 3, "backend recovery did not queue");
        output.stop();
        probe_fail_initialize = true;
        require(!output.start(callback, error), "initialization failure accepted");
        require(error == "audrenInitialize failed", "initialization failure missing");
        probe_fail_initialize = false;
        require(output.start(callback, error), "initialization recovery failed");
        wait_for([&] { return probe_submitted.load() >= before + 3 && output.stats().queued_frames == 3072; },
                 "initialization recovery silent");
        require(error.empty() && output.error().empty(), "initialization error survived");
        output.stop();
        std::cout << "PASS actual Switch Output restart, backend failure and initialization recovery\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}