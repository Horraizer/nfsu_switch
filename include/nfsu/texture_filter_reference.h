#pragma once
#include "nfsu/render.h"
#include <cstdint>
namespace nfsu {
// Projection refreshed from original40DD00.
// #262 diagnostic native-top-level CPU reference. Desktop images use the
// same cached GPU selector, v2 bounded fixture and 1280x720 camera.
// Pose state pins include the restored neutral-first engine feedback branch.
// r90 restores native ordinary-owner grip/policy; pose/image pins refreshed.
// r91 restores complete dt0 rest refresh and native strut bump impulses.
// Pose/image references refreshed; diagnostic input identity is unchanged.
// The Stadium landing clock correction moves the step-6000 pose and images.
// Recording identity, culling and exclusive writer remain unchanged.
// Persisting wheel direction across rest refresh moves the final parked pose.
// v2 uses production physics. This migration has desktop evidence only;
// prior console evidence belongs to f61380672d48; new GPU acceptance is pending.
inline constexpr const char* kTextureFilterFixtureHash =
    "83B4D2EF5745DCF33B8E28D3C0193B5C1B5E0F59E22D55513234ED2E8B38B268";
struct TextureFilterPoseReference {
    std::uint64_t step;
    const char* sequence;
    const char* steps;
};
inline constexpr TextureFilterPoseReference kTextureFilterPoseReferences[] = {
    {180, "DFA930A0AC98469414A76E4FDB6105E7C23F1B7E2B5E97E941037233250B1DAE", "D8FEC550876459DBA99BA8FFCF9448C1F028D0D9EB24F938C5DFB49067B54249"},
    {600, "F475AD1A2FFC9B4564C0CB80152DC3EE74EDA627BA7C490FAA4DD7E63CB728DD", "4317B7F287AE437725E491CC656CC4CCB2FF44F901E2EFC02AE1B672ED25D4AB"},
    {6000, "9DF6E432AB236B703190D573A3CF078CA116FB906AE4DE3AF6976E608AF08D01", "76D66AF0E130F48E9D0CFA18041EDA71C6D954F0955F1FE23C5EC074D968CFD2"},
};
struct TextureFilterReference {
    std::uint64_t step;
    TextureFilterMode filter;
    bool shaded;
    const char* sha256;
};
inline constexpr TextureFilterReference kTextureFilterReferences[] = {
    // Original ordinary-world RGB correction; input and pose references retained.
    // The six shaded rows re-pinned for the installed-BASE_PAINT material
    // correction (review F2): the painted car shades with its own installed
    // GLOSS_L1_COLOR08 material instead of the swatch surrogate.

    {180, TextureFilterMode::legacy, false, "FBDDCB4042BA6AE50DC6F81AB12A52D188EAD8F28EE573496D02B9399693EC24"},
    {180, TextureFilterMode::linear, false, "58266C5F5514369073028952A12578A28519281C98F265C05A0C48EC2535F193"},
    {180, TextureFilterMode::legacy, true, "356C005D20CE74DF2203E2CAAFA4E3EB560AFEF0C04C753F287FC22D78B0661B"},
    {180, TextureFilterMode::linear, true, "625C67052F0372E61D24E4A6092CAD0C99493BDAC3BDCA8E955233C91F574E2D"},
    {600, TextureFilterMode::legacy, false, "B16FD689811FF0A0E0DFB4D467DF500094B957D850527B948947854D362BE0C2"},
    {600, TextureFilterMode::linear, false, "C81D0CB27F27D193299D6AC99DE341840BAB01E6AAC6C9567D0B7BEFF4D5AECB"},
    {600, TextureFilterMode::legacy, true, "C712C7825B4510E0D29D2071F28E05FB3CF691C93355B5165441541F7245E6FE"},
    {600, TextureFilterMode::linear, true, "6D0E2211DB2D30F8D38977D427EE382C31AC12AB8D5193392B36C679167D568B"},
    {6000, TextureFilterMode::legacy, false, "39973A6C1F1493A62B279CDA88B299F7369A9A7468147CD3B7FB9FF459DB6DC9"},
    {6000, TextureFilterMode::linear, false, "2AD403967F8CB7CEB5916EEE2E6FC3B8CCF704BCE59AF0929CC98E1E52C2206C"},
    {6000, TextureFilterMode::legacy, true, "3DB74BE481B953550E91140AD7B6F6A10730D8ABBC6E61035158A60DE1782096"},
    {6000, TextureFilterMode::linear, true, "51BF6D90BE95C6EE0C62738919A36918D338906B4BF7490C4FE71BE8484C7F75"},
};
}
