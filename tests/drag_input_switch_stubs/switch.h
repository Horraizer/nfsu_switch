#pragma once
#include <cstddef>
#include <cstdint>
using u32=std::uint32_t;
using s32=std::int32_t;
using Result=std::uint32_t;
using u64=std::uint64_t;
enum HidNpadIdType {HidNpadIdType_No1=0,HidNpadIdType_No2=1,HidNpadIdType_Handheld=32};
enum HidNpadStyleTag {HidNpadStyleTag_NpadFullKey=1,HidNpadStyleTag_NpadHandheld=2,
      HidNpadStyleTag_NpadJoyDual=4,HidNpadStyleTag_NpadJoyLeft=8,
      HidNpadStyleTag_NpadJoyRight=16};
enum {HidNpadAttribute_IsConnected=1,HidNpadAttribute_IsRightConnected=16,
      HidSixAxisSensorAttribute_IsConnected=1};
enum : u64 {HidNpadButton_L=1ULL<<6,HidNpadButton_R=1ULL<<7,HidNpadButton_StickL=1ULL<<4};
struct HidSixAxisSensorHandle {unsigned index=0;};
struct HidSixAxisSensorState {
    u64 sampling_number=0;
    struct Vector {float x=0,y=0,z=0;} acceleration{},angle{};
    u32 attributes=0;
};
inline bool R_SUCCEEDED(Result r){return r==0;}
inline bool R_FAILED(Result r){return !R_SUCCEEDED(r);}
Result hidGetSixAxisSensorHandles(HidSixAxisSensorHandle*,s32,HidNpadIdType,HidNpadStyleTag);
Result hidStartSixAxisSensor(HidSixAxisSensorHandle);
Result hidStopSixAxisSensor(HidSixAxisSensorHandle);
std::size_t hidGetSixAxisSensorStates(HidSixAxisSensorHandle,HidSixAxisSensorState*,std::size_t);
struct HidAnalogStickState {std::int32_t x=0,y=0;};
struct HidNpadCommonState {
    u64 sampling_number=0,buttons=0;
    HidAnalogStickState analog_stick_l{},analog_stick_r{};
    u32 attributes=HidNpadAttribute_IsConnected,reserved=0;
};
using HidNpadFullKeyState=HidNpadCommonState;
using HidNpadHandheldState=HidNpadCommonState;
using HidNpadJoyDualState=HidNpadCommonState;
using HidNpadJoyLeftState=HidNpadCommonState;
using HidNpadJoyRightState=HidNpadCommonState;
struct PadState {
    std::uint8_t active_id_mask=0;
    bool active_handheld=true;
    u32 style_set=HidNpadStyleTag_NpadHandheld,attributes=HidNpadAttribute_IsConnected;
    u64 buttons_cur=0,buttons_down=0;
    HidAnalogStickState sticks[2]{};
};
inline bool padIsConnected(const PadState* p){return (p->attributes&HidNpadAttribute_IsConnected)!=0;}
inline u32 padGetStyleSet(const PadState* p){return p->style_set;}
inline u32 padGetAttributes(const PadState* p){return p->attributes;}
inline u64 padGetButtonsDown(const PadState* p){return p->buttons_down;}
inline u64 padGetButtons(const PadState* p){return p->buttons_cur;}
inline HidAnalogStickState padGetStickPos(const PadState* p,unsigned i){return p->sticks[i];}
std::size_t hidGetNpadStatesFullKey(HidNpadIdType,HidNpadFullKeyState*,std::size_t);
std::size_t hidGetNpadStatesHandheld(HidNpadIdType,HidNpadHandheldState*,std::size_t);
std::size_t hidGetNpadStatesJoyDual(HidNpadIdType,HidNpadJoyDualState*,std::size_t);
std::size_t hidGetNpadStatesJoyLeft(HidNpadIdType,HidNpadJoyLeftState*,std::size_t);
std::size_t hidGetNpadStatesJoyRight(HidNpadIdType,HidNpadJoyRightState*,std::size_t);
