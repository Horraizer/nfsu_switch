#pragma once
#include "nfsu/render.h"
#include <cstdint>
namespace nfsu {
// #262 diagnostic native-top-level CPU reference. Desktop images use the
// same cached GPU selector, v2 bounded fixture and 1280x720 camera.
// Pose and images refreshed after the integrated atan/tyre corrections;
// recording identity, culling checks and exclusive output writer are unchanged.
// v2 uses production physics; prior console evidence belongs to f61380672d48.
inline constexpr const char* kTextureFilterFixtureHash =
    "83B4D2EF5745DCF33B8E28D3C0193B5C1B5E0F59E22D55513234ED2E8B38B268";
struct TextureFilterPoseReference {
    std::uint64_t step;
    const char* sequence;
    const char* steps;
};
inline constexpr TextureFilterPoseReference kTextureFilterPoseReferences[] = {
    {180, "A7634037E74CAB1053E5EB7F0CA030703D33517EF81BCB08E2750434E9098A2F", "C0768FE0D7DA99E4B838283B8EE57EB1395214F45441AB522514731127BB206C"},
    {600, "1F66D76972B87DE4176D1E7BE8E1186695F095A2E6D40E332D700165772FE430", "948A7C5C1352540897F4D072B568F2B3DEC5243E86788088E66B15BC180980C9"},
    {6000, "F4CBBFFEDCCD163CC36F6361AE95E4F49EA20F793214592D45FCD70EAAAD8981", "6F9025D00F55CB2F8899DBFD29A06532E527F051ED191AF8026F39A024D581E2"},
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

    {180, TextureFilterMode::legacy, false, "BF6124E9CA7D6FC0E2ABD020570265C71A9358ACC8159BBA1424828B30EDCFED"},
    {180, TextureFilterMode::linear, false, "C81BCB127972F9043B0FDA08614A7F5079ED6A0A00114EDD2D5C959D744EA6B7"},
    {180, TextureFilterMode::legacy, true, "984AA9DD13279DA23FA8F285699F94C121A6D312046099FC913333BE754F8429"},
    {180, TextureFilterMode::linear, true, "6FC3BF31F196225A86A3898066A0B82E58DFCF98C8E847A71B4667E88001E799"},
    {600, TextureFilterMode::legacy, false, "DAFB6086D9B098961E66F1A1C79C72F3942CCE1BDC0AF4B3F3A4D8477AC6B053"},
    {600, TextureFilterMode::linear, false, "75C15F797DA25403103C97EB149433EC424CD39529F7B37F8FE6309D4022B3A1"},
    {600, TextureFilterMode::legacy, true, "EEE19FE07A2BB9ABCD19E099EA2EB380EC272C9338C3408045928011DB4D3359"},
    {600, TextureFilterMode::linear, true, "38870254A23EDADE8D39613C5B87DB42F794F0B0991408DDC407F4A6704828B0"},
    {6000, TextureFilterMode::legacy, false, "EDFA64C64FC77168BCDC8938D894793FFFF415E9BD0CD61037650EDCE6C7C00F"},
    {6000, TextureFilterMode::linear, false, "D1E6897CAACDD54C25A29F754D9B252BD2E7ACB9BC5763F26AE884C6CA34C718"},
    {6000, TextureFilterMode::legacy, true, "2C2BFC16EF96A6C4E8D186940C5912A55F4FBAEB76C9DC4485B09B854985CD37"},
    {6000, TextureFilterMode::linear, true, "5279182CE24183A355140E5888CA8D7A5BC3FC730592B2B0CD4BDC8A84909A5A"},
};
}
