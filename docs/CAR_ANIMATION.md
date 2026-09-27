# Race car animation and lamps (issue #201)

This is visual state only. The 240SX vehicle, controls, collision, race timing,
residency and replay encoding are unchanged. The reference executable is the
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
  consumer that draws lamp glows has not been identified.

## Reproduction and approximations

`RaceSession` integrates each wheel's existing rolling speed after each
120 Hz vehicle step into a separate visual phase. It reads the existing
road-wheel steer angle and strut compression. The renderer composes wheel
half-turn, spin, front steering and hub translation in the retail order;
brakes reflect on the left and receive front steering but no spin. Positive
compression moves the hub up. Using the model's compression directly for
the retail display travel is an approximation because that writer remains
untraced. The visual phase, lamp choice and prepared instance transforms
are never serialized into the race state or returned to physics.

The headlight slot is always bright. The rear slot has a dim red tail
treatment and becomes brighter when the mapped brake or handbrake control
is held, including during countdown and reverse assist. The lamp colours
and intensities are explicit approximations. The CPU renderer and deko3d
fragment shaders use the same integer brightening rule on the existing
lamp mesh texels; they do not claim the game's glow sprite, halo or beam.
They add no asset or new texture to the repository. The shader's spare
material `alpha.w` lane carries the per-instance lamp mode without changing
the uniform size or the upload budget.

`PART_PLACEMENT.md` keeps the unverified x87 rounding, the alternative
wheel-offset selector and wheel-marker branch labelled. The stock lateral
correction moves the wheel rim away from the nearly coplanar wheel-arch
surface seen in PR #194 comment 4239. Whether it removes that specific
console CPU/GPU depth residual still needs a new #147 comparison photograph;
a desktop-only render cannot settle a console depth tie.

`DESKTOP_RACE.md` lists each changed race frame pin and its reason. The
static stock 240SX frame pins change because the lateral matrix now includes
the traced stock scale and offset. Event 1 still ends at 13:26.90 with the
same state and steps SHA-256 values.
