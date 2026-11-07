#include "nfsu/inspect_window.h"

#include "nfsu/inspect_session.h"
#include "nfsu/platform.h"
#include "nfsu/render.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

// The Win32 window backend. It uses user32 and gdi32 only: no external
// dependency, no graphics API and no shader, because the frame it shows is
// produced by the CPU reference renderer and blitted as a device-independent
// bitmap. Its whole job is to be three of the services in
// include/nfsu/platform.h for one window - the display that puts a composed
// frame and its overlay text on the screen, the input that turns window
// messages into InputEvent values, and the lifecycle that runs the message
// loop - and to hand every message to the InspectionSession as it arrives.
// Every rule about what a control does lives in src/inspect_session.cpp and
// src/inspect.cpp; docs/PLATFORM_SERVICES.md says which call moved where.

namespace nfsu {

namespace {

constexpr const char* kWindowClass = "nfsu_inspection_window";
// Overlay box geometry, in pixels of the client area.
constexpr int kOverlayMargin = 8;
constexpr int kOverlayPadding = 6;

// The session counts wheel movement in the unit Windows reports it in, so
// WM_MOUSEWHEEL's delta reaches it unscaled.
static_assert(WHEEL_DELTA == kWheelNotch, "InputEvent::wheel is in units of WHEEL_DELTA");

std::string ascii_title(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        out += (byte >= 0x20U && byte <= 0x7EU) ? ch : '?';
    }
    return out.empty() ? std::string("nfsu inspector") : out;
}

void draw_overlay(HDC dc, const std::vector<std::string>& overlay, int width, int height) {
    if (overlay.empty()) {
        return;
    }
    HGDIOBJ font = GetStockObject(ANSI_FIXED_FONT);
    HGDIOBJ previous_font = SelectObject(dc, font);
    TEXTMETRICA metrics;
    std::memset(&metrics, 0, sizeof(metrics));
    GetTextMetricsA(dc, &metrics);
    const int line_height = metrics.tmHeight + 2;
    int box_width = 0;
    for (const std::string& line : overlay) {
        SIZE size;
        if (GetTextExtentPoint32A(dc, line.c_str(), static_cast<int>(line.size()), &size)) {
            box_width = std::max<int>(box_width, size.cx);
        }
    }
    const int box_height =
        line_height * static_cast<int>(overlay.size()) + kOverlayPadding * 2;
    RECT box;
    box.left = kOverlayMargin;
    box.top = kOverlayMargin;
    box.right = std::min(width - kOverlayMargin,
                         kOverlayMargin + box_width + kOverlayPadding * 2);
    box.bottom = std::min(height - kOverlayMargin, kOverlayMargin + box_height);
    if (box.right > box.left && box.bottom > box.top) {
        HBRUSH brush = CreateSolidBrush(RGB(16, 18, 22));
        if (brush != nullptr) {
            FillRect(dc, &box, brush);
            DeleteObject(brush);
        }
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(226, 232, 240));
    int y = kOverlayMargin + kOverlayPadding;
    for (const std::string& line : overlay) {
        if (y + line_height > height) {
            break;
        }
        TextOutA(dc, kOverlayMargin + kOverlayPadding, y, line.c_str(),
                 static_cast<int>(line.size()));
        y += line_height;
    }
    SelectObject(dc, previous_font);
}

// The display: a paint of the client area. begin_frame and end_frame are
// BeginPaint and EndPaint, so a frame is only ever presented from WM_PAINT,
// and draw_frame is the double-buffered stretch-and-text it always was.
class Win32Display final : public DisplayService {
public:
    HWND window = nullptr;

    void surface_size(int& width, int& height) override {
        RECT client;
        GetClientRect(window, &client);
        width = client.right - client.left;
        height = client.bottom - client.top;
    }

    bool begin_frame(int& width, int& height) override {
        dc_ = BeginPaint(window, &paint_);
        if (dc_ == nullptr) {
            // BeginPaint failed, so EndPaint has nothing to end and the
            // update region is still there. Clear it by hand: leaving it
            // would have Windows post WM_PAINT again at once, forever.
            ValidateRect(window, nullptr);
            return false;
        }
        RECT client;
        GetClientRect(window, &client);
        width_ = client.right - client.left;
        height_ = client.bottom - client.top;
        width = width_;
        height = height_;
        return true;
    }

