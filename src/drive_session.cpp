#include "nfsu/drive_session.h"
#include "nfsu/car_parts.h"

#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The controller-to-frame loop of issue #59 and what the CPU reference
// renderer needs to show it. include/nfsu/drive_session.h states each rule
// and docs/CONTROLS_CAMERA.md labels it. Plain IEEE arithmetic with
// contraction disabled by cmake/drive_libraries.cmake; no library
// mathematics function and no clock.

#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "the drive session needs double arithmetic without excess precision"
#endif

namespace nfsu {

namespace {

constexpr double kPi = 3.14159265358979311600e+00;

VehicleVec3 add(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

VehicleVec3 sub(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

VehicleVec3 scale(const VehicleVec3& a, double s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}

VehicleVec3 cross(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double dot(const VehicleVec3& a, const VehicleVec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

VehicleVec3 rotate(const VehicleQuat& q, const VehicleVec3& v) noexcept {
    const VehicleVec3 u{q.x, q.y, q.z};
    const VehicleVec3 t = scale(cross(u, v), 2.0);
    return add(add(v, scale(t, q.w)), cross(u, t));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
}

void put_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
}

void put_f64(std::vector<std::uint8_t>& out, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u64(out, bits);
}

void put_vec(std::vector<std::uint8_t>& out, const VehicleVec3& v) {
    put_f64(out, v.x);
    put_f64(out, v.y);
    put_f64(out, v.z);
}

double forward_speed(const VehicleState& state) noexcept {
    return dot(state.velocity, rotate(state.orientation, VehicleVec3{1.0, 0.0, 0.0}));
}

// floor(value / step) * step for a step that divides exactly, without a
// library call: the quotient's integer part, one lower for a negative
// remainder.
double floor_to(double value, double step) noexcept {
    const double q = value / step;
    if (!(q > -1.0e15) || !(q < 1.0e15)) {
        return 0.0;
    }
    std::int64_t whole = static_cast<std::int64_t>(q);
    if (static_cast<double>(whole) > q) {
        --whole;
    }
    return static_cast<double>(whole) * step;
}

} // namespace

// ---------------------------------------------------------------------------
// The record
// ---------------------------------------------------------------------------

void encode_drive_frame(std::uint64_t frame,
                        const VehicleInput (&inputs)[kDriveStepsPerFrame],
                        const VehicleTelemetry (&telemetry)[kDriveStepsPerFrame],
                        const ChaseCameraView& view,
                        const DriveFrameState& events,
                        std::vector<std::uint8_t>& out) {
    out.clear();
    out.reserve(kDriveFrameRecordBytes);
    put_u64(out, frame);
    for (std::size_t i = 0; i < kDriveStepsPerFrame; ++i) {
        put_f64(out, inputs[i].throttle);
        put_f64(out, inputs[i].brake);
        put_f64(out, inputs[i].steer);
        put_u32(out, inputs[i].handbrake ? 1U : 0U);
        put_u32(out, static_cast<std::uint32_t>(inputs[i].shift));
        std::array<std::uint8_t, kVehicleTelemetryRecordBytes> bytes{};
        encode_vehicle_telemetry(telemetry[i], bytes);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    put_u32(out, static_cast<std::uint32_t>(view.pov));
    put_u32(out, view.heading);
    put_u32(out, view.target);
    put_u32(out, static_cast<std::uint32_t>(static_cast<std::uint16_t>(view.lag)));
    put_u32(out, view.pitch);
    put_u32(out, view.fov);
    put_vec(out, view.eye);
    put_vec(out, view.forward);
    put_vec(out, view.up);
    put_f64(out, view.reach);
    put_u32(out, events.camera ? 1U : 0U);
    put_u32(out, events.reset ? 1U : 0U);
}

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

DriveSessionSetup default_drive_session_setup() {
    DriveSessionSetup setup;
    setup.spec = stock_240sx_vehicle_spec();
    setup.controls = default_drive_controls_config();
    setup.camera_table = stock_240sx_chase_camera_table();
    setup.pov = 0;
    return setup;
}

bool DriveSession::start(const DriveSessionSetup& setup, std::string& error) {
    std::unique_ptr<VehicleModel> model(new VehicleModel(setup.spec));
    if (!model->valid()) {
        error = "the vehicle model refuses the car: " + model->error();
        return false;
    }
    DriveControls controls;
    if (!controls.configure(setup.controls, error)) {
        error = "the controls refuse the configuration: " + error;
        return false;
    }
    ChaseCamera camera(setup.camera_table, setup.camera_rules, setup.camera_approx);
    if (!camera.valid()) {
        error = "the chase camera refuses its setup: " + camera.error();
        return false;
    }
    if (!camera.set_pov(setup.pov)) {
        error = "the chase camera has no POV " + std::to_string(setup.pov);
        return false;
    }
    VehicleState state;
    if (!model->reset(state, setup.pose)) {
        error = "the vehicle model refuses the starting pose";
        return false;
    }
    ChaseCameraView view;
    if (!camera.reset(chase_subject(state, setup.spec), world_, view)) {
        error = "the chase camera refuses the starting pose";
        return false;
    }
    setup_ = setup;
    model_ = std::move(model);
    controls_ = controls;
    camera_ = camera;
    state_ = state;
    telemetry_ = VehicleTelemetry{};
    last_input_ = VehicleInput{};
    view_ = view;
    frames_ = 0;
    sha_ = Sha256{};
    hashed_bytes_ = 0;
    return true;
}

bool DriveSession::advance(const GamepadFrame& frame, std::string& error) {
    if (!model_) {
        error = "the session has not started";
        return false;
    }
    DriveControls controls = controls_;
    ChaseCamera camera = camera_;
    VehicleState state = state_;
    controls.update(frame);
    VehicleInput inputs[kDriveStepsPerFrame];
    VehicleTelemetry telemetry[kDriveStepsPerFrame];
    // Preserve the port's two120Hz filter updates as a qualified control
    // adapter. Transfer the completed input once before the outer solve;
    // original4775E4 does not refresh steering between physics slices.
    const auto first=controls.step(forward_speed(state),state.gear,state.gearbox);
    auto held=controls.step(forward_speed(state),state.gear,state.gearbox);
    if(first.shift!=VehicleShift::none) held.shift=first.shift;
    for (std::size_t i = 0; i < kDriveStepsPerFrame; ++i) {
        inputs[i]=held;
        if(i) inputs[i].shift=VehicleShift::none;
        if (!model_->step(state, inputs[i], ground_, telemetry[i],
                          i+1==kDriveStepsPerFrame?kDriveFrameSeconds:0.0,
                          kDriveSolverSeconds,i==0)) {
            error = "the vehicle model stopped at frame " + std::to_string(frames_ + 1) +
                    ": its state is no longer finite";
            return false;
        }
    }
    const DriveFrameState events = controls.frame_state();
    if (events.camera) {
        camera.next_pov();
    }
    ChaseCameraView view;
    if (!camera.update(chase_subject(state, setup_.spec), kDriveFrameSeconds, world_, view,
                       events.look_back)) {
        error = "the chase camera refused frame " + std::to_string(frames_ + 1);
        return false;
    }
    controls_ = controls;
    camera_ = camera;
    state_ = state;
    telemetry_ = telemetry[kDriveStepsPerFrame - 1];
    last_input_ = inputs[kDriveStepsPerFrame - 1];
    view_ = view;
    ++frames_;
    encode_drive_frame(frames_, inputs, telemetry, view, events, record_);
    sha_.update(record_.data(), record_.size());
    hashed_bytes_ += record_.size();
    return true;
}

// ---------------------------------------------------------------------------
// The demonstration
// ---------------------------------------------------------------------------

std::vector<GamepadFrame> drive_demo_recording() {
    struct Segment {
        std::uint32_t frames;
        std::uint32_t buttons;
        std::int32_t left_x;
    };
    const std::uint32_t zr = gamepad_button_bit(GamepadButton::trigger_right);
    const std::uint32_t zl = gamepad_button_bit(GamepadButton::trigger_left);
    const std::uint32_t right = gamepad_button_bit(GamepadButton::dpad_right);
    const std::uint32_t b = gamepad_button_bit(GamepadButton::face_south);
    const std::uint32_t x = gamepad_button_bit(GamepadButton::face_north);
    // The times are cumulative frames: 0.5 s at rest; 6 s of full throttle;
    // 3 s of throttle with the stick half left; 2 s of throttle with the
    // D-pad right; 2 s of brake with the stick full right; 1 s of the
    // handbrake with the stick full left, the next chase view pressed on its
    // first frame; 1 s coasting; 4 s of brake alone, which stops the car and
    // after 0.25 s below 1 m/s puts it in reverse, where the brake drives it
    // backwards; 3.5 s of throttle alone, which in reverse brakes it, at a
    // standstill puts it back in first, and then drives it forward.
    const Segment segments[] = {
        {30, 0, 0},           {360, zr, 0},         {180, zr, -16384}, {120, zr | right, 0},
        {120, zl, 32767},     {1, b | x, -32767},   {59, b, -32767},   {60, 0, 0},
        {240, zl, 0},         {210, zr, 0}};
    std::vector<GamepadFrame> frames;
    std::uint64_t index = 0;
    for (const Segment& segment : segments) {
        for (std::uint32_t i = 0; i < segment.frames; ++i) {
            GamepadFrame frame;
            frame.time_us = index * 1000000U / 60U;
            frame.pads[0].connected = true;
            frame.pads[0].buttons = segment.buttons;
            frame.pads[0].axes[static_cast<std::size_t>(GamepadAxis::left_x)] = segment.left_x;
            frames.push_back(frame);
            ++index;
        }
    }
    return frames;
}

// ---------------------------------------------------------------------------
// The CPU reference renderer
// ---------------------------------------------------------------------------

double drive_vertical_fov_degrees(std::uint16_t units,
                                  ChaseFovMode fov_mode,
                                  std::uint32_t width,
                                  std::uint32_t height) noexcept {
    const double half = chase_angle_radians(units) * 0.5;
    const double tangent = vehicle_sin(half) / vehicle_cos(half);
    double ratio = 0.75;
    if (fov_mode == ChaseFovMode::horizontal && width != 0) {
        ratio = static_cast<double>(height) / static_cast<double>(width);
    }
    return 2.0 * chase_atan(tangent * ratio) * (180.0 / kPi);
}

bool drive_render_camera(const ChaseCameraView& view,
                         ChaseFovMode fov_mode,
                         std::uint32_t width,
                         std::uint32_t height,
                         OrbitCamera& out,
                         std::string& error) {
    if (width == 0 || height == 0) {
        error = "frame dimensions must be positive";
        return false;
    }
    OrbitCamera camera;
    camera.yaw_deg = 180.0 - chase_angle_degrees(view.heading);
    camera.pitch_deg =
        static_cast<double>(static_cast<std::int16_t>(view.pitch)) * (360.0 / 65536.0);
    // Any distance in front of the eye gives the same view; ten metres keeps
    // the target inside the renderer's bounds wherever the eye is.
    camera.distance = 10.0;
    const double cp = det_cos_deg(camera.pitch_deg);
    const double sp = det_sin_deg(camera.pitch_deg);
    const double cy = det_cos_deg(camera.yaw_deg);
    const double sy = det_sin_deg(camera.yaw_deg);
    const Vec3 eye{view.eye.x, view.eye.z, -view.eye.y};
    camera.target = {eye.x - camera.distance * cp * cy, eye.y - camera.distance * sp,
                     eye.z - camera.distance * cp * sy};
    camera.fov_y_deg = drive_vertical_fov_degrees(view.fov, fov_mode, width, height);
    camera.near_plane = view.near_plane;
    camera.far_plane = view.far_plane;
    if (!validate_camera(camera, error)) {
        return false;
    }
    out = camera;
    return true;
}

bool drive_place_instances(const std::vector<PreparedInstance>& base,
                           const VehicleState& state,
                           const VehicleSpec& spec,
                           std::vector<PreparedInstance>& placed,
                           const CarAnimationState* animation) {
    if (placed.size() != base.size()) {
        return false;
    }
    const VehicleVec3 axes[3] = {rotate(state.orientation, VehicleVec3{1.0, 0.0, 0.0}),
                                 rotate(state.orientation, VehicleVec3{0.0, 1.0, 0.0}),
                                 rotate(state.orientation, VehicleVec3{0.0, 0.0, 1.0})};
    const VehicleVec3 com{static_cast<double>(spec.handling.centre_of_mass[0]),
                          static_cast<double>(spec.handling.centre_of_mass[1]),
                          static_cast<double>(spec.handling.centre_of_mass[2])};
    const VehicleVec3 origin = sub(state.position, rotate(state.orientation, com));
    // The car's matrix in the row-vector convention: rows 0..2 are the world
    // images of the body axes, row 3 the body origin.
    double car[4][4] = {{axes[0].x, axes[0].y, axes[0].z, 0.0},
                        {axes[1].x, axes[1].y, axes[1].z, 0.0},
                        {axes[2].x, axes[2].y, axes[2].z, 0.0},
                        {origin.x, origin.y, origin.z, 1.0}};
    for (std::size_t n = 0; n < base.size(); ++n) {
        placed[n] = base[n];
        double part[4][4];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                part[r][c] = base[n].transform[4 * r + c];
            }
        }
        if (animation && (base[n].part_slot == kSlotWheel ||
                          base[n].part_slot == kSlotBrake)) {
            const std::size_t wheel = base[n].copy;
            if (wheel >= kVehicleWheelCount ||
                !std::isfinite(state.wheels[wheel].compression) ||
                !std::isfinite(state.steer_angle) ||
                !std::isfinite(animation->wheel_phase[wheel])) return false;
            const auto rotate_part = [&](const double turn[4][4]) {
                double product[4][4]{};
                for (int r = 0; r < 4; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        for (int k = 0; k < 4; ++k) product[r][c] += part[r][k] * turn[k][c];
                    }
                }
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c) part[r][c] = product[r][c];
            };
            for (int c = 0; c < 3; ++c) part[3][c] -= base[n].animation_pivot[c];
            if (base[n].part_slot == kSlotWheel) {
                const double angle = animation->wheel_phase[wheel];
                const double co = vehicle_cos(angle), si = vehicle_sin(angle);
                const double spin[4][4] = {{co, 0, -si, 0}, {0, 1, 0, 0},
                                           {si, 0, co, 0}, {0, 0, 0, 1}};
                rotate_part(spin);
            }
            if (wheel < 2) {
                const double co = vehicle_cos(state.steer_angle);
                const double si = vehicle_sin(state.steer_angle);
                const double steer[4][4] = {{co, si, 0, 0}, {-si, co, 0, 0},
                                            {0, 0, 1, 0}, {0, 0, 0, 1}};
                rotate_part(steer);
            }
            for (int c = 0; c < 3; ++c) part[3][c] += base[n].animation_pivot[c];
            part[3][2] += state.wheels[wheel].compression;
        }
        if (animation) {
            if (base[n].part_slot == kSlotHeadlight) placed[n].lamp_mode = 1;
            if (base[n].part_slot == kSlotBrakelight)
                placed[n].lamp_mode = animation->braking ? 3 : 2;
        }
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                const double value =
                    part[r][0] * car[0][c] + part[r][1] * car[1][c] +
                    part[r][2] * car[2][c] + part[r][3] * car[3][c];
                placed[n].transform[4 * r + c] = static_cast<float>(value);
            }
        }
    }
    return true;
}

