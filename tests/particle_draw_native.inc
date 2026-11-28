// Executed PC404110 main view1; real402D30 seed121 births, supplied half-life.
// speed.exe c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
struct NativeParticleVisual { RaceParticleEffect effect; unsigned life,remaining,rgba; double diameter; bool draw; };
inline const NativeParticleVisual kNativeParticleVisuals[]{
    {RaceParticleEffect::road_spray,670,335,0x00000000U,0,false}, // XETT_RoadSDr_SprayA
    {RaceParticleEffect::road,3937,1968,0x3d9898aeU,2.259364128112793,true}, // XETT_RoadSk_SmkA
    {static_cast<RaceParticleEffect>(unsigned(RaceParticleEffect::water_first)+13),590,295,0x1a95959aU,2.9748342633247375,true}, // XETT_DpWtrDRF_DropsFT
};
