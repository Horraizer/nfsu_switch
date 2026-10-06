#pragma once

#include "nfsu/chase_camera.h"
#include "nfsu/drive_controls.h"
#include "nfsu/input.h"
#include "nfsu/render.h"
#include "nfsu/scene.h"
#include "nfsu/sha256.h"
#include "nfsu/vehicle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nfsu {

// The loop issue #59 adds between a controller and a frame: one controller
// frame in, the controls, two vehicle steps, the chase camera, and a record
// of all of it hashed, so that a recorded input stream pins everything it
// drives. Also what the CPU reference renderer needs to show the result: the
// car placed at its pose, the camera turned into the renderer's camera, and
// a flat ground to see the motion against. docs/CONTROLS_CAMERA.md.
//
// The fixed rate. One controller frame is one 1/60 s tick, the rate the
// console samples its pads at and the game's own world tick (0x00585D70,
// docs/VEHICLE.md); the vehicle runs two of its 1/120 s steps in each and
// the camera one 1/60 s update. A frame's time field is not read: a replay
// is one tick per recorded frame, whatever the clock said, and so is the
// live loop that recorded it. Nothing here reads a clock.

constexpr std::uint32_t kDriveFramesPerSecond = 60;
constexpr std::uint32_t kDriveStepsPerFrame = kVehicleStepsPerSecond / kDriveFramesPerSecond;
constexpr double kDriveFrameSeconds = 1.0 / 60.0;

// One frame's record, as the hash reads it: the frame number (u64); per
// vehicle step the input as applied (throttle, brake, steer as doubles, the
// handbrake and the shift as u32) and the vehicle's telemetry record
// (encode_vehicle_telemetry); then the camera (POV, heading, target, lag,
// pitch and field of view as u32, the eye, forward and up vectors and the
// reach as doubles) and the frame's camera and reset edges (u32). All
// little-endian, every double as its IEEE-754 bits.
constexpr std::size_t kDriveInputRecordBytes = 3 * 8 + 2 * 4;
constexpr std::size_t kDriveCameraRecordBytes = 6 * 4 + 9 * 8 + 8;
constexpr std::size_t kDriveFrameRecordBytes =
    8 + kDriveStepsPerFrame * (kDriveInputRecordBytes + kVehicleTelemetryRecordBytes) +
    kDriveCameraRecordBytes + 2 * 4;

void encode_drive_frame(std::uint64_t frame,
                        const VehicleInput (&inputs)[kDriveStepsPerFrame],
                        const VehicleTelemetry (&telemetry)[kDriveStepsPerFrame],
                        const ChaseCameraView& view,
                        const DriveFrameState& events,
                        std::vector<std::uint8_t>& out);

struct DriveSessionSetup {
    VehicleSpec spec;
    DriveControlsConfig controls;
    ChaseCameraTable camera_table;
    ChaseCameraRules camera_rules;
    ChaseCameraApproximations camera_approx;
    // The chase POV the camera starts at (0, 1 or 4).
    int pov = 0;
    VehiclePose pose;
};

// The stock 240SX's handling and camera anchors, the default controls, the
// recovered camera rules with the default approximations, POV 0, at rest at
// the origin facing +x with the automatic gearbox.
DriveSessionSetup default_drive_session_setup();

class DriveSession {
public:
    DriveSession() = default;
    DriveSession(const DriveSession&) = delete;
    DriveSession& operator=(const DriveSession&) = delete;

    // Builds the model, the controls and the camera, resets the car at the
    // pose and the camera behind it, and starts the hash. False, with error
    // set, when any of them refuses.
    bool start(const DriveSessionSetup& setup, std::string& error);

    // One tick: the controls read the frame, the vehicle takes two steps
    // over flat ground at height 0 (issue #60 supplies the track), the camera
    // takes the camera edge and then one update, and the frame's record is
    // hashed. False, with error set, when a step or the camera refuses; the
    // session then stays where it was.
    bool advance(const GamepadFrame& frame, std::string& error);

    std::uint64_t frames() const noexcept { return frames_; }
    const VehicleState& state() const noexcept { return state_; }
    const VehicleTelemetry& telemetry() const noexcept { return telemetry_; }
    const VehicleInput& last_input() const noexcept { return last_input_; }
    const ChaseCameraView& view() const noexcept { return view_; }
    const DriveFrameState& frame_state() const noexcept { return controls_.frame_state(); }
    const VehicleSpec& spec() const noexcept { return setup_.spec; }
    const ChaseCamera& camera() const noexcept { return camera_; }

