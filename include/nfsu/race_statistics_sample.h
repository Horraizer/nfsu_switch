#pragma once
#include "nfsu/race_timing.h"
#include <array>

namespace nfsu {
// UI statistics captured at authoritative physics steps. No physics state,
// random generator, driver plan or campaign accounting is stored here.
struct RaceStatisticsSample {
    bool done=false;
    double top=0,distance=0,seconds=0;
    std::array<unsigned,kRaceLapSlots> positions{};
    std::array<float,kRaceLapSlots> scores{};
    float last_score=0;
};
}
