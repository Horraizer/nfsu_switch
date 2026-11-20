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
struct RaceStartupRandom {
    std::uint32_t sound_seed;
    std::uint32_t before_world;
    std::uint32_t next_global;
};
// Settings and four opponent-name draws before an authored Underground grid.
std::uint32_t authored_race_roster_seed(std::uint32_t before_settings) noexcept;
// 51CE40 copies the old global state to sound. 41F7C9 takes the next draw.
RaceStartupRandom race_startup_random(std::uint32_t after_traffic_roster) noexcept;
// Decodes only the authored fields used by57CFA0/57D000/4B3AC0.
bool decode_traffic_model_record(const unsigned char*,std::size_t,TrafficModelRecord&) noexcept;
// 57D000 / 57CFA0 and the duplicate/skin stages in 4B3AC0. Callers supply
// the original global RNG state and the previously configured traffic IDs.
TrafficModelChoice choose_traffic_model(const TrafficModelRecord* records,
    std::size_t count,unsigned track,const int* used,std::size_t used_count,
    std::uint32_t& random) noexcept;
}