    // The SHA-256 of every frame record so far, in order. finish() may be
    // called once; the session keeps going and hashing is then over.
    std::uint64_t hashed_bytes() const noexcept { return hashed_bytes_; }
    Sha256Digest finish() noexcept { return sha_.finish(); }

private:
    DriveSessionSetup setup_;
    std::unique_ptr<VehicleModel> model_;
    DriveControls controls_;
    ChaseCamera camera_;
    VehicleFlatGround ground_;
    ChaseCameraOpenWorld world_;
    VehicleState state_;
    VehicleTelemetry telemetry_;
    VehicleInput last_input_;
    ChaseCameraView view_;
    std::uint64_t frames_ = 0;
    Sha256 sha_;
    std::uint64_t hashed_bytes_ = 0;
    std::vector<std::uint8_t> record_;
};

// The built-in demonstration: a Switch pad in slot 0 driving the stock car
// for 23 seconds, 1380 frames at 60 a second, stamped frame * 1000000 / 60
// microseconds. docs/CONTROLS_CAMERA.md lists what it does when; in short
// full throttle, a stick-steered left bend, a D-pad-steered right one,
// braking into a right turn, the handbrake, the next chase view, braking to
// a stop and on into reverse by the assist, and forward again.
std::vector<GamepadFrame> drive_demo_recording();

// The SHA-256 of the demonstration's frame records through
// default_drive_session_setup(), as the desktop Debug and Release builds
// give it: docs/CONTROLS_CAMERA.md pins it and nfsu_drive_tests requires it.
constexpr const char* kDrivePinnedDemoSha256 =
    "1CC5FD8A2A2BD2A7746F434CD309B99B63A20AFC7CB645A0CB2D0DCDA7BB958C";

// ---------------------------------------------------------------------------
// Showing it with the CPU reference renderer
// ---------------------------------------------------------------------------

// The renderer's camera for a chase view on a width x height frame. The
// renderer's world is y-up, (x, z, -y) of the vehicle's z-up world; its
// OrbitCamera is set so that its eye is the view's eye to rounding and it
// looks along the view's forward vector: yaw 180 - heading, pitch the
// view's pitch, both in degrees, which are exact for 16-bit angles. The
// vertical field of view follows the camera's ChaseFovMode. False, with
// error set, for a frame size of 0 or a camera the renderer refuses.
bool drive_render_camera(const ChaseCameraView& view,
                         ChaseFovMode fov_mode,
                         std::uint32_t width,
                         std::uint32_t height,
                         OrbitCamera& out,
                         std::string& error);

// The vertical field of view, degrees, for a POV's angle in `units` on a
// width x height frame. Exposed for tests.
double drive_vertical_fov_degrees(std::uint16_t units,
                                  ChaseFovMode fov_mode,
                                  std::uint32_t width,
                                  std::uint32_t height) noexcept;

struct CarAnimationState {
    // Integrated visual phase in radians, one value per wheel. It is never
    // encoded into the simulation or replay state.
    std::array<double, kVehicleWheelCount> wheel_phase{};
    bool braking = false;
};

// Copies each of `base`'s instance transforms, composed with the car's
// body-to-world transform, into `placed`, which must hold as many instances
// (the rest of each instance is copied too). The optional visual state
// spins and steers wheels, moves their suspension hubs and sets lamp modes.
// The row-vector car matrix is applied after each part matrix. Computed in
// double and stored as float.
bool drive_place_instances(const std::vector<PreparedInstance>& base,
                           const VehicleState& state,
                           const VehicleSpec& spec,
                           std::vector<PreparedInstance>& placed,
                           const CarAnimationState* animation = nullptr);

// Draws a flat checkered ground at height 0 into a frame render_scene has
// just drawn, with the same options, so that depth hides it behind the
// car: 16 m tiles out to 160 m around `centre`, checkered every 4 m by the
// renderer's own fallback pattern. The draws are counted into `stats`, a
// separate RenderStats. False, with error set, when the renderer refuses.
bool drive_draw_ground(const RenderOptions& options,
                       const VehicleVec3& centre,
                       Frame& frame,
                       RenderStats& stats,
                       std::string& error);

} // namespace nfsu
