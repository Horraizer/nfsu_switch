#include "nfsu/inspect_session.h"

#include "nfsu/input_camera.h"

#include <algorithm>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

// The inspection session: the rules the desktop window applied to pointer,
// wheel and key input, and to painting, moved out of the Win32 window
// procedure so that they no longer depend on it. include/nfsu/inspect_session.h
// states the contract.
//
// Every function below was a handler or a helper in
// src/inspect_window_win32.cpp and does, in the same order, what it did
// there. Where that code called Windows, this calls a service:
// SetCapture and ReleaseCapture are InputService::set_pointer_capture,
// InvalidateRect is DisplayService::request_redraw, posting WM_CLOSE for
// 'Q' is LifecycleService::request_exit, and BeginPaint, GetClientRect,
// the bitmap blit and EndPaint are the three DisplayService frame calls.
// The window procedure translates a message into an InputEvent and calls
// handle() at once, so the order in which state changes, including the
// capture change Windows sends from inside ReleaseCapture, is the order it
// always was. docs/PLATFORM_SERVICES.md lists what moved and why nothing a
// window shows could change.

namespace nfsu {

namespace {

// A drag shorter than this many pixels in both axes counts as a click.
constexpr std::int64_t kClickSlack = 3;

// The key table. Nothing else in the session decides what a key means; the
// backend decides only which of its keys is which Key.
bool command_for_key(Key key, InspectorCommand& command) {
    switch (key) {
    case Key::digit_1: command = InspectorCommand::view_front; return true;
    case Key::digit_2: command = InspectorCommand::view_right; return true;
    case Key::digit_3: command = InspectorCommand::view_rear; return true;
    case Key::digit_4: command = InspectorCommand::view_left; return true;
    case Key::digit_5: command = InspectorCommand::view_top; return true;
    case Key::r: command = InspectorCommand::reset_camera; return true;
    case Key::f: command = InspectorCommand::frame_selection; return true;
    case Key::g: command = InspectorCommand::frame_scene; return true;
    case Key::n: command = InspectorCommand::select_next_draw; return true;
    case Key::p: command = InspectorCommand::select_previous_draw; return true;
    case Key::m: command = InspectorCommand::select_next_mesh; return true;
    case Key::b: command = InspectorCommand::select_previous_mesh; return true;
    case Key::escape: command = InspectorCommand::clear_selection; return true;
    case Key::v: command = InspectorCommand::cycle_view; return true;
    case Key::o: command = InspectorCommand::cycle_overlay; return true;
    case Key::h: command = InspectorCommand::toggle_highlight; return true;
    case Key::c: command = InspectorCommand::cycle_cull; return true;
    case Key::s: command = InspectorCommand::toggle_shading; return true;
    case Key::t: command = InspectorCommand::toggle_material_state; return true;
    case Key::x: command = InspectorCommand::toggle_vertex_color; return true;
    case Key::u: command = InspectorCommand::toggle_flip_v; return true;
    default: return false;
    }
}

std::int64_t magnitude(std::int64_t value) noexcept { return value < 0 ? -value : value; }

} // namespace

InspectionSession::InspectionSession(
    Inspector& inspector,
    const InspectionWindowOptions& options,
    InputService& input,
    DisplayService& display,
    LifecycleService& lifecycle)
    : inspector_(inspector),
      options_(options),
      input_(input),
      display_(display),
      lifecycle_(lifecycle) {}

// The viewport the next frame is rendered at: the surface size, divided
// while a drag is in progress so that the CPU renderer keeps up.
//
// Returns false when the viewport is not the size that was asked for, which
// happens when the render limits refuse it: the previous viewport is kept
// and the display stretches whatever size the frame has onto the surface.
// Only the pick cares, because a click is resolved in surface pixels and that
// is only the same pixel when the frame covers the surface one to one.
bool InspectionSession::apply_viewport() {
    if (client_width_ <= 0 || client_height_ <= 0) {
        return false;
    }
    std::uint32_t divisor = 1;
    if (orbiting_ || panning_ || gamepad_moving_) {
        divisor = options_.drag_divisor == 0 ? 1 : options_.drag_divisor;
    }
    const std::uint32_t width =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(client_width_) / divisor);
    const std::uint32_t height =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(client_height_) / divisor);
    std::string ignored;
    return inspector_.set_viewport(width, height, ignored);
}

void InspectionSession::start() {
    int width = 0;
    int height = 0;
    display_.surface_size(width, height);
    client_width_ = width;
    client_height_ = height;
    static_cast<void>(apply_viewport());
}

void InspectionSession::request_redraw() {
    redraw_requested_ = true;
    display_.request_redraw();
}

void InspectionSession::pump_events() {
    InputEvent event;
    while (input_.next_event(event)) {
        handle(event);
    }
}

