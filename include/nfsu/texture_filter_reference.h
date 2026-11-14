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
    {180, "716E45542B004EAD69A13979834AF00C7383D8D2BE66A2FB7936EBA715D6BC34", "DFAFF82132582E23E51148B68BD26E73DED235272BBCCC8C20B886B258857048"},
    {600, "3AE38276C76DB8A456BA09C14A240E012A1DD8AC960D0A1A487B16A39BAAF5CF", "A19978E07AB2CF0FB4F60CF721517900F410D0E657A17D3719929E040F1A3856"},
    {6000, "C6D66488C44FC26A2EDA8DF58BD4A15E84FEAF0728EBB1894EE71CDBC8CD3872", "2CD947164989A9E4991FE560B9D7F2DB0658CC08BB16946F0F7F89092A6684E5"},
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
    {6000, TextureFilterMode::legacy, false, "BCC771B11BBB864EA5A85710A62918062565E22F14E6FAAEAA5DFC5389E0F6E7"},
    {6000, TextureFilterMode::linear, false, "34BECA2C90DC6004352EA8A4B68614337ECE9F4B6A4C79337F26AA3D29404BC7"},
    {6000, TextureFilterMode::legacy, true, "A30ABE080E8BCE56405881667B78B0360493FDF92D5262B59ADB9A0B1EBB6041"},
    {6000, TextureFilterMode::linear, true, "FCF5BA3BE9529BE0C0FB89F1B275BE8B8EE3BB70E9E60E6C5E0B995179F015C6"},
};
}
