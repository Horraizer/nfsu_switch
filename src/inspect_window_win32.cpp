#include "nfsu/inspect_window.h"

#include "nfsu/render.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
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
// bitmap. Its whole job is to turn pointer and key events into
// InspectorCommand values, ask the Inspector what to show and put it on the
// screen. Every rule about what a control does lives in src/inspect.cpp.

namespace nfsu {

namespace {

constexpr const char* kWindowClass = "nfsu_inspection_window";
// Overlay box geometry, in pixels of the client area.
constexpr int kOverlayMargin = 8;
constexpr int kOverlayPadding = 6;
// A drag shorter than this many pixels in both axes counts as a click.
constexpr int kClickSlack = 3;

std::string ascii_title(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        out += (byte >= 0x20U && byte <= 0x7EU) ? ch : '?';
    }
    return out.empty() ? std::string("nfsu inspector") : out;
}

struct Session {
    Inspector* inspector = nullptr;
    InspectionWindowOptions options;
    Frame frame;
    RenderStats stats;
    // Set when the frame buffer holds a frame that can be presented.
    bool have_frame = false;
    // The last render failure, shown in the overlay until a render succeeds.
    std::string render_error;
    // Pointer state.
    bool orbiting = false;
    bool panning = false;
    int last_x = 0;
    int last_y = 0;
    int press_x = 0;
    int press_y = 0;
    // A click that has not been resolved into a selection yet, in client
    // pixels; resolved against the frame that the next paint renders.
    bool pending_pick = false;
    int pick_x = 0;
    int pick_y = 0;
    // Client size the viewport currently follows.
    int client_width = 0;
    int client_height = 0;
    // Scratch buffers, kept between paints so that a drag allocates nothing.
    std::vector<unsigned char> view_rgba;
    std::vector<unsigned char> mask;
    std::vector<unsigned char> bgra;
    std::vector<std::string> overlay;
    // The exit reason when the loop ends early.
    std::string fatal;
};

// The viewport the next frame is rendered at: the client size, divided while
// a drag is in progress so that the CPU renderer keeps up.
void apply_viewport(Session& session) {
    if (session.inspector == nullptr || session.client_width <= 0 ||
        session.client_height <= 0) {
        return;
    }
    std::uint32_t divisor = 1;
    if (session.orbiting || session.panning) {
        divisor = session.options.drag_divisor == 0 ? 1 : session.options.drag_divisor;
    }
    const std::uint32_t width =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(session.client_width) / divisor);
    const std::uint32_t height =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(session.client_height) / divisor);
    std::string ignored;
    // A size the render limits refuse keeps the previous viewport; the blit
    // stretches whatever size the frame has onto the client area.
    session.inspector->set_viewport(width, height, ignored);
}

// Renders when the inspector says the frame is stale, then resolves a
// pending click against the frame that is now on hand.
void update_frame(Session& session) {
    Inspector& inspector = *session.inspector;
    // Only a change the frame itself depends on costs a render. Switching
    // the view, moving the overlay and selecting a group are answered from
    // the frame already on hand, which is what makes them feel instant on a
    // car that takes a moment to rasterize.
    if (inspector.frame_stale() || !session.have_frame) {
        std::string error;
        RenderStats stats;
        if (render_scene(*inspector.scene(), inspector.options(), inspector.limits(),
                         session.frame, stats, error)) {
            session.stats = stats;
            session.have_frame = true;
            session.render_error.clear();
        } else {
            session.render_error = error;
            if (!session.have_frame) {
                session.fatal = error;
                return;
            }
        }
        inspector.clear_frame_stale();
    }
    inspector.clear_dirty();
    if (session.pending_pick && session.have_frame) {
        session.pending_pick = false;
        const InspectionBuffers buffers = inspection_buffers_of(session.frame);
        // The frame was rendered at the current viewport, which equals the
        // client area outside a drag; a click is only ever resolved there.
        if (session.pick_x >= 0 && session.pick_y >= 0 &&
            static_cast<std::uint32_t>(session.pick_x) < buffers.width &&
            static_cast<std::uint32_t>(session.pick_y) < buffers.height) {
            std::string error;
            inspector.pick(buffers, static_cast<std::uint32_t>(session.pick_x),
                           static_cast<std::uint32_t>(session.pick_y), error);
        }
        // A selection changes text and the highlight, not the frame.
        inspector.clear_dirty();
    }
}