void InspectionSession::handle(const InputEvent& event) {
    switch (event.type) {
    case InputEventType::resize:
        client_width_ = event.x;
        client_height_ = event.y;
        static_cast<void>(apply_viewport());
        request_redraw();
        return;
    case InputEventType::pointer_down:
        begin_drag(event.button == PointerButton::primary, event.x, event.y);
        return;
    // Releasing any pointer button ends the drag that is running, whichever
    // button started it. Pressing a second button while the first is held
    // switches the drag, so tying each release to its own button would make
    // the first release do nothing and hold the capture until the second.
    case InputEventType::pointer_up:
        if (orbiting_ || panning_) {
            end_drag(event.x, event.y, orbiting_);
        }
        return;
    case InputEventType::pointer_cancel:
        orbiting_ = false;
        panning_ = false;
        static_cast<void>(apply_viewport());
        return;
    case InputEventType::pointer_move:
        pointer_move(event.x, event.y);
        return;
    case InputEventType::wheel:
        wheel(event.wheel);
        return;
    case InputEventType::key_down:
        key(event.key);
        return;
    }
}

void InspectionSession::begin_drag(bool orbit, std::int32_t x, std::int32_t y) {
    orbiting_ = orbit;
    panning_ = !orbit;
    press_x_ = x;
    press_y_ = y;
    last_x_ = x;
    last_y_ = y;
    input_.set_pointer_capture(true);
    static_cast<void>(apply_viewport());
}

void InspectionSession::end_drag(std::int32_t x, std::int32_t y, bool was_orbit) {
    const bool click = was_orbit &&
                       magnitude(std::int64_t{x} - press_x_) <= kClickSlack &&
                       magnitude(std::int64_t{y} - press_y_) <= kClickSlack;
    orbiting_ = false;
    panning_ = false;
    input_.set_pointer_capture(false);
    const bool full_size = apply_viewport();
    if (click && full_size) {
        // Resolved by the next update, against a frame at the full surface
        // size, so the surface pixel and the frame pixel are the same one.
        // A surface the render limits refuse leaves the frame at the drag
        // size, where that is no longer true, so the click is dropped
        // rather than resolved against the wrong pixel.
        pending_pick_ = true;
        pick_x_ = x;
        pick_y_ = y;
    }
    request_redraw();
}

void InspectionSession::pointer_move(std::int32_t x, std::int32_t y) {
    if (!orbiting_ && !panning_) {
        return;
    }
    // In double, so that no pair of 32-bit positions can overflow; for the
    // 16-bit positions a window reports the difference is the same exact
    // integer it was when it was taken in int.
    const double dx = static_cast<double>(x) - static_cast<double>(last_x_);
    const double dy = static_cast<double>(y) - static_cast<double>(last_y_);
    last_x_ = x;
    last_y_ = y;
    if (dx == 0.0 && dy == 0.0) {
        return;
    }
    if (orbiting_) {
        // Degrees per pixel does not depend on the viewport, so an orbit is
        // the same drag whatever size the frame is rendered at.
        inspector_.orbit(dx, dy);
    } else {
        // A pan does depend on it: the delta is in surface pixels and
        // Inspector::pan measures in the pixels of the viewport it will
        // render at, which is smaller than the surface for the length of
        // the drag.
        const RenderOptions& view = inspector_.options();
        const PanDelta delta =
            pan_delta_for(client_width_, client_height_, view.width, view.height, dx, dy);
        inspector_.pan(delta.dx, delta.dy);
    }
    request_redraw();
}

void InspectionSession::wheel(std::int32_t delta) {
    // Accumulate, so that a precision trackpad reporting a fraction of a
    // notch per event still zooms once its events add up to one. A window
    // reports a 16-bit delta; one outside that range is clamped into it,
    // which with the residual bounded by a notch in each direction keeps
    // the sum from overflowing whatever a backend sends.
    const std::int32_t bounded = delta < -32768 ? -32768 : (delta > 32767 ? 32767 : delta);
    const std::int32_t total = wheel_residual_ + bounded;
    const std::int32_t steps = total / kWheelNotch;
    wheel_residual_ = total - steps * kWheelNotch;
    if (steps != 0) {
        inspector_.zoom(steps);
        request_redraw();
    }
}

void InspectionSession::key(Key key) {
    if (key == Key::q) {
        lifecycle_.request_exit();
        return;
    }
    InspectorCommand command = InspectorCommand::reset_camera;
    if (command_for_key(key, command)) {
        std::string error;
        // A command that cannot apply (nothing selected, no drawable
        // group) is not an error of the session: it changes nothing and the
        // overlay keeps showing what is there.
        static_cast<void>(inspector_.command(command, error));
        request_redraw();
    }
}

void InspectionSession::set_gamepad_motion(bool moving) {
    if (gamepad_moving_ == moving) {
        return;
    }
    gamepad_moving_ = moving;
    static_cast<void>(apply_viewport());
}

