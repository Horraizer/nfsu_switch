# Controller input

How a controller moves the inspection camera, and later will drive a car:
the model a backend reports into, the rules that turn it into actions, the
recorded form a test states its input in, the camera it moves, and the
Joy-Con and Pro Controller mapping a libnx backend would implement. This is
issue #48; the service it arrives through is the input service of
[`PLATFORM_SERVICES.md`](PLATFORM_SERVICES.md).

## The property everything else rests on

A recorded sequence of frames replays to the same camera, bit for bit, every
time. Nothing in the controller path reads a clock: a frame carries its own
time, stamped by whoever sampled it, and every rule below decides from the
frames alone. So a test states its input as text and pins the camera it
produces, and a session sampled live can be written down and replayed.

That is measured, not assumed. `tests/input_tests.cpp` replays a scripted
session of 19 frames - a controller connecting with buttons held, a stick
exactly on the dead zone's edge and one unit past it, a stick pinned in the
corner of its square, `-32768`, two controllers pushing opposite ways, both
zoom triggers at once, a controller disappearing mid-gesture, two stalls of
five seconds, a reconnect holding reset, two view commands on one frame and
a frame with nothing connected - and pins the final camera and a digest of
the camera after every frame. The same bits come out of GCC 13 in the
default and Release configurations, Clang 18, MinGW-w64 GCC 13 running under
Wine, and GCC 13 for AArch64 at `-O3` running under QEMU - the last only
since `nfsu_inspect` is compiled without contraction; see **What is
guaranteed about determinism** below. The AArch64/QEMU run was measured
once, in the worker's cloud session for this branch, not reproduced on the
project's own toolchain, and nothing here has run on a console.

## The model

`include/nfsu/input.h`. A backend fills a `GamepadFrame` once per frame:

- `time_us`, microseconds on a monotonic clock, of which only differences
  are used;
- eight slots, each a `GamepadState`: whether it is connected, which buttons
  are held, and four axes.

Buttons are named by where they sit - `face_south`, `face_east`,
`face_west`, `face_north`, the two shoulders, the two triggers, `start`,
`select`, the two stick clicks and the four D-pad directions - because
controller families print different letters in the same place. Axes are the
two sticks, x positive to the right and y positive away from the player,
from `-32768` to `32767`; full deflection is `32767`, and `-32768` is read
as `-32767` so that both directions have one length.

Triggers are buttons, because on the Switch they are. A backend for a pad
with analog triggers thresholds them.

## Dead zones

One radial zone per stick, `StickDeadZone{inner, outer}` in raw units, by
default 3000 and 30000 - about 9 and 92 per cent of full deflection. At or
inside `inner` a stick reads as centred, exactly. At or beyond `outer` it
reads as fully deflected in the direction it points. In between, its length
is mapped linearly from `inner..outer` onto `0..1` and its direction kept, so
the output is continuous across both edges.

Radial rather than per axis, because a per-axis zone snaps a diagonal onto
the nearer axis. Both edges are decided by comparing the squared length, an
exact 64-bit integer, with the squared radius: a stick exactly on the edge is
centred on every platform, and `1800, -2400`, whose length is exactly 3000,
is the test's case for it. The zone is symmetric to the bit through the
centre, and a stick in the corner of its square, beyond the circle, keeps its
diagonal and reads no longer than full deflection.

## The remapping table

A table of `InputBinding`s binds a control - one button or one axis - to an
action id, optionally inverted. The actions and their kinds belong to the
consumer: an analog action is a value in `-1..1`, a digital one is on or off
with the frame it came on reported. The camera's set is in
`include/nfsu/input_camera.h`; driving controls would be a second set over
the same model.

`validate_input_bindings` refuses, naming the binding by its position: an
action past the end of the set; a control that is not a button or an axis; a
control named twice, because one control driving two actions is a remapping
mistake; an axis on a digital action, because a stick has no press; an
inverted digital action; and more than 64 bindings or 256 actions. A table
that is refused changes nothing.

## From frames to actions

`InputMapper`, one call per frame:

- **Connecting.** A button that is down on the frame its slot connects counts
  as up until it has been seen up. A controller plugged in, or woken, with a
  button held fires nothing, and neither does the button still held from
  launching the application.
