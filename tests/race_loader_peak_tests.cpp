// Count every replaceable C++ allocation across the Switch race load phases.
// The renderer opens after HeapUsage::finish_load, so GPU blocks are excluded.
#include "nfsu/race_gpu_scene.h"
#include "nfsu/race_audio.h"
#include "nfsu/posix_file_system.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace {
std::atomic<std::size_t> live{0}, peak{0}, calls{0};
struct Header { void* raw; std::size_t bytes; };
void* allocate(std::size_t n, std::size_t alignment) {
    alignment = std::max(alignment, alignof(std::max_align_t));
    if (n > std::numeric_limits<std::size_t>::max() - alignment - sizeof(Header))
        throw std::bad_alloc();
    void* raw = std::malloc(std::max<std::size_t>(n, 1) + alignment + sizeof(Header));
    if (!raw) throw std::bad_alloc();
    const auto address = (reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header) + alignment - 1) & ~(alignment - 1);
    auto* header = reinterpret_cast<Header*>(address) - 1;
    header->raw = raw;
    header->bytes = n;
    const auto current = live.fetch_add(n) + n;
    auto previous = peak.load();
    while (current > previous && !peak.compare_exchange_weak(previous, current)) {}
    ++calls;
    return reinterpret_cast<void*>(address);
}
void release(void* p) noexcept {
    if (!p) return;
    const auto* header = static_cast<Header*>(p) - 1;
    live.fetch_sub(header->bytes);
    std::free(header->raw);
}
bool check(bool okay, const char* reason) {
    if (!okay) std::fprintf(stderr, "FAIL: %s\n", reason);
    return okay;
}
void stage(const char* name, std::size_t baseline) {
    std::printf("stage %s live %zu peak %zu allocations %zu\n",
                name, live.load() - baseline, peak.load() - baseline, calls.load());
}
}

void* operator new(std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return allocate(n, alignof(std::max_align_t)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void* operator new(std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocate(n, static_cast<std::size_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new(n, a); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { try { return ::operator new[](n, a); } catch (...) { return nullptr; } }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p); }

int main(int argc, char** argv) {
    try {
        const auto before = live.load();
        peak = before;
        auto* scalar = ::operator new(17);
        auto* array = ::operator new[](31, std::nothrow);
        auto* aligned = ::operator new(63, std::align_val_t{256}, std::nothrow);
        auto* aligned_array = ::operator new[](127, std::align_val_t{128});
        const auto during = live.load(), high = peak.load();
        const bool alignment = reinterpret_cast<std::uintptr_t>(aligned) % 256 == 0 &&
            reinterpret_cast<std::uintptr_t>(aligned_array) % 128 == 0;
        ::operator delete(scalar, std::size_t{17});
        ::operator delete[](array, std::nothrow);
        ::operator delete(aligned, std::align_val_t{256}, std::nothrow);
        ::operator delete[](aligned_array, std::size_t{127}, std::align_val_t{128});
        if (!check(during == before + 238 && high == during && live == before && alignment,
                   "allocation counter missed scalar/array/aligned/nothrow bytes")) return 1;
        if (argc == 1) {
            std::puts("PASS: race loader allocation counter sees transient allocations");
            return 0;
        }
        const bool baseline_only = argc == 3 && std::strcmp(argv[2], "--baseline") == 0;
        const bool no_audio = argc == 3 && std::strcmp(argv[2], "--no-audio") == 0;
        if (argc > 3 || (argc == 3 && !baseline_only && !no_audio)) {
            std::fprintf(stderr, "Usage: nfsu_race_loader_peak_tests [root [--baseline|--no-audio]]\n");
            return 1;
        }
        const std::filesystem::path root(argv[1]);
        nfsu::RaceGpuScene scene;
        std::string error;
        struct Observer : nfsu::TrackSliceLoadObserver {
            std::size_t baseline = 0, checkpoints = 0, last_peak = 0;
            const char* peak_phase = "none";
            void checkpoint(const char* phase) override {
                ++checkpoints;
                if (peak.load() > last_peak) {
                    last_peak = peak.load();
                    peak_phase = phase;
                }
                if (std::strcmp(phase, "path sampled") == 0 ||
                    std::strcmp(phase, "car input") == 0 ||
                    std::strcmp(phase, "car preparation") == 0 ||
                    std::strcmp(phase, "car prepared") == 0 ||
                    std::strcmp(phase, "render preparation") == 0 ||
                    std::strcmp(phase, "render templates prepared") == 0 ||
                    std::strcmp(phase, "render prepared") == 0 ||
                    std::strncmp(phase, "race ", 5) == 0)
                    stage(phase, baseline);
            }
        } observer;
        const auto baseline = live.load();
        peak = baseline;
        calls = 0;
        observer.baseline = baseline;
        observer.last_peak = baseline;
        nfsu::RaceRun race;
        if (!check(race.prepare_metadata(root, error, &observer), error.c_str())) return 1;
        if (!check(scene.load(nfsu::posix_file_system(), root, error, nullptr, &observer), error.c_str())) return 1;
        stage("scene loaded", baseline);
        std::printf("scene peak checkpoint %s, count %zu\n", observer.peak_phase, observer.checkpoints);
        if (!check(race.start(root, scene.track(), error, &observer), error.c_str())) return 1;
        stage("race started", baseline);
        nfsu::audio::RaceAudioService sound;
        std::string diagnostic;
        if (!no_audio) {
            const bool started = sound.start(root, diagnostic);
            if (!check(started || diagnostic.find("audio device unavailable") == 0,
                       diagnostic.c_str())) return 1;
        }
        stage("audio started", baseline);
        nfsu::RaceGpuFrame frame;
        if (!check(scene.frame(race.session(), frame, error), error.c_str())) return 1;
        stage("first frame", baseline);
        const auto delta = peak.load() - baseline;
        std::printf("race loader peak %zu retained %zu allocations %zu budget 50331648\n",
                    delta, live.load() - baseline, calls.load());
        if (!baseline_only && !no_audio && !check(delta <= 50331648,
            "race loader peak exceeds 48 MiB")) return 1;
        std::puts(baseline_only || no_audio ? "BASELINE measured (budget not asserted)" :
                  "PASS: every-allocation race loader peak within 48 MiB");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
