#pragma once
#include "nfsu/render.h"
#include <cstdint>
namespace nfsu {
// #262 diagnostic native-top-level CPU reference. Initial desktop Release
// images: same cached GPU selector, event1 replay and 1280x720 camera.
struct TextureFilterReference {
    std::uint64_t step;
    TextureFilterMode filter;
    bool shaded;
    const char* sha256;
};
inline constexpr TextureFilterReference kTextureFilterReferences[] = {
    {180, TextureFilterMode::legacy, false, "638F7837BD15745BA175129ABD719D8F12A33CE39E4DCE6454AAD0833D298091"},
    {180, TextureFilterMode::linear, false, "73A4F4813ED673E31EC69DA50F9C88E274F2ADC3014C5E10BEECB6D51FF55A93"},
    {180, TextureFilterMode::legacy, true, "FF4DD356F50079FD5DC3AAD1340BAE46E531845D0FA9705CD88BE689B5540258"},
    {180, TextureFilterMode::linear, true, "AB682CE0AE36B3D15FE4EB46FE2EF2C48A346711B92728A5A9150348052DC777"},
    {600, TextureFilterMode::legacy, false, "D9082414DD6FE34CACACD7EBF3CE9EA0A7733A744EE0BB9EBCF24F3E217B5009"},
    {600, TextureFilterMode::linear, false, "015118C4796C55895EB1B706D1999D9C48F2A952463B897C9013B26328756185"},
    {600, TextureFilterMode::legacy, true, "357778D0C44B1084D00516DAF4670BDCBE88B0E33D8C8D2321A2A34895B2653F"},
    {600, TextureFilterMode::linear, true, "46F1D4CBD7F51764627DCD3E275D65F882334F7BB357FD9D1F80EF08548264ED"},
    {6000, TextureFilterMode::legacy, false, "5250D4DF86EEB493A70166818857E7EC99E6EF704F0EF235C255321FFC8494B6"},
    {6000, TextureFilterMode::linear, false, "0383B3BC43DDD9F604B210E1F2B48EA0C788CA13D88B6FFD0F76A8F57FBD69FC"},
    {6000, TextureFilterMode::legacy, true, "67DD1B44BEBC5B0037A11E4209F03F84FC4DDF192D6A3A21447B3E32E68AEBC1"},
    {6000, TextureFilterMode::linear, true, "CE23451009B81CC3693E1B292310406A99DA7ABA1DB3EF712B1D424EFDF9576A"},
};
}
