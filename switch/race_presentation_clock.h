#pragma once
#include "nfsu/race_feng_clock.h"

namespace nfsu {
// One UI delta producer per process, shared by solo, local and LAN entry.
// Restarting an event resets Animate, while this producer keeps its residue.
inline RaceFengClock& race_presentation_clock() {
    static RaceFengClock clock;return clock;
}
}
