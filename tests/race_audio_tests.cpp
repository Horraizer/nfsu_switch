#include "nfsu/race_audio.h"
#include "nfsu/audio.h"
#include "nfsu/arg_path.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void test_playlist_order() {
    nfsu::audio::Bank bank;
    bank.entries = {{1, 200, 0}, {1, 0, 0}, {1, 100, 0}};
    std::vector<nfsu::audio::Segment> indexed(3);
    indexed[0].offset = 0;
    indexed[1].offset = 100;
    indexed[2].offset = 200;
    const auto playlist = nfsu::audio::race_music_playlist(bank, indexed);
    check(playlist.size() == 2 && playlist[0].offset == 200 &&
          playlist[1].offset == 100,
          "music table order with preview segment skipped");
    bank.entries[0].value = 300;
    bool rejected = false;
    try { (void)nfsu::audio::race_music_playlist(bank, indexed); }
    catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "music bank reference must resolve");
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

// The retail root comes only from the command line (issue #233): CTest
// registers the retail run, nfsu_race_audio_retail, with the configured
// NFSU_RETAIL_ROOT, and the plain run has no root and no retail half.
void test_retail_music_prefix(const std::filesystem::path* root) {
    if (!root) {
        std::cout << "race audio: retail check not run here, no root argument "
                     "(see nfsu_race_audio_retail)\n";
        return;
    }
    check(!root->empty(), "the retail root argument is not empty");
    std::ifstream input(*root /
        "SNDSTREAMS/STRM_Music_UG0_MB.ast", std::ios::binary);
    check(bool(input), "retail music stream available");
    unsigned polls = 0;
    const auto segments = nfsu::audio::scan_ast_prefix(input,
        6 * 1024 * 1024, 2, [&] { ++polls; });
    check(segments.size() == 2 && segments[1].end == 5343716 &&
          segments[1].rate == 22050 && segments[1].channels == 2 &&
          polls >= 20, "retail music bounded index and progress");

    std::ifstream bank_input(*root /
        "Sound/STRMBANK/STRM_Music_UG0_MB.abk", std::ios::binary);
    check(bool(bank_input), "retail music bank available");
    const std::vector<std::uint8_t> bank_bytes(
        (std::istreambuf_iterator<char>(bank_input)),
        std::istreambuf_iterator<char>());
    const auto bank = nfsu::audio::parse_abk(bank_bytes);
    std::vector<std::uint64_t> offsets;
    for (const auto& entry : bank.entries)
        if (entry.type == 1) offsets.push_back(entry.value);
    std::sort(offsets.begin(), offsets.end());
    check(offsets.size() == 27 && offsets.front() == 0 &&
          std::adjacent_find(offsets.begin(), offsets.end()) == offsets.end(),
          "music bank has distinct segment offsets");
    input.clear();
    input.seekg(0, std::ios::end);
    const auto stream_size = static_cast<std::uint64_t>(input.tellg());
    std::vector<nfsu::audio::Segment> indexed;
    for (std::size_t i = 0; i < offsets.size(); ++i)
        indexed.push_back(nfsu::audio::index_ast_segment_by_boundary(input,
            offsets[i], i + 1 < offsets.size() ? offsets[i + 1] : stream_size));
    const auto fully_scanned = nfsu::audio::scan_ast(input);
    check(fully_scanned.size() == indexed.size(),
          "retail music index count matches full scan");
    for (std::size_t i = 0; i < indexed.size(); ++i)
        check(indexed[i].offset == fully_scanned[i].offset &&
              indexed[i].end == fully_scanned[i].end &&
              indexed[i].frames == fully_scanned[i].frames &&
              indexed[i].rate == fully_scanned[i].rate &&
              indexed[i].channels == fully_scanned[i].channels &&
              indexed[i].blocks == fully_scanned[i].blocks &&
              indexed[i].codec == fully_scanned[i].codec,
              "bank-boundary index matches full retail AST scan");
    check(indexed[1].offset == segments[1].offset &&
          indexed[1].end == segments[1].end &&
          indexed[1].frames == segments[1].frames &&
          indexed[16].codec == nfsu::audio::Codec::ea_xa_v1 &&
          indexed[16].rate == 44100 && indexed[26].end <= stream_size,
          "all retail music segments indexed by bank boundaries");
    const auto playlist = nfsu::audio::race_music_playlist(bank, indexed);
    check(playlist.size() == 26 && playlist.front().offset == 717824 &&
          playlist.back().offset == 143246848,
          "retail playlist follows full song entries");
    std::cout << "race audio: retail music prefix pinned\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: nfsu_race_audio_tests [RETAIL_ROOT]\n";
        return 1;
    }
    try {
        test_weights();
        test_playlist_order();
        test_missing_audio();
        std::string argument_error;
        check(nfsu::check_arguments(argc, argv, 1, argument_error),
              argument_error.c_str());
        const std::filesystem::path root =
            argc == 2 ? nfsu::argument_path(argv[1]) : std::filesystem::path();
        test_retail_music_prefix(argc == 2 ? &root : nullptr);
        std::cout << "race audio tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "race audio tests: " << e.what() << '\n';
        return 1;
    }
}
