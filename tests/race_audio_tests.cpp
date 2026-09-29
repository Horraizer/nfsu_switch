#include "nfsu/race_audio.h"
#include "nfsu/audio.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_weights() {
    const auto low = nfsu::audio::engine_layer_weights(1000);
    check(std::abs(low[0] - 15.0 / 17.0) < 1e-12 &&
          std::abs(low[1] - 2.0 / 17.0) < 1e-12 &&
          low[2] == 0 && low[3] == 0, "engine crossfade direction");
    const auto high = nfsu::audio::engine_layer_weights(6000);
    check(high[0] == 0 && high[1] == 0 &&
          std::abs(high[2] - 0.4) < 1e-12 &&
          std::abs(high[3] - 0.6) < 1e-12,
          "engine high crossfade direction");
}

void test_missing_audio() {
    nfsu::audio::RaceAudioService service;
    std::string diagnostic;
    check(!service.start("nfsu-audio-missing-fixture-835148", diagnostic),
          "missing audio must not start");
    check(diagnostic.find("music: missing AST") != std::string::npos &&
          service.diagnostic() == diagnostic,
          "missing music fallback and visible diagnostic");
}

void test_retail_music_prefix() {
    const char* root = std::getenv("NFSU_RETAIL_ROOT");
    if (!root || !*root) return;
    std::ifstream input(std::filesystem::path(root) /
        "SNDSTREAMS/STRM_Music_UG0_MB.ast", std::ios::binary);
    check(bool(input), "retail music stream available");
    unsigned polls = 0;
    const auto segments = nfsu::audio::scan_ast_prefix(input,
        6 * 1024 * 1024, 2, [&] { ++polls; });
    check(segments.size() == 2 && segments[1].end == 5343716 &&
          segments[1].rate == 22050 && segments[1].channels == 2 &&
          polls >= 20, "retail music bounded index and progress");
}

} // namespace

int main() {
    try {
        test_weights();
        test_missing_audio();
        test_retail_music_prefix();
        std::cout << "race audio tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "race audio tests: " << e.what() << '\n';
        return 1;
    }
}
