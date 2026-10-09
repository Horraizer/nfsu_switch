# Atlantic reference and R3 rear view, 2026-10-09

Owner supplied video_2026-10-09_18-44-21.mp4 as the original second-track
reference and requested right-stick click held to look back from the front
of the car, release to restore the selected ordinary camera.
Base bbf6a3cedc1f93829cc6fa58fc0f38aa91445970 includes the pending theater
steering correction; owner acceptance of that episode is still unconfirmed.
Own tree: build/day20261008-root-combined. No delegates, push or main merge.

## Reference observations

Video identity and contact sheet are preserved under
build-artifacts/draw-distance-20261008-root/atlantic-reference-20261009.
The clip is57.735578s,1280x800,H264,nominal60fps; this is not a performance
measurement. Original world files remain read-only.

- Lighting and road highlights change under the bridges; this does not
  establish a global exposure algorithm.
- Environment reflections, smoke and exhaust flashes are visible. Their
  original activation rules are not established by the video alone.
- At high speed there is broad scene blur/radial streaking. Existing isolated
  light glow/history streaks do not implement that whole-scene effect.
- During the airborne/overturned section, camera orientation/framing changes.
  Our current camera uses fixed world up and simplified pitch/zoom.
- This clip mainly shows a rear chase view; it does not establish the full
  cycle of original POVs or the precise original look-back geometry.

These observations are recorded for sequential future tasks. Only R3 is
implemented here; effects, existing POV list, physics and interface stay as
before. No matched original/port frame comparison is claimed.

## Implemented behavior

Switch's existing npad mapping already carries right-stick click into
GamepadButton::stick_right. DriveControls exposes a held look_back flag from
live, connected, unlatched buttons. It is a fixed owner-requested R3 mapping,
not an added remapping action or a POV cycle. Hot-plug held-button policy is
retained. Release/disconnect clears the temporary view.

ChaseCamera continues updating the ordinary selected POV and its history.
For the published temporary view only, it mirrors the full horizontal chase
arm ahead of the car, rotates horizontal forward/up by180degrees, retains
height/downward pitch/FOV and checks obstruction on that front ray. Release
uses the current normal view directly, with no orbit or stale held history.
The normal camera collision reach is not overwritten by the front query.

This is a port projection adapter implementing the owner's interaction;
the original retail rear-view mover/offsets have not been recovered here.
RaceSession and standalone DriveSession both consume the same held flag.
When R3 is not held, the existing camera computations and encoded record
format remain unchanged. No rear flag is appended to the old replay schema;
the resulting view is already serialized and input records retain R3.

## Targeted validation

- Fresh native affected dependencies and nfsu_race_session_tests built in
  the existing Release configuration. Targeted CTest passes1/1.
- Cases cover R3 hot-plug latch, held/release/disconnect, all three current
  selected POVs and paired moving/turning sessions. Front placement/backward
  gaze, unchanged vehicle motion and exact ordinary-view restoration on
  release are checked. No old expectation/hash was changed.
- Actual Switch SDK Release NRO build completed. NRO, embedded-shader and
  frontend-profile contract tests pass3/3. Tag rear-view-r1; existing graphics
  settings and the preceding steering correction are retained.
- Owner requested minimal relevant tests; no full unrelated matrix run or
  complete green native gate claimed. Existing steering116 failed assertions
  are retained as documented in root-steering-world-20261009.md.
- Hardware R3 behavior remains pending. No upload performed in this step.
  Wait for current owner readiness/command; the earlier steering diagnostic
  nxlink connection must not be replaced while it remains active.