// Builds the BGRA rows GDI wants out of the inspector view and the highlight.
bool compose(Session& session) {
    Inspector& inspector = *session.inspector;
    const InspectionBuffers buffers = inspection_buffers_of(session.frame);
    std::string error;
    if (!inspector.view_pixels(buffers, session.view_rgba, error) ||
        !inspector.highlight_mask(buffers, session.mask, error)) {
        session.fatal = error;
        return false;
    }
    const std::size_t pixels = session.mask.size();
    try {
        session.bgra.resize(pixels * 4U);
    } catch (const std::bad_alloc&) {
        session.fatal = "not enough memory to present the frame";
        return false;
    }
    for (std::size_t i = 0; i < pixels; ++i) {
        unsigned char r = session.view_rgba[i * 4U + 0];
        unsigned char g = session.view_rgba[i * 4U + 1];
        unsigned char b = session.view_rgba[i * 4U + 2];
        if (session.mask[i] != 0) {
            // Half way to a fixed amber, which stays visible over both the
            // shaded frame and the flat index colors.
            r = static_cast<unsigned char>((r + 255U) / 2U);
            g = static_cast<unsigned char>((g + 190U) / 2U);
            b = static_cast<unsigned char>((b + 0U) / 2U);
        }
        session.bgra[i * 4U + 0] = b;
        session.bgra[i * 4U + 1] = g;
        session.bgra[i * 4U + 2] = r;
        session.bgra[i * 4U + 3] = 255;
    }
    return true;
}

void draw_overlay(Session& session, HDC dc, int width, int height) {
    Inspector& inspector = *session.inspector;
    inspector.build_overlay(session.stats, session.overlay);
    if (!session.render_error.empty()) {
        session.overlay.push_back("render failed: " + session.render_error);
    }
    if (session.overlay.empty()) {
        return;
    }
    HGDIOBJ font = GetStockObject(ANSI_FIXED_FONT);
    HGDIOBJ previous_font = SelectObject(dc, font);
    TEXTMETRICA metrics;
    std::memset(&metrics, 0, sizeof(metrics));
    GetTextMetricsA(dc, &metrics);
    const int line_height = metrics.tmHeight + 2;
    int box_width = 0;
    for (const std::string& line : session.overlay) {
        SIZE size;
        if (GetTextExtentPoint32A(dc, line.c_str(), static_cast<int>(line.size()), &size)) {
            box_width = std::max<int>(box_width, size.cx);
        }
    }
    const int box_height =
        line_height * static_cast<int>(session.overlay.size()) + kOverlayPadding * 2;
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
    for (const std::string& line : session.overlay) {
        if (y + line_height > height) {
            break;
        }
        TextOutA(dc, kOverlayMargin + kOverlayPadding, y, line.c_str(),
                 static_cast<int>(line.size()));
        y += line_height;
    }
    SelectObject(dc, previous_font);
}

void paint(Session& session, HWND window) {
    PAINTSTRUCT paint_struct;
    HDC dc = BeginPaint(window, &paint_struct);
    if (dc == nullptr) {
        return;
    }
    RECT client;
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width > 0 && height > 0 && session.have_frame && compose(session)) {
        // One memory bitmap for the whole client area: the stretch and the
        // text land there and reach the screen in a single blit, so the
        // overlay never flickers over a half-drawn frame.
        HDC memory = CreateCompatibleDC(dc);
        HBITMAP bitmap = memory != nullptr ? CreateCompatibleBitmap(dc, width, height)
                                           : nullptr;
        if (memory != nullptr && bitmap != nullptr) {
            HGDIOBJ previous = SelectObject(memory, bitmap);
            BITMAPINFO info;
            std::memset(&info, 0, sizeof(info));
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = static_cast<LONG>(session.frame.width());
            // Negative: the rows are top down, as the renderer writes them.
            info.bmiHeader.biHeight = -static_cast<LONG>(session.frame.height());
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(memory, COLORONCOLOR);
            StretchDIBits(memory, 0, 0, width, height, 0, 0,
                          static_cast<int>(session.frame.width()),
                          static_cast<int>(session.frame.height()),
                          session.bgra.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            draw_overlay(session, memory, width, height);
            BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            DeleteDC(memory);
        }
    }
    EndPaint(window, &paint_struct);
}

// The key table. Nothing else in this file decides what a key means.
bool command_for_key(WPARAM key, InspectorCommand& command) {
    switch (key) {
    case '1': command = InspectorCommand::view_front; return true;
    case '2': command = InspectorCommand::view_right; return true;
    case '3': command = InspectorCommand::view_rear; return true;
    case '4': command = InspectorCommand::view_left; return true;
    case '5': command = InspectorCommand::view_top; return true;
    case 'R': command = InspectorCommand::reset_camera; return true;
    case 'F': command = InspectorCommand::frame_selection; return true;
    case 'G': command = InspectorCommand::frame_scene; return true;
    case 'N': command = InspectorCommand::select_next_draw; return true;
    case 'P': command = InspectorCommand::select_previous_draw; return true;
    case 'M': command = InspectorCommand::select_next_mesh; return true;
    case 'B': command = InspectorCommand::select_previous_mesh; return true;
    case VK_ESCAPE: command = InspectorCommand::clear_selection; return true;
    case 'V': command = InspectorCommand::cycle_view; return true;
    case 'O': command = InspectorCommand::cycle_overlay; return true;
    case 'H': command = InspectorCommand::toggle_highlight; return true;
    case 'C': command = InspectorCommand::cycle_cull; return true;
    case 'S': command = InspectorCommand::toggle_shading; return true;
    case 'T': command = InspectorCommand::toggle_material_state; return true;
    case 'X': command = InspectorCommand::toggle_vertex_color; return true;
    case 'U': command = InspectorCommand::toggle_flip_v; return true;
    default: return false;
    }
}

