// Authenticated original42E020 and40DD00, supplied player/viewport, no hooks.
static constexpr unsigned kNativeCameraCycle[][3]={
    {0,0,1},
    {0,1,2},
    {0,2,0},
    {0,4,0},
    {1,0,1},
    {1,1,2},
    {1,2,4},
    {1,4,0},
};
static constexpr unsigned kNativeRaceProjection[][5]={
    {13020,1280,720,0x3f9c2e9bU,0xc00d8401U},
    {13020,640,480,0x3f9c2e9bU,0xbfc95262U},
    {13020,1920,1080,0x3f9c2e9bU,0xc00d8401U},
    {16384,1280,720,0x3f5aa734U,0xbfd839e0U},
    {16384,640,480,0x3f5aa734U,0xbf9419acU},
    {16384,1920,1080,0x3f5aa734U,0xbfd839e0U},
};
// 40DD00 with the PC widescreen primary-view operands (Scaling=0).
static constexpr unsigned kNativeRaceWidescreenProjection[][5]={
    {13020,1280,720,0x3f861cd4U,0xbfae0c21U},
    {13020,640,480,0x3fb2d11bU,0xbfae0c21U},
    {13020,1920,1080,0x3f861cd4U,0xbfae0c21U},
    {16384,1280,720,0x3f415df5U,0xbf6a2dc7U},
    {16384,640,480,0x3f80e94eU,0xbf6a2dc7U},
    {16384,1920,1080,0x3f415df5U,0xbf6a2dc7U},
};
static constexpr unsigned kNativeRaceCameraStart[][3]={
    {0,0,0},
    {0,1,1},
    {0,2,2},
    {0,3,0},
    {1,0,4},
    {1,1,4},
    {1,2,4},
    {1,3,4},
};
// Native 593D40 eye response, stationary far-to-near transition.
static constexpr unsigned kNativeRaceEyeResponse[][2]={
    {0,0xc0b9999aU},
    {1,0xc0b9922dU},
    {2,0xc0b97c7aU},
    {10,0xc0b7385cU},
    {30,0xc0abfe22U},
    {60,0xc09da18aU},
    {120,0xc0960dd3U},
};
// Native 48BCB0 world eye, straight motion with acceleration step.
static constexpr unsigned kNativeRaceAcceleration[][2]={
    {0,0xc0966666U},
    {20,0x3ffbbbbcU},
    {21,0x40133184U},
    {24,0x405302f8U},
    {40,0x41089aecU},
    {60,0x417098ccU},
    {120,0x420b392cU},
    {179,0x4259d9b1U},
};
