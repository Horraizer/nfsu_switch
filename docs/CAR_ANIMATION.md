# Race car animation and lamps (issue #201)

Wheel phase is visual state. Authored road starts also publish their original
NIS steering and car placement. The reference executable is the
retail `speed.exe` identified by SHA-256 in `PART_PLACEMENT.md`.

## Retail observations

- `0x004B192A` advances the car's 16-bit wheel spin angle at `car+0x108`.
  `0x004B1A31` builds each wheel's spin matrix from that angle. The node
  loop's first two wheels also receive a steering turn at
  `0x004B1A39..0x004B1A7E`; its brake matrix steers with them but does not
  receive the spin turn.
- `0x004B1A87..0x004B1AC4` copies the three wheel-record position floats
  from `RideInfo+0x70+k*0x30` into both node matrices. The third float is
  vertical. The writer that makes its displayed value follow suspension
  travel was not recovered. The render caller copies four 0x40-byte wheel
  and brake matrices from node `+0x790` and `+0x890` at
  `0x00570A77..0x00570B02`.
- `0x0056F945..0x0056FBCF` rebuilds the drawn lateral terms from the
  wheel-table values; `PART_PLACEMENT.md` gives the stock branch and the
  multiply order. For the 240SX it moves the wheel model origin from the
  culling-table `+/-0.73` to `+/-0.82094` front and `+/-0.80067` rear.
- The headlight and brakelight are installed part slots 13 and 14
  (`CAR_SCENE.md`). The dispatch at `0x0056F4B0..0x0056F4D0` sends both
  through the generic draw at `0x0056F557..0x0056F5FE`, using the car
  matrix directly (`0x0056F5EB`). That branch substitutes material keys
  `0xD6D6080A` and `0x471A1DCA` when corresponding pointers are present
  (`0x0056F593..0x0056F5D8`). Its relationship to the lamp control is
  untraced. The collector at `0x0056BDB0..0x0056C0DC` also records
  `LEFT_HEADLIGHT`, `RIGHT_HEADLIGHT` and brakelight markers, but the
  consumer is traced further in [LIGHTS_GLOWS.md](LIGHTS_GLOWS.md).

## Reproduction and approximations

`RaceSession` integrates each wheel's existing rolling speed after each
120 Hz vehicle step into a separate visual phase. It reads the existing
road-wheel steer angle and strut compression. The renderer composes wheel
half-turn, spin, front steering and hub translation in the retail order;
brakes reflect on the left and receive front steering but no spin.
Positive compression moves the hub up. Original 478B20 and the following
three wheel blocks cap that travel with the eighth float of the active
axle strut record (+0x1C). Original 465150 adds the published travel to
the active setup mount, which includes suspension tuning. Race wheel and
brake placement offset the stock mesh pivot accordingly. The camera's
wheel-transform sample uses the same travel cap and active mount. Force
calculations continue to use the strut's physical compression.

APPROXIMATION - wheel spin source: retail `0x004B18F7..0x004B192A`
advances one car-level 16-bit angle at `car+0x108` from road distance and
one radius, truncating the scaled turn at `0x00674898`. The wheel loop
reloads that same angle for all four wheels at `0x004B1950..0x004B1953`.
This implementation instead integrates the four existing simulated wheel
spin values because the current vehicle state exposes those rates but no
shared retail road-distance angle. During a burnout or a locked rear wheel,
the displayed rear phase therefore differs from retail. The matrix order
above follows the traced draw path; the phase source does not.

## Authored road-start cars

Normal race starts bind the original `ZPM_Car1` through `ZPM_Car4` root
channels. Before the event48 correction, these moved the chassis through
`RaceSession` while leaving wheel phase at zero. The physical wheel speeds
are not the source during NIS movement mode0. Its published speed magnitude
is a synthetic value and cannot recover the authored travel.

PC `47C310/47C580` and GameCube `80087260/800874E8` create two half-speed
controllers per car. The second starts one second ahead. Their forward-axis
angle difference, wrapped as a signed 16-bit turn, steers the front wheels.
The full original channels are retained for this look-ahead, including keys
beyond the stage's release frame. GameCube `802113BC` supplies the key count
as the controller end; `800875A8` tests the scaled old cursor plus the current
interval before advancing `800856AC`. The finishing update holds the last
pose and does not add wheel travel.

PC `47C854..47C955` and GameCube `8008795C..80087A64` preserve each wheel's
previous float phase, calculate unsigned XY distance between successive root
placements and use each wheel's active radius. The initial placement, a zero
interval and a finished controller add no distance. The GameCube accumulator
divides distance by `(radius + radius) * pi`, doubles that quotient and uses
one fused multiply-add with the old phase. The phase stays in radians and
is not wrapped by the NIS update. First placement and stage-release teleports
are excluded. Normal physics subsequently continues from these individual
phases, so the countdown and live handover do not replace them with zero.

`RaceStartCarMotion::sample` owns the controllers and publishes animation and
steering together with the grounded chassis. `RaceGpuScene` consumes that
same session animation for both the cinematic camera and the game camera.
The existing wheel and brake matrix composition remains the presentation
path for stock and preset rims, independent spin, steering and suspension.

`nfsu_start_car_wheels_tests` pins original arithmetic witnesses, including
controller end/zero-time cases. `nfsu_start_car_wheels_retail` reads the
original start playlists, samples their bound slots, checks real car/rim
variants and held poses, and verifies independent live spin and steering.
The `nfsu_event48_start_wheels_*_retail` tests compare all four wheel and
brake placements with actual Switch scene commands through the intro,
countdown, skip and GO. A terminal authored snap is allowed once, only after
rolling, and every later cinematic chassis pose must remain fixed.

Reproduce the read-only native witnesses with Python3.9 or newer, Unicorn,
the PC1.4 installation and the original GNDE69 disc:

```powershell
python -B tools/start_car_wheels_native.py --root $retail --disc $disc --out build/start-wheels-native
```

The script authenticates the inputs, runs bounded original instructions with
explicit controller/phase storage, and compares the tracked fixture. It does
not claim a running original game or a Switch display check. The independent
event48 GameCube trace supplied to this worker contains advancing per-wheel
phase during mode0 and confirms that wheel rotation exists in the real intro.
Hardware confirmation of the corrected Switch presentation remains required.

The headlight slot is always bright. The rear slot has a dim red tail
treatment and becomes brighter when the mapped brake or handbrake control
is held, including during countdown and reverse assist. The lamp colours
and intensities are explicit approximations. The CPU renderer and deko3d
fragment shaders use the same integer brightening rule on the existing
lamp mesh texels; they do not claim the game's glow sprite, halo or beam.
They add no asset or new texture to the repository. The optional marker-driven
sprites described in [LIGHTS_GLOWS.md](LIGHTS_GLOWS.md) are a separate
approximation. The shader's spare material `alpha.w` lane carries the
per-instance lamp mode without changing
the uniform size or the upload budget.

`PART_PLACEMENT.md` keeps the unverified x87 rounding, the alternative
wheel-offset selector and wheel-marker branch labelled. The stock lateral
correction moves the wheel rim away from the nearly coplanar wheel-arch
surface seen in PR #194 comment 4239. Whether it removes that specific
console CPU/GPU depth residual still needs a new #147 comparison photograph;
a desktop-only render cannot settle a console depth tie.

`DESKTOP_RACE.md` lists each changed race frame pin and its reason. The
static stock 240SX frame pins change because the lateral matrix now includes
the traced stock scale and offset. The Event1 replay result and state hashes
are pinned by the race-run retail tests.