void InspectionSession::update() {
    if (!inspector_.has_scene()) {
        // The backends refuse such an inspector before a session exists;
        // this is the session refusing it too rather than rendering null.
        failure_ = "the inspector has no scene";
        return;
    }
    // Only a change the frame itself depends on costs a render. Switching
    // the view, moving the overlay and selecting a group are answered from
    // the frame already on hand, which is what makes them feel instant on a
    // car that takes a moment to rasterize.
    if (inspector_.frame_stale() || !have_frame_) {
        std::string error;
        RenderStats stats;
        if (render_scene(*inspector_.scene(), inspector_.options(), inspector_.limits(),
                         frame_, stats, error)) {
            stats_ = stats;
            have_frame_ = true;
            render_error_.clear();
        } else {
            render_error_ = error;
            if (!have_frame_) {
                failure_ = error;
                return;
            }
        }
        inspector_.clear_frame_stale();
    }
    inspector_.clear_dirty();
    if (pending_pick_ && have_frame_) {
        pending_pick_ = false;
        const InspectionBuffers buffers = inspection_buffers_of(frame_);
        // The frame was rendered at the current viewport, which equals the
        // surface outside a drag; a click is only ever resolved there.
        if (pick_x_ >= 0 && pick_y_ >= 0 &&
            static_cast<std::uint32_t>(pick_x_) < buffers.width &&
            static_cast<std::uint32_t>(pick_y_) < buffers.height) {
            std::string error;
            inspector_.pick(buffers, static_cast<std::uint32_t>(pick_x_),
                            static_cast<std::uint32_t>(pick_y_), error);
        }
        // A selection changes text and the highlight, not the frame.
        inspector_.clear_dirty();
    }
}

// Builds the BGRA rows a display takes out of the inspector view and the
// highlight.
bool InspectionSession::compose() {
    const InspectionBuffers buffers = inspection_buffers_of(frame_);
    std::string error;
    if (!inspector_.view_pixels(buffers, view_rgba_, error) ||
        !inspector_.highlight_mask(buffers, mask_, error)) {
        failure_ = error;
        return false;
    }
    const std::size_t pixels = mask_.size();
    try {
        bgra_.resize(pixels * 4U);
    } catch (const std::bad_alloc&) {
        failure_ = "not enough memory to present the frame";
        return false;
    }
    for (std::size_t i = 0; i < pixels; ++i) {
        unsigned char r = view_rgba_[i * 4U + 0];
        unsigned char g = view_rgba_[i * 4U + 1];
        unsigned char b = view_rgba_[i * 4U + 2];
        if (mask_[i] != 0) {
            // Half way to a fixed amber, which stays visible over both the
            // shaded frame and the flat index colors.
            r = static_cast<unsigned char>((r + 255U) / 2U);
            g = static_cast<unsigned char>((g + 190U) / 2U);
            b = static_cast<unsigned char>((b + 0U) / 2U);
        }
        bgra_[i * 4U + 0] = b;
        bgra_[i * 4U + 1] = g;
        bgra_[i * 4U + 2] = r;
        bgra_[i * 4U + 3] = 255;
    }
    return true;
}

void InspectionSession::present() {
    int width = 0;
    int height = 0;
    if (!display_.begin_frame(width, height)) {
        return;
    }
    if (width > 0 && height > 0 && have_frame_ && compose()) {
        inspector_.build_overlay(stats_, overlay_);
        if (!render_error_.empty()) {
            overlay_.push_back("render failed: " + render_error_);
        }
        DisplayImage image;
        image.width = frame_.width();
        image.height = frame_.height();
        image.bgra = bgra_.data();
        display_.draw_frame(image, overlay_);
    }
    display_.end_frame();
    redraw_requested_ = false;
}

bool run_inspection_loop(
    Inspector& inspector,
    const InspectionWindowOptions& options,
    GamepadCamera* gamepads,
    InputService& input,
    DisplayService& display,
    LifecycleService& lifecycle,
    TimingService& timing,
    std::string& error) {
    if (!inspector.has_scene()) {
        error = "the inspector has no scene";
        return false;
    }
    if (options.drag_divisor == 0 || options.drag_divisor > kMaxDragDivisor) {
        error = "the drag divisor must be between 1 and " + std::to_string(kMaxDragDivisor);
        return false;
    }
    InspectionSession session(inspector, options, input, display, lifecycle);
    session.start();
    bool first = true;
    while (lifecycle.pump()) {
        session.pump_events();
        if (gamepads != nullptr) {
            GamepadFrame frame;
            frame.time_us = timing.now_us();
            input.sample_gamepads(frame);
            const CameraStep step = gamepads->step(frame, inspector);
            session.set_gamepad_motion(step.moving);
            if (step.exit_requested) {
                lifecycle.request_exit();
            }
        }
        // Decided before update(), which clears the inspector's flags: a
        // frame is presented when the first one is due, when anything
        // changed what the inspector shows, or when an event asked for it.
        const bool changed = first || inspector.dirty() || session.redraw_requested();
        session.update();
        if (session.failed()) {
            break;
        }
        if (changed) {
            session.present();
            if (session.failed()) {
                break;
            }
        }
        first = false;
    }
    if (session.failed()) {
        error = session.failure();
        return false;
    }
    return true;
}

} // namespace nfsu