- **Disconnecting.** A slot that is not connected contributes nothing, from
  the frame it disconnects on. A gesture it was making ends there: its sticks
  stop and its held buttons are released without firing anything, because a
  digital action fires on a press, not a release. On reconnecting it is
  connecting again.
- **Adding up.** Analog sources are added slot by slot in ascending order,
  and within a slot in table order, then clamped to `-1..1`. Two sources
  pushing opposite ways cancel exactly; two pushing the same way are no
  faster than one at full deflection. The order is fixed, so the rounding of
  the sum is too.
- **Pressing.** A digital action is held while any source of it is held on
  any slot, and pressed on the frame it changes from not held to held. A
  second controller pressing what the first holds is not a new press.
- **Time.** The interval is the difference from the previous frame's time,
  clamped to 100 ms, so a frame after a stall - a suspended application, a
  debugger - moves the camera no further than a frame at 10 frames per
  second would. The first frame reports no interval, and so does a frame
  whose time is earlier than the one before.

## The camera

`include/nfsu/input_camera.h`. `GamepadCamera::step` reads one frame and
moves an `Inspector`'s `OrbitCamera` through `Inspector::orbit`, `pan`,
`zoom` and `command` only - the calls the desktop pointer makes - so every
clamp and wrap that keeps the camera valid under hostile pointer input keeps
it valid here, and whichever renderer draws `Inspector::options()` shows the
result. It renders nothing and knows no renderer.

Each frame applies, in this order: the orbit, the pan, the zoom, then every
command pressed on that frame in `InspectorCommand` order. Motion is the
action's value times its rate times the interval:

| Rate | Default | Unit |
| --- | --- | --- |
| orbit | 120 | degrees of yaw or pitch per second |
| pan | 0.75 | viewport heights per second, so the speed is the same at any resolution and drag divisor |
| zoom | 6 | steps per second, a step being `Inspector::zoom(1)`; the fraction of a step not yet whole is carried, as the window carries a fraction of a wheel notch |

Each rate must be finite, above 0 and at most 3600, 100 and 240. Degrees are
handed to `Inspector::orbit` as pointer pixels at the inspector's own
sensitivity, so the orbit rate does not change when the pointer's does.

**The convention is that the stick moves the camera.** Pushing the right
stick right swings the eye along the camera's right vector around the
target; up raises it; the left stick carries the target the way it points.
`tests/input_tests.cpp` checks each direction against the basis
`make_camera_frame` builds, not against a sign chosen in the test. For the
orbit this is what the pointer already does - measured, a pointer drag right
lowers yaw, which moves a point between the target and the eye towards the
left of the screen, from 0 to -0.039 in normalised coordinates for ten
degrees - and for the pan it is the opposite of the pointer's grab, which
carries the target against the drag. `include/nfsu/inspect.h` and
[`INSPECTOR.md`](INSPECTOR.md) describe the pointer's orbit as turning "the
side that faces the camera towards the right of the screen"; the
measurement above says the near side moves left. That wording predates this
change and is left to the coordinator. The table's invert flag is how a
player who wants the other convention on an axis gets it.

While any motion action is nonzero the step reports `moving`, and a session
renders a moving camera at 1 / drag divisor of the surface, as the window
renders a drag; the full size returns on the frame the motion stops,
including when a controller disappears mid-gesture.

### The default layout

| Control | Nintendo label | Action |
| --- | --- | --- |
| right stick | right stick | orbit |
| left stick | left stick | pan |
| `trigger_right` | ZR | zoom in |
| `trigger_left` | ZL | zoom out |
| `start` | + | end the session |
| `select` | - | reset the camera |
| `dpad_up`, `dpad_right`, `dpad_down`, `dpad_left` | D-pad | front, right, rear and left views |
| `stick_right` | R stick click | top view |
| `stick_left` | L stick click | frame the whole scene |
| `face_east` | A | frame the selection |
| `face_south` | B | clear the selection |
| `shoulder_right` | R | next material group |
| `shoulder_left` | L | previous material group |
| `face_north` | X | cycle the view |
| `face_west` | Y | cycle the overlay |

Every control is bound once. The mesh walk, the highlight, culling, shading,
the retail material state, vertex colour and V flip have no control by
default and are reached by remapping. The test compares the default table
with this one binding for binding.

## The recorded form

ASCII text, one line per connected slot per frame:

