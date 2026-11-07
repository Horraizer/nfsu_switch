#include "nfsu/race_window.h"
#include "nfsu/race_timing.h"
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace {
LRESULT CALLBACK race_proc(HWND w,UINT m,WPARAM a,LPARAM b) {
    if (m==WM_CLOSE) { DestroyWindow(w); return 0; }
    return DefWindowProc(w,m,a,b);
}
bool down(int key) { return (GetAsyncKeyState(key)&0x8000)!=0; }
}
#endif
namespace nfsu {
bool run_race_window(const std::function<void(const GamepadFrame&)>& tick,
                     const std::function<std::vector<unsigned char>()>& render,
                     std::string& error, bool smoke) {
#ifdef _WIN32
    const auto instance=GetModuleHandle(nullptr);
    WNDCLASSA cls{}; cls.lpfnWndProc=race_proc; cls.hInstance=instance;
    cls.lpszClassName="nfsu_race_window"; cls.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if (!RegisterClassA(&cls)) { error="cannot register race window"; return false; }
    const auto window=CreateWindowA(cls.lpszClassName,"NFSU event 1",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,976,579,nullptr,nullptr,instance,nullptr);
    if (!window) { UnregisterClassA(cls.lpszClassName,instance); error="cannot create race window"; return false; }
    if (!smoke) ShowWindow(window,SW_SHOW);
    try {
        RaceStepClock clock;
        if (!make_race_step_clock(60,4,clock,error)) throw std::runtime_error(error);
        std::uint64_t ticks=0, frames=0;
        while (IsWindow(window)) {
            MSG msg;
            while (PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
            if (!IsWindow(window) || (GetForegroundWindow()==window && down(VK_ESCAPE))) break;
            const auto now=std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto count=advance_race_step_clock(clock,static_cast<std::uint64_t>(now));
            for (std::uint32_t i=0;i<count;++i) {
                GamepadFrame pad; pad.time_us=ticks++*1000000/60; pad.pads[0].connected=true;
                if (!smoke && GetForegroundWindow()==window) {
                    auto bind=[&](int key,GamepadButton button) { if (down(key)) pad.pads[0].buttons|=gamepad_button_bit(button); };
                    bind('W',GamepadButton::trigger_right); bind('S',GamepadButton::trigger_left);
                    bind(VK_SPACE,GamepadButton::face_south); bind('R',GamepadButton::select); bind('C',GamepadButton::face_north);
                    pad.pads[0].axes[0]=(down('D')?32767:0)-(down('A')?32767:0);
                }
                pad.sample_clock_available=true;
                pad.sample_time_us=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                pad.display_frame=frames;
                tick(pad);
            }
            if (count) {
                auto pixels=render();
                for (std::size_t i=0;i<pixels.size();i+=4) std::swap(pixels[i],pixels[i+2]);
                BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                bitmap.bmiHeader.biWidth=640; bitmap.bmiHeader.biHeight=-360;
                bitmap.bmiHeader.biPlanes=1; bitmap.bmiHeader.biBitCount=32; bitmap.bmiHeader.biCompression=BI_RGB;
                RECT rect{}; GetClientRect(window,&rect); const auto dc=GetDC(window);
                StretchDIBits(dc,0,0,rect.right,rect.bottom,0,0,640,360,pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
                ReleaseDC(window,dc);
                ++frames;
                if (smoke && frames==4) break;
            } else Sleep(1);
        }
    } catch (...) { if (IsWindow(window)) DestroyWindow(window); UnregisterClassA(cls.lpszClassName,instance); throw; }
    if (IsWindow(window)) DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName,instance);
    return true;
#else
    (void)tick; (void)render; (void)smoke; error="live mode requires the Windows desktop backend"; return false;
#endif
}
}