    void draw_frame(const DisplayImage& image, const std::vector<std::string>& overlay) override {
        // One memory bitmap for the whole client area: the stretch and the
        // text land there and reach the screen in a single blit, so the
        // overlay never flickers over a half-drawn frame.
        HDC memory = CreateCompatibleDC(dc_);
        HBITMAP bitmap = memory != nullptr ? CreateCompatibleBitmap(dc_, width_, height_)
                                           : nullptr;
        if (memory != nullptr && bitmap != nullptr) {
            HGDIOBJ previous = SelectObject(memory, bitmap);
            BITMAPINFO info;
            std::memset(&info, 0, sizeof(info));
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = static_cast<LONG>(image.width);
            // Negative: the rows are top down, as the renderer writes them.
            info.bmiHeader.biHeight = -static_cast<LONG>(image.height);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(memory, COLORONCOLOR);
            StretchDIBits(memory, 0, 0, width_, height_, 0, 0,
                          static_cast<int>(image.width), static_cast<int>(image.height),
                          image.bgra, &info, DIB_RGB_COLORS, SRCCOPY);
            draw_overlay(memory, overlay, width_, height_);
            BitBlt(dc_, 0, 0, width_, height_, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            DeleteDC(memory);
        }
    }

    void end_frame() override {
        EndPaint(window, &paint_);
        dc_ = nullptr;
    }

    void request_redraw() override { InvalidateRect(window, nullptr, FALSE); }

private:
    PAINTSTRUCT paint_{};
    HDC dc_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// The input: the window procedure queues each input message here as an
// InputEvent and the session takes it at once. Capture is the window
// capture a drag has always taken.
class Win32Input final : public InputService {
public:
    HWND window = nullptr;
    std::deque<InputEvent> queue;

    bool next_event(InputEvent& event) override {
        if (queue.empty()) {
            return false;
        }
        event = queue.front();
        queue.pop_front();
        return true;
    }

    void set_pointer_capture(bool captured) override {
        if (captured) {
            SetCapture(window);
        } else {
            ReleaseCapture();
        }
    }

    // No controller backend exists on the desktop (docs/INPUT.md), so every
    // slot reads as disconnected. Nothing on this backend samples it: the
    // window runs no frame loop.
    void sample_gamepads(GamepadFrame& frame) override {
        for (GamepadState& pad : frame.pads) {
            pad = GamepadState{};
        }
    }
};

// The lifecycle: the message loop. One pump is one message waited for,
// translated and dispatched; the loop ends on WM_QUIT, which the window
// posts when it is destroyed. An exit request is the WM_CLOSE a user
// closing the window sends.
class Win32Lifecycle final : public LifecycleService {
public:
    HWND window = nullptr;

    bool pump() override {
        if (stopped_) {
            return false;
        }
        MSG message;
        if (GetMessageA(&message, nullptr, 0, 0) <= 0) {
            stopped_ = true;
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageA(&message);
        return true;
    }

    void request_exit() override { PostMessageA(window, WM_CLOSE, 0, 0); }

private:
    bool stopped_ = false;
};

// What GWLP_USERDATA points at while the window runs.
struct Backend {
    Win32Display display;
    Win32Input input;
    Win32Lifecycle lifecycle;
    InspectionSession* session = nullptr;
};

Backend* backend_of(HWND window) {
    return reinterpret_cast<Backend*>(GetWindowLongPtrA(window, GWLP_USERDATA));
}

// Which Key a virtual key is. What each one does is the session's key
// table; this only names the keys that table knows.
Key key_of(WPARAM key) {
    switch (key) {
    case '1': return Key::digit_1;
    case '2': return Key::digit_2;
    case '3': return Key::digit_3;
    case '4': return Key::digit_4;
    case '5': return Key::digit_5;
    case 'B': return Key::b;
    case 'C': return Key::c;
    case 'F': return Key::f;
    case 'G': return Key::g;
    case 'H': return Key::h;
    case 'M': return Key::m;
    case 'N': return Key::n;
    case 'O': return Key::o;
    case 'P': return Key::p;
    case 'Q': return Key::q;
    case 'R': return Key::r;
    case 'S': return Key::s;
    case 'T': return Key::t;
    case 'U': return Key::u;
    case 'V': return Key::v;
    case 'X': return Key::x;
    case VK_ESCAPE: return Key::escape;
    default: return Key::other;
    }
}

// The message table: which window message is which input event. False for
// a message that is not input.
bool event_of(UINT message, WPARAM w, LPARAM l, InputEvent& event) {
    switch (message) {
    case WM_SIZE:
        event = resize_event(static_cast<std::int32_t>(LOWORD(l)),
                             static_cast<std::int32_t>(HIWORD(l)));
        return true;
    case WM_LBUTTONDOWN:
        event = pointer_down_event(PointerButton::primary, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return true;
    case WM_RBUTTONDOWN:
        event = pointer_down_event(PointerButton::secondary, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return true;
    case WM_MBUTTONDOWN:
        event = pointer_down_event(PointerButton::middle, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return true;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        event = pointer_up_event(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return true;
    case WM_CAPTURECHANGED:
        event = pointer_cancel_event();
        return true;
    case WM_MOUSEMOVE:
        event = pointer_move_event(GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return true;
    case WM_MOUSEWHEEL:
        event = wheel_event(GET_WHEEL_DELTA_WPARAM(w));
        return true;
    case WM_KEYDOWN:
        event = key_event(key_of(w));
        return true;
    default:
        return false;
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    Backend* backend = backend_of(window);
    if (backend == nullptr || backend->session == nullptr) {
        return DefWindowProcA(window, message, w, l);
    }
    InspectionSession& session = *backend->session;
    switch (message) {
    case WM_ERASEBKGND:
        return 1;  // the paint covers every pixel
    case WM_PAINT:
        session.update();
        if (session.failed()) {
            PostQuitMessage(0);
        }
        session.present();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    InputEvent event;
    if (event_of(message, w, l, event)) {
        // Queued and taken at once: every event reaches the session through
        // the InputService a frame loop drains, and in the order the window
        // procedure is called in. That includes WM_CAPTURECHANGED, which
        // Windows sends from inside the ReleaseCapture that ending a drag
        // makes, so it is handled inside that drag's release exactly as it
        // was when both were handlers here.
        backend->input.queue.push_back(event);
        session.pump_events();
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

}  // namespace

bool inspection_window_supported() noexcept { return true; }

const char* inspection_window_backend_name() noexcept { return "win32"; }

bool run_inspection_window(
    Inspector& inspector,
    const InspectionWindowOptions& options,
    std::string& error) {
    if (!inspector.has_scene()) {
        error = "the inspector has no scene";
        return false;
    }
    if (options.drag_divisor == 0 || options.drag_divisor > kMaxDragDivisor) {
        error = "the drag divisor must be between 1 and " +
                std::to_string(kMaxDragDivisor);
        return false;
    }
    HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSEXA window_class;
    std::memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    // The class outlives one session, so an existing registration is the
    // expected answer on a second call and not a failure.
    if (RegisterClassExA(&window_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "could not register the inspection window class";
        return false;
    }

    Backend backend;
    InspectionSession session(inspector, options, backend.input, backend.display,
                              backend.lifecycle);
    backend.session = &session;

    RECT wanted;
    wanted.left = 0;
    wanted.top = 0;
    wanted.right = static_cast<LONG>(inspector.options().width);
    wanted.bottom = static_cast<LONG>(inspector.options().height);
    // The client area is what the frame size means, so the window is grown
    // by however much its frame and title bar take.
    AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);
    const std::string title = ascii_title(options.title);
    HWND window = CreateWindowExA(
        0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, wanted.right - wanted.left, wanted.bottom - wanted.top,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        error = "could not create the inspection window";
        return false;
    }
    backend.display.window = window;
    backend.input.window = window;
    backend.lifecycle.window = window;
    SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&backend));
    session.start();

    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);

    while (backend.lifecycle.pump()) {
        if (session.failed()) {
            break;
        }
    }
    // The window may already be gone when the loop ended through WM_DESTROY.
    if (IsWindow(window)) {
        SetWindowLongPtrA(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
    }
    if (session.failed()) {
        error = session.failure();
        return false;
    }
    return true;
}

} // namespace nfsu
