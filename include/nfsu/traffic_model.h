#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace nfsu {
struct TrafficModelRecord {
    int id=-1,category=0,variants=0;
    bool enabled=false;
    std::array<float,5> weights{};
    std::array<int,16> skins{};
};
struct TrafficModelChoice {
    int id=-1,skin=0;
    unsigned retries=0;
};
// Decodes only the authored fields used by57CFA0/57D000/4B3AC0.
bool decode_traffic_model_record(const unsigned char*,std::size_t,TrafficModelRecord&) noexcept;
// 57D000 / 57CFA0 and the duplicate/skin stages in 4B3AC0. Callers supply
// the original sound RNG state and the previously configured traffic IDs.
TrafficModelChoice choose_traffic_model(const TrafficModelRecord* records,
    std::size_t count,unsigned track,const int* used,std::size_t used_count,
    std::uint32_t& random) noexcept;
}
