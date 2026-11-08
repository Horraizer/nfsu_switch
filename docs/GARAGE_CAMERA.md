# Manual garage camera

The Switch right stick enters the original manual FE camera mode. It must
not retain a detail camera's translation, look target or narrow field of
view indefinitely. Those values were left in place by the old port orbit,
whose lower pitch limit could also move the eye underneath the road.

## Original chain

Authenticated PC 1.4 USA `speed.exe`:

- `004870C0`, state3: apply angular inputs, wrap yaw, clamp pitch to85..140
  degrees and distance to6, then call `00487F50`.
- `00487F50`: return roll to the nearest zero, FOV to45, translation to
  `(0,.75,0)` and target to `(0,0,.75)`. The maximum of four normalized
  return times synchronizes the channels. Angular rates are75/50 degrees
  per second; vector rates are2.2 units per second.
- `00488190` and `00488280`: ignore vectors whose components are each
  within.001 of the destination; otherwise use Euclidean distance.
- `00487680`: construct the same original view, with the existing raw to
  world coordinate conversion. Part focus and magazine presets are separate
  modes and retain their original transitions.

`FrontendBrowserCamera::orbit` receives the port's finite stick deltas.
`update` continues the manual return after the stick is released. Neutral
input alone does not cancel a detail transition. Entering another focus,
car-switch preparation or magazine view exits manual mode.

## Verification and limits

`tools/frontend_orbit_native.py` authenticates the executable, executes its
constructor, slot setter, manual update and view builder in Unicorn, and
produces `tests/frontend_manual_camera_samples.inc`. No camera arithmetic
is stubbed; later renderer submission is outside this bounded execution.
The probe includes every vinyl layer and whole-car, bumper, rim, engine
accent paint, brake-caliper paint and rim-paint focus inputs.

`nfsu_frontend_tests` compares manual state and view matrices with those
native samples, sweeps both pitch limits and yaw wrapping, and checks mode
changes. `nfsu_frontend_retail` also drives the resident garage scene through
the public orbit API, checks its submitted eye/frustum and renders vinyl
views with the reference renderer. Existing native focus, browser and
magazine samples continue to cover their unchanged modes.

These checks do not establish pixel parity with the original renderer or
Switch hardware acceptance. This correction restores the manual controller;
it does not add a new world-collision spring arm or change race cameras.