bool drive_draw_ground(const RenderOptions& options,
                       const VehicleVec3& centre,
                       Frame& frame,
                       RenderStats& stats,
                       std::string& error) {
    if (frame.width() != options.width || frame.height() != options.height) {
        error = "the frame is not the size the options describe";
        return false;
    }
    CameraFrame camera;
    if (!make_camera_frame(options.camera, options.width, options.height, camera, error)) {
        return false;
    }
    const RenderLimits limits;
    Rasterizer rasterizer(frame, options, limits, stats);
    RasterMaterial material;
    material.texture = nullptr;
    material.fallback[0] = 112;
    material.fallback[1] = 116;
    material.fallback[2] = 104;
    material.fallback[3] = 255;
    material.fallback_pattern = true;
    material.shade = 256;
    const double tile = 16.0;
    const int tiles = 20;
    const double x0 = floor_to(centre.x, tile) - tile * (tiles / 2);
    const double y0 = floor_to(centre.y, tile) - tile * (tiles / 2);
    // The checker is the renderer's fallback pattern, cells of 1/8 in u and
    // v: a texture coordinate of world / 32 checkers every 4 m.
    const auto vertex = [&camera](double x, double y) {
        ClipVertex v = project_point(camera, Vec3{x, 0.0, -y});
        v.u = x / 32.0;
        v.v = y / 32.0;
        return v;
    };
    std::uint32_t id = 0x70000000U;
    for (int j = 0; j < tiles; ++j) {
        for (int i = 0; i < tiles; ++i) {
            const double xa = x0 + tile * i;
            const double ya = y0 + tile * j;
            const ClipVertex a = vertex(xa, ya);
            const ClipVertex b = vertex(xa + tile, ya);
            const ClipVertex c = vertex(xa + tile, ya + tile);
            const ClipVertex d = vertex(xa, ya + tile);
            const ClipVertex first[3] = {a, b, c};
            const ClipVertex second[3] = {a, c, d};
            if (!rasterizer.draw_triangle(first, material, id++, error) ||
                !rasterizer.draw_triangle(second, material, id++, error)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace nfsu