```text
nfsu-input 1
# a comment
0
16000 0 - 0 0 0 0
32000 0 face_east+trigger_right 0 0 32767 -120
32000 1 - 0 0 0 0
```

The first line is the header. Every other line is a time in microseconds,
optionally followed by one connected slot: its number, its held buttons as
names joined by `+` or `-` for none, and its four axes in the order
`left_x left_y right_x right_y`. Consecutive lines with one time are one
frame, and a slot a frame does not name is disconnected in it, so a line
holding a time alone is a frame with nothing connected - which is how a test
states a disconnect. Blank lines and lines starting with `#` are ignored;
lines end in LF or CR LF.

The parser is strict, because a recording is what a camera is pinned to and
a line that could be read two ways should be refused rather than read one of
them. It refuses, naming the line: a missing or different header; any byte
outside printable ASCII, a tab included and a comment included; a number
that is not canonical - a sign on a time or a slot, a leading zero, `-0`, or
a value out of range; an unknown button or one named twice; a slot named
twice in one frame; a time alone in a frame that also names a slot, or the
reverse; a time earlier than the one before; more than a million frames; and
more than 16 MiB. Nothing is appended unless the whole text parses.

`format_input_recording` writes the canonical text: LF, one space, slots in
ascending order, buttons in enumeration order. It records what the mapper
reads rather than what a backend happened to fill in - a disconnected slot is
left out whatever its fields hold, bits that are not buttons are dropped, and
axes are clamped into range - so a live session written down replays to the
camera it produced.

## Hostile input

| Case | What happens | Where it is tested |
| --- | --- | --- |
| a controller disappears mid-gesture | the camera stops on that frame and stays stopped; nothing fires; the drag viewport returns | `tests/input_tests.cpp` camera and mapper tests, `tests/platform_tests.cpp` frame loop |
| a button is held as its controller connects | latched until released, including across a reconnect and a remapping | both mapper and camera tests |
| an axis pinned at its extreme, for 3000 frames of five-second stalls | the camera stays valid, yaw stays wrapped, pitch rests on its limit | `test_camera_hostile` |
| `-32768` and values past the range | read as the extreme they are clamped to, in the same direction | `test_dead_zone` |
| conflicting inputs | opposite sticks, buttons or controllers cancel exactly; same-way sources are no faster than one; two commands on one frame both apply, in order | mapper and camera tests |
| the dead zone edge | exactly on it: centred and not moving; one unit past: moving | `test_dead_zone`, `test_camera_hostile` |
| time going backwards | no interval live; refused in a recording | mapper and recording tests |

## What is guaranteed about determinism, and how it was measured

Every library on the path from a frame to a camera is compiled with
floating-point contraction disabled: `nfsu_input`, `nfsu_input_camera`,
`nfsu_inspect`, whose `orbit`, `pan` and `zoom` do the camera arithmetic,
and `nfsu_render`, whose `make_camera_frame` gives `pan` the camera's axes.
The library calls on the path are `std::sqrt`, which IEEE 754 requires to
be correctly rounded, and `std::floor` and `std::fmod`, which are exact;
sine and cosine are the renderer's own polynomials, `det_sin_deg` and
`det_cos_deg`, not the library's. So a recorded session replays to the same
camera bits wherever double arithmetic is IEEE 754 binary64 without extended
precision, as it is on x86-64 and AArch64.

`nfsu_inspect` did not have that option before this change, and the reason
it has it now was measured rather than assumed - once, in the worker's
cloud session for this branch, cross-compiling and running under QEMU
there, not reproduced on the project's own toolchain, and nothing here has
run on a console. GCC 13 cross-compiling for AArch64 - the console's
architecture - at `-O3` fuses three multiply-adds into `Inspector::orbit`.
Run under QEMU, that build failed two tests: the pinned session's per-frame
digest (`0x4D5D67E655FC6F3D` against `0x7BE849D74BE1CBE8`; the final camera
happened to converge back, which is why the digest is taken after every
frame), and the existing `nfsu_inspect_tests`, whose "the orbit is
reversible" is false once an orbit and its inverse each round only once.
With the option on `nfsu_inspect` alone, the function has no fused
instruction and every test that can run under QEMU passes, 28 of 28.
x86-64 without FMA has nothing to fuse, so every desktop build is
unchanged, the pinned values included.

