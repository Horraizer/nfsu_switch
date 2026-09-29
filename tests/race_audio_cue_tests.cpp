#include "nfsu/race_audio.h"

#include <iostream>
#include <stdexcept>

using nfsu::audio::RaceAudioSnapshot;
using nfsu::audio::RaceCueScheduler;
using nfsu::audio::RaceInterfaceCue;

namespace {
void check(RaceInterfaceCue actual, RaceInterfaceCue wanted, const char* message) {
    if (actual != wanted) throw std::runtime_error(message);
}
}

int main() {
    try {
        RaceCueScheduler cues;
        RaceAudioSnapshot state;
        check(cues.update(state), RaceInterfaceCue::none, "initial state");
        for (const int value : {3, 2, 1}) {
            state.countdown = value;
            check(cues.update(state), RaceInterfaceCue::countdown, "countdown edge");
            check(cues.update(state), RaceInterfaceCue::none, "no repeated countdown");
        }
        state.countdown = 4;
        check(cues.update(state), RaceInterfaceCue::go, "go edge");
        state.countdown = 0;
        check(cues.update(state), RaceInterfaceCue::none, "clear countdown");
        state.completed_laps = 1;
        check(cues.update(state), RaceInterfaceCue::lap, "lap edge");
        check(cues.update(state), RaceInterfaceCue::none, "no repeated lap");
        state.completed_laps = 2;
        state.finished = true;
        check(cues.update(state), RaceInterfaceCue::finish,
              "finish replaces final lap cue");
        check(cues.update(state), RaceInterfaceCue::none, "no repeated finish");
        std::cout << "race audio cue tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "race audio cue tests: " << e.what() << '\n';
        return 1;
    }
}
