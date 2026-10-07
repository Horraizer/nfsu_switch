#pragma once
#include "nfsu/render.h"
#include <cstdint>
namespace nfsu {
// #262 diagnostic native-top-level CPU reference. Initial desktop Release
// images: same cached GPU selector, v2 bounded fixture and 1280x720 camera.
// v2 uses production physics; prior console evidence belongs to f61380672d48.
inline constexpr const char* kTextureFilterFixtureHash =
    "83B4D2EF5745DCF33B8E28D3C0193B5C1B5E0F59E22D55513234ED2E8B38B268";
struct TextureFilterPoseReference {
    std::uint64_t step;
    const char* sequence;
    const char* steps;
};
inline constexpr TextureFilterPoseReference kTextureFilterPoseReferences[] = {
    {180, "40F4F8AC1AEA39189587270ADC2901DF05EEF218C3916B577B629DB9E29F1DF6", "0B42F895D4C4AE6B7FD25E754C778A5BF2AD4060DC8CB9BA48A44E51B3E86986"},
    {600, "D42C1C33D7B7F464BF61F14933ACA469D0F0424F0C7EDE435E3F16145EF441CD", "33EABD0F1FFE62F4E9BC1854E15DABBC40E29648BAED58B316ADA974C80BD517"},
    {6000, "DFF49CF02269671A617137815598FBC106D913B447EA95332F430CD22C8F25FE", "4C0CDC6004000D4F981405D375A8EC6020D43787EAC8F636976E3BD724F96387"},
};
struct TextureFilterReference {
    std::uint64_t step;
    TextureFilterMode filter;
    bool shaded;
    const char* sha256;
};
inline constexpr TextureFilterReference kTextureFilterReferences[] = {
    // The six shaded rows re-pinned for the installed-BASE_PAINT material
    // correction (review F2): the painted car shades with its own installed
    // GLOSS_L1_COLOR08 material instead of the swatch surrogate.

    {180, TextureFilterMode::legacy, false, "638F7837BD15745BA175129ABD719D8F12A33CE39E4DCE6454AAD0833D298091"},
    {180, TextureFilterMode::linear, false, "73A4F4813ED673E31EC69DA50F9C88E274F2ADC3014C5E10BEECB6D51FF55A93"},
    {180, TextureFilterMode::legacy, true, "A3A5E51E682F2CC5A39FDD2376D055B2CA8FB797E85C20CB154A791358E6D59A"},
    {180, TextureFilterMode::linear, true, "11FA182A18AE04BBDEB0C35B8EF63043D1F0F79B28DC6373066DA406AFFE7FBD"},
    {600, TextureFilterMode::legacy, false, "D9082414DD6FE34CACACD7EBF3CE9EA0A7733A744EE0BB9EBCF24F3E217B5009"},
    {600, TextureFilterMode::linear, false, "015118C4796C55895EB1B706D1999D9C48F2A952463B897C9013B26328756185"},
    {600, TextureFilterMode::legacy, true, "2DEED6B20ECBC8041EF9C89129F2821BF3B9956C8F42A47B09C5EC44A603EBFA"},
    {600, TextureFilterMode::linear, true, "90B17A6C3B1D932C711096166D564B11FFC6A5A1B65C3E086533988A4F222D46"},
    {6000, TextureFilterMode::legacy, false, "ED7E45C2A14DF0D9DFE8048D8F233732A394F3AF808F259890DA321EEC39DA1E"},
    {6000, TextureFilterMode::linear, false, "9D5BE0B2688523BFC30602080ADDFECEF4A68C418B40BFB013CD96B870102980"},
    {6000, TextureFilterMode::legacy, true, "2FBA89D2B37BD460ED54FFE604704F027D836D403460E3B50A2A187A4FE41D70"},
    {6000, TextureFilterMode::linear, true, "574B7CC610A921CDF7FBC78994F16393F338E2F484679740E4BA2C53510AF813"},
};
}
