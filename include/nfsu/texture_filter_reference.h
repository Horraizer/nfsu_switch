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
    // The six shaded rows re-pinned for the installed-BASE_PAINT material
    // correction (review F2): the painted car shades with its own installed
    // GLOSS_L1_COLOR08 material instead of the swatch surrogate.

    {180, TextureFilterMode::legacy, false, "5EF5C2161BFEDB871AE859BADEA5591562A23C0D4F7C241CE7E7E701068E1F6C"},
    {180, TextureFilterMode::linear, false, "5F063D7AFA5F084BC5BBC42BA18768404D395751D9B7F53365651B07AF5750BB"},
    {180, TextureFilterMode::legacy, true, "E4A4F165C312CF72379BFB096CEDEB4AB82279162F5CC6AC6C3B9BEC7E6F20D7"},
    {180, TextureFilterMode::linear, true, "5E56E690DBA87A84C090077892EAB1E9DB770C431E1CC8E580DF0D25B057F794"},
    {600, TextureFilterMode::legacy, false, "DD8FB5A0B64AAA448F5825EE5395C7D2497581AB400A4F8FAC5CA49D25656CAC"},
    {600, TextureFilterMode::linear, false, "48840F50BA0B1488014ACC2030C36102B4497EEF9131B805191D7609B30EE825"},
    {600, TextureFilterMode::legacy, true, "5526C333D200473330B681BCBCB64AD51E1BE55DA5628FF72D3DC1C45CB04745"},
    {600, TextureFilterMode::linear, true, "AF46E9A7330CF33EF30E5F7870EF2A0AFE0E69B480CF314C657B93EF152A85A1"},
    {6000, TextureFilterMode::legacy, false, "C536F2862850D580F15EC4ED27C648D354CDF07FAFA82DC85667800AD287BA24"},
    {6000, TextureFilterMode::linear, false, "BE7FEB49BA6AE04D78FB22351F79613C381AD7723B9E2922AFA9F5A7F20A520A"},
    {6000, TextureFilterMode::legacy, true, "5E28F34FDDB90655167F5460F49013829040AEDEE57B416B3742768703081AE8"},
    {6000, TextureFilterMode::linear, true, "4478A0D50436271CD07E18180804AD6433432580189D59EBBAB9BFE8F442EFB3"},
};
}
