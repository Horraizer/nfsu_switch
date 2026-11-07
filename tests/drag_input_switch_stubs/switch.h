#pragma once
#include <cstddef>
#include <cstdint>
using u32=std::uint32_t;
using u64=std::uint64_t;
enum HidNpadIdType {HidNpadIdType_No1=0,HidNpadIdType_Handheld=32};
enum {HidNpadStyleTag_NpadFullKey=1,HidNpadStyleTag_NpadHandheld=2,
      HidNpadStyleTag_NpadJoyDual=4,HidNpadStyleTag_NpadJoyLeft=8,
      HidNpadStyleTag_NpadJoyRight=16,HidNpadAttribute_IsConnected=1};
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
    u64 buttons_cur=0;
    HidAnalogStickState sticks[2]{};
};
inline bool padIsConnected(const PadState* p){return (p->attributes&HidNpadAttribute_IsConnected)!=0;}
inline u64 padGetButtons(const PadState* p){return p->buttons_cur;}
inline HidAnalogStickState padGetStickPos(const PadState* p,unsigned i){return p->sticks[i];}
std::size_t hidGetNpadStatesFullKey(HidNpadIdType,HidNpadFullKeyState*,std::size_t);
std::size_t hidGetNpadStatesHandheld(HidNpadIdType,HidNpadHandheldState*,std::size_t);
std::size_t hidGetNpadStatesJoyDual(HidNpadIdType,HidNpadJoyDualState*,std::size_t);
std::size_t hidGetNpadStatesJoyLeft(HidNpadIdType,HidNpadJoyLeftState*,std::size_t);
std::size_t hidGetNpadStatesJoyRight(HidNpadIdType,HidNpadJoyRightState*,std::size_t);