Session* session_of(HWND window) {
    return reinterpret_cast<Session*>(GetWindowLongPtrA(window, GWLP_USERDATA));
}

void begin_drag(Session& session, HWND window, bool orbit, LPARAM position) {
    session.orbiting = orbit;
    session.panning = !orbit;
    session.press_x = GET_X_LPARAM(position);
    session.press_y = GET_Y_LPARAM(position);
    session.last_x = session.press_x;
    session.last_y = session.press_y;
    SetCapture(window);
    apply_viewport(session);
}

void end_drag(Session& session, HWND window, LPARAM position, bool was_orbit) {
    const int x = GET_X_LPARAM(position);
    const int y = GET_Y_LPARAM(position);
    const bool click = was_orbit && std::abs(x - session.press_x) <= kClickSlack &&
                       std::abs(y - session.press_y) <= kClickSlack;
    session.orbiting = false;
    session.panning = false;
    ReleaseCapture();
    apply_viewport(session);
    if (click) {
        // Resolved by the next paint, against a frame at the full client
        // size, so the client pixel and the frame pixel are the same one.
        session.pending_pick = true;
        session.pick_x = x;
        session.pick_y = y;
    }
    InvalidateRect(window, nullptr, FALSE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    Session* session = session_of(window);
    if (session == nullptr || session->inspector == nullptr) {
        return DefWindowProcA(window, message, w, l);
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;  // the paint covers every pixel
    case WM_SIZE: {
        session->client_width = static_cast<int>(LOWORD(l));
        session->client_height = static_cast<int>(HIWORD(l));
        apply_viewport(*session);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT:
        update_frame(*session);
        if (!session->fatal.empty()) {
            PostQuitMessage(0);
        }
        paint(*session, window);
        return 0;
    case WM_LBUTTONDOWN:
        begin_drag(*session, window, true, l);
        return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        begin_drag(*session, window, false, l);
        return 0;
    case WM_LBUTTONUP:
        if (session->orbiting) {
            end_drag(*session, window, l, true);
        }
        return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if (session->panning) {
            end_drag(*session, window, l, false);
        }
        return 0;
    case WM_CAPTURECHANGED:
        session->orbiting = false;
        session->panning = false;
        apply_viewport(*session);
        return 0;
    case WM_MOUSEMOVE: {
        if (!session->orbiting && !session->panning) {
            return 0;
        }
        const int x = GET_X_LPARAM(l);
        const int y = GET_Y_LPARAM(l);
        const double dx = static_cast<double>(x - session->last_x);
        const double dy = static_cast<double>(y - session->last_y);
        session->last_x = x;
        session->last_y = y;
        if (dx == 0.0 && dy == 0.0) {
            return 0;
        }
        if (session->orbiting) {
            session->inspector->orbit(dx, dy);
        } else {
            session->inspector->pan(dx, dy);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(w);
        const int steps = delta / WHEEL_DELTA;
        if (steps != 0) {
            session->inspector->zoom(steps);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (w == 'Q') {
            PostMessageA(window, WM_CLOSE, 0, 0);
            return 0;
        }
        InspectorCommand command = InspectorCommand::reset_camera;
        if (command_for_key(w, command)) {
            std::string error;
            // A command that cannot apply (nothing selected, no drawable
            // group) is not an error of the window: it changes nothing and
            // the overlay keeps showing what is there.
            session->inspector->command(command, error);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
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

    Session session;
    session.inspector = &inspector;
    session.options = options;

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
    SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&session));
    RECT client;
    GetClientRect(window, &client);
    session.client_width = client.right - client.left;
    session.client_height = client.bottom - client.top;
    apply_viewport(session);

    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);

    MSG message;
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
        if (!session.fatal.empty()) {
            break;
        }
    }
    // The window may already be gone when the loop ended through WM_DESTROY.
    if (IsWindow(window)) {
        SetWindowLongPtrA(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
    }
    if (!session.fatal.empty()) {
        error = session.fatal;
        return false;
    }
    return true;
}

} // namespace nfsu
