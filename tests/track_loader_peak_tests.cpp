// Every replaceable C++ allocation is counted, including transient ones
// between loader polls. Header bookkeeping uses malloc, never operator new.
#include "nfsu/track_slice.h"
#include "nfsu/arg_path.h"
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
    alignment = std::max(alignment,alignof(std::max_align_t));
    if (n > std::numeric_limits<std::size_t>::max()-alignment-sizeof(Header)) throw std::bad_alloc();
    void* raw = std::malloc(std::max<std::size_t>(n,1)+alignment+sizeof(Header));
    if (!raw) throw std::bad_alloc();
    const auto address = (reinterpret_cast<std::uintptr_t>(raw)+sizeof(Header)+alignment-1)&~(alignment-1);
    auto* header = reinterpret_cast<Header*>(address)-1;
    header->raw = raw; header->bytes = n;
    const auto current = live.fetch_add(n)+n;
    auto previous = peak.load();
    while (current > previous && !peak.compare_exchange_weak(previous,current)) {}
    ++calls;
    return reinterpret_cast<void*>(address);
}
void release(void* p) noexcept {
    if (!p) return;
    const auto* h = static_cast<Header*>(p)-1;
    live.fetch_sub(h->bytes); std::free(h->raw);
}
bool check(bool okay, const char* reason) {
    if (!okay) std::fprintf(stderr,"FAIL: %s\n",reason);
    return okay;
}
}
void* operator new(std::size_t n) { return allocate(n,alignof(std::max_align_t)); }
void* operator new[](std::size_t n) { return allocate(n,alignof(std::max_align_t)); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p,std::size_t) noexcept { release(p); }
void operator delete[](void* p,std::size_t) noexcept { release(p); }
void* operator new(std::size_t n,std::align_val_t a) { return allocate(n,static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n,std::align_val_t a) { return allocate(n,static_cast<std::size_t>(a)); }
void operator delete(void* p,std::align_val_t) noexcept { release(p); }
void operator delete[](void* p,std::align_val_t) noexcept { release(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { release(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { release(p); }
void* operator new(std::size_t n,const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p,const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { release(p); }
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try { return ::operator new(n,a); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try { return ::operator new[](n,a); } catch (...) { return nullptr; } }
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept { release(p); }

int main(int argc, char** argv) {
    try {
        const auto before = live.load(); peak = before;
        auto* scalar = ::operator new(17);
        auto* array = ::operator new[](31,std::nothrow);
        auto* aligned = ::operator new(63,std::align_val_t{256},std::nothrow);
        auto* aligned_array = ::operator new[](127,std::align_val_t{128});
        const auto during = live.load(), high = peak.load();
        const bool alignment = reinterpret_cast<std::uintptr_t>(aligned)%256 == 0 &&
            reinterpret_cast<std::uintptr_t>(aligned_array)%128 == 0;
        ::operator delete(scalar,std::size_t{17}); ::operator delete[](array,std::nothrow);
        ::operator delete(aligned,std::align_val_t{256},std::nothrow);
        ::operator delete[](aligned_array,std::size_t{127},std::align_val_t{128});
        if (!check(during == before+238 && high == during && peak == high && live == before && alignment,
                   "scalar/array/aligned/nothrow counters and transient peak")) return 1;
        if (argc == 1) { std::puts("PASS: allocation counter sees transient scalar/array/aligned/nothrow bytes"); return 0; }
        if (argc > 3 || (argc == 3 && std::strcmp(argv[2],"--baseline") != 0)) {
            std::fprintf(stderr,"Usage: nfsu_track_loader_peak_tests [root [--baseline]]\n"); return 1;
        }
        std::string error;
        if (!check(nfsu::check_arguments(argc,argv,1,error),error.c_str())) return 1;
        const std::filesystem::path root = nfsu::argument_path(argv[1]);
        nfsu::TrackSlice slice;
        struct Observer : nfsu::TrackSliceLoadObserver {
            std::size_t baseline = 0, checkpoints = 0;
            char previous[128]{};
            void checkpoint(const char* phase) override {
                ++checkpoints;
                if (std::strcmp(previous,phase) == 0) return;
                std::snprintf(previous,sizeof previous,"%s",phase);
                std::printf("phase %s live %zu peak %zu\n",phase,live.load()-baseline,peak.load()-baseline);
            }
        } observer;
        observer.baseline = live.load(); peak = observer.baseline; calls = 0;
        const bool okay = nfsu::load_track_slice(nfsu::posix_file_system(),root,slice,error,nullptr,&observer);
        const auto peak_delta = peak.load()-observer.baseline;
        const auto retained = live.load()-observer.baseline;
        std::printf("loader peak %zu retained %zu allocations %zu checkpoints %zu budget 50331648\n",
                    peak_delta,retained,calls.load(),observer.checkpoints);
        if (!check(okay,error.c_str())) return 1;
        if (argc != 3 && !check(peak_delta <= 50331648,"loader peak exceeds 48 MiB")) return 1;
        std::puts(argc == 3 ? "BASELINE measured (budget not asserted)" : "PASS: every-allocation loader peak within 48 MiB");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