## The Joy-Con and Pro Controller mapping a libnx backend would implement

Configure once with `padConfigureInput(8, HidNpadStyleSet_NpadStandard)`,
which admits the Pro Controller, the two Joy-Con held together or in the
console, and a single Joy-Con. Slot 0 is `padInitializeDefault`, which reads
the console's own controls and player 1 as one; slots 1 to 7 are
`padInitialize` for players 2 to 8. Each frame, `padUpdate` each pad; a slot
is connected when `padIsConnected` says so.

Buttons come from `padGetButtons`, the held state. The model computes its
own presses, so `padGetButtonsDown` must not be used for it, or the connect
latch is bypassed.

| `GamepadButton` | `HidNpadButton` | Bit |
| --- | --- | --- |
| `face_east` | `A` | 0 |
| `face_south` | `B` | 1 |
| `face_north` | `X` | 2 |
| `face_west` | `Y` | 3 |
| `stick_left` | `StickL` | 4 |
| `stick_right` | `StickR` | 5 |
| `shoulder_left` | `L` | 6 |
| `shoulder_right` | `R` | 7 |
| `trigger_left` | `ZL` | 8 |
| `trigger_right` | `ZR` | 9 |
| `start` | `Plus` | 10 |
| `select` | `Minus` | 11 |
| `dpad_left` | `Left` | 12 |
| `dpad_up` | `Up` | 13 |
| `dpad_right` | `Right` | 14 |
| `dpad_down` | `Down` | 15 |

Only these bits. libnx's `HidNpadButton_AnyLeft` and its siblings also hold
the stick-direction bits, and mapping one of them would make a stick press
the D-pad. HOME and Capture are not readable by an application and are not
in the model. The single Joy-Con's SL and SR (bits 24 to 27) have no
`GamepadButton`; see below.

Sticks come from `padGetStickPos(&pad, 0)` for the left stick and
`padGetStickPos(&pad, 1)` for the right. libnx reports each axis as a signed
32-bit value within `JOYSTICK_MIN..JOYSTICK_MAX`, `-0x7FFF..0x7FFF`, which is
exactly `-kAxisMax..kAxisMax`, so the values pass through unscaled. The model
takes y as positive away from the player; that the Switch reports it that
way is expected and not verified here, and a backend that finds it
otherwise negates y.

These names were checked against libnx's own headers: `pad.h` for the calls,
`hid.h` for the bit values and the stick range. Nothing here has run on a
console.

Issue #148 implements the button table as
`nfsu::gamepad_buttons_from_npad` (`include/nfsu/stock_view.h`), neutral
code a desktop test checks bit by bit, and `switch/car_main.cpp` checks its
constants against libnx's enumerators at compile time. That application
reads slot 0 only - `padConfigureInput(1, ...)` and
`padInitializeDefault` - and binds its own table over the camera actions:
the left stick orbits, - resets the camera and + exits
([`SWITCH_BUILD.md`](SWITCH_BUILD.md)). It has not been run on a console
either.

## Not decided yet

- **A desktop controller.** The Win32 backend reports no controller. XInput
  or SDL would be the candidates; neither is chosen.
- **A single Joy-Con held sideways.** It has one stick, four face buttons in
  a turned layout, and SL and SR. How that maps onto this model is open.
- **Held commands repeating.** A held D-pad fires once; the keyboard repeats.
- **Which controller owns the camera.** Every connected controller drives it,
  added up. Per-player cameras are open.
- **Driving controls.** A second action set - steering, throttle, brake -
  over this model and this recorded form, with analog triggers where a pad
  has them.
- **Keeping a remapping.** Tables are values; no file format stores one.
- **Recording the pointer and keys.** Only controller frames are recorded.

## Where this lives

- `include/nfsu/input.h`, `src/input.cpp`, `src/input_recording.cpp` - the
  model, the dead zone, the table, the mapper and the recorded form
  (`nfsu_input`).
- `include/nfsu/input_camera.h`, `src/input_camera.cpp` - the camera's
  actions, the default layout, the rates and `GamepadCamera`
  (`nfsu_input_camera`).
- `tests/input_tests.cpp` - the recorded form and its refusals, the dead
  zone's edges, the table's refusals and the default layout, the mapper's
  rules, the camera's motion and conventions, the hostile cases, and the
  replayed session pinned byte for byte.
