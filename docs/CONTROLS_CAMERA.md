# Driving controls and the chase camera

Issue #59: a controller frame becomes the vehicle model's input once per
fixed step, through a remapping table with per-axis calibration, and a
chase camera follows the car the way the game's does, with its smoothing
and collision stated as parameters. The first lap of issue #159 is driven
through this layer: a Switch pad, ZR and ZL digital, the sticks analog.

The code:

| Library | Files | What it is |
| --- | --- | --- |
| `nfsu_drive_controls` | `include/nfsu/drive_controls.h`, `src/drive_controls.cpp` | the remapping table and its file, per-axis calibration, digital against analog, the throttle shaping, the steering filter, the reverse assist |
| `nfsu_chase_camera` | `include/nfsu/chase_camera.h`, `src/chase_camera.cpp` | the per-car anchors of CarTypeInfo +0xB50, the recovered heading rules and lag, the field of view, collision over a world the caller supplies |
| `nfsu_drive_session` | `include/nfsu/drive_session.h`, `src/drive_session.cpp` | one controller frame through the controls, two vehicle steps and the camera, hashed; the built-in demonstration; the renderer's camera, the car placed at its pose and a flat ground for the CPU reference renderer |

All three are declared for both configures in `cmake/drive_libraries.cmake`
and classified neutral for `nfsu_platform_neutrality`. The probe command is
`drive-replay`, the tests `nfsu_drive_tests` and
`nfsu_probe_drive_replay_guards`, and on the console `nfsu_switch_drive`
with the census `nfsu_switch_fma_census`.

Labels, as in [`VEHICLE.md`](VEHICLE.md):

- **RECOVERED**: read from `speed.exe` or `GLOBALB.BUN`, with the address.
  A value is recovered when its bytes are read at a named address; a rule
  when the instructions that apply it were read.
- **INFERENCE**: a meaning given to a recovered value on evidence weaker
  than a traced reader; the text says what it rests on.
- **APPROXIMATION**: invented here, with the reason. Every one is a field of
  `DriveControlsConfig` or `ChaseCameraApproximations`, or a named rule.

## Evidence base

| File | SHA-256 | What it gives |
| --- | --- | --- |
| `speed.exe` | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` | the rules; file offset = VA - 0x400000 for `.text`, `.rdata` and `.data`, checked against the PE section table |
| `Global/GLOBALB.BUN` | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` | the camera anchors of every car type, CarTypeInfo +0xB50 |
| `Global/INGAMEB.BUN` | read, not used | chunk 0x00034492 holds the camera shake animations `ShakeA1` onwards, not the chase camera |

Both files were read, never written. The installation's manifest - path,
size and modification time of all 476 files - was identical before the
first read and after the last.

## What the game does with a controller

`speed.exe` names its input events in a table of `{id, name}` pairs at
0x006F14B8. The driving ones, by id: `JOY_EVENT_STEER` 0x87,
`STEER_ANALOG` 0x88, `THROTTLE` 0x89, `THROTTLE_ANALOG` 0x8A,
`THROTTLE_ANALOG_ALTERNATE` 0x8B, `BRAKE` 0x8C, `BRAKE_ANALOG` 0x8D,
`BRAKE_ANALOG_ALTERNATE` 0x8E, `REVERSE` 0x8F, `REVERSE_ANALOG` 0x90,
`EBRAKE` 0x91, `EBRAKE_ANALOG` 0x92, `SHIFTUP` 0x93, `SHIFTDOWN` 0x94,
`CAMERA_POV_CHANGE` 0x75, `CAMERA_LOOKBACK` 0x76 and `RESET_CAR` 0x99. Each
handler switches on those ids, so the identifiers below are traced, not
guessed from order. Every value arrives as a byte.

| Action | Handler | Rule | Label |
| --- | --- | --- | --- |
| steer | 0x00461840 | the digital byte (0x87) unless it is centred, 0x80, then the analog one (0x88); `(byte - 128) * -1/128`, with 0xFF read as exactly -1 so that both ends reach 1 (0x0046186E..0x004618A4) | RECOVERED |
| throttle | 0x004618B0 | the digital byte (0x89) when it is not 0, otherwise the larger of the analog one (0x8A) and the alternate analog one (0x8B), which is scaled by 1.07 and clamped to 255 unless a flag is 1; over 255 (0x00461951..0x00461988); then, when that flag (tested again at 0x00461994) is 1, the six-point curve at 0x00700374 (0x004619A3..0x004619B1) | RECOVERED; what sets the flag was not traced |
| brake | 0x00461DA0 | the digital byte (0x8C) when it is not 0, otherwise the larger of the two analog ones; over 255 | RECOVERED |
| reverse | 0x004619C0 | the digital byte (0x8F) when it is not 0, otherwise the analog one (0x90), cubed under the same flag | RECOVERED; the axis is registered (0x0046113A) and stored at car+0x2A4, but no driving-path consumer of it was found. The reverse the wheels feel is the gear-slot chain: the controls context's slot +0x1D4 holds -1 (automatic), 0 (reverse) or a manual gear, set by the mode setter 0x00461520, reset by 0x004615B0 (modes 1 and 3 to first, else -1), stepped by the shift handler 0x00461E70 with its table bounds, and zeroed by the processed-control machine at 0x00461C34; the physics reads it through vtable slot +0x24, whose body at 0x00424FF0 is `mov eax,[ecx+0x1D4]; ret`, into engine+0x134 at 0x00474671, and 0x004759A8 answers 0 by forcing gear slot 0 and the reverse ratio. The condition that drives the 0x00461C34 zeroing was not stack-verified; an emulator pass would settle it |
| handbrake | 0x00461E10 | the digital byte (0x91) when it is not 0, otherwise the analog one (0x92) doubled in eight bits (0x00461E26); the clamp after it compares an 8-bit value with 0xFF and never fires, so an analog byte of 128 or more wraps | RECOVERED |

Nothing in these handlers depends on speed, and nothing rate-limits the
steering: the byte goes straight to the car (+0x28C). Whatever makes the
game's steering speed-sensitive happens past the input layer, in physics
that was not traced (`TwoWheelAckermanSteering`, 0x006B9528, is where it
would start). The automatic gearbox is the vehicle model's recovered rule
(docs/VEHICLE.md).

### What the controls do, as rules

| Rule | Value | Label and reason |
| --- | --- | --- |
| a digital steer, throttle or brake decides unless it is centred; a centred one hands the action to the axis sources | a held throttle or brake button is a digital 1; steer buttons add, clamped to -1..1 | RECOVERED (the table above: 0x0046186E takes the steer byte unless it is 0x80, 0x00461957 and 0x00461DDE the throttle and brake bytes unless they are 0). Two opposite steer buttons add to a centred digital steer and hand the steer to the stick, as a centred byte does in speed.exe; what byte the game's own joystick layer makes of two opposite buttons was not traced, so that case is APPROXIMATION |
| a stick axis is read on its own, with its own dead zone | `axis <name> <inner> <outer>`, default 3000 and 30000, linear between, exact 0 at or inside inner | APPROXIMATION of the calibration: the game's joystick layer was not traced. Axial, not radial, so that pushing the steering stick up does not change the steering, and so that no square root is on the path |
| steer sign | the player's +1 is right; `VehicleInput::steer` is the negation, +1 left | the model's convention (docs/VEHICLE.md) |
| a digital throttle or brake | exactly 1 while held | the owner's ZR and ZL; docs/VEHICLE.md, "Throttle deadband": a digital throttle is unaffected by the model's engine-braking deadband |
| an analog throttle | `floor + (1 - floor) * value` above 0, `throttle-floor` 0.30, and 0 inside the dead zone | APPROXIMATION, from the same section of docs/VEHICLE.md: with the model's engine braking a throttle at or below about 0.30 is net engine braking, so a stick's lower third would slow the car it is asked to drive |
| the retail throttle curve | `throttle-curve retail` applies the curve at 0x00700374 before the floor, evaluated as 0x00454F30 does, `(1 - f) * y0 + f * y1` (0x00454FA9..0x00454FCD), in double where the game keeps x87 precision and stores a float | RECOVERED curve and form, not the default: the game applies it only under a flag whose writer was not traced, and at 0.2 it gives 0.152, which the model would turn into engine braking |
| a button steer's rate | away from centre at `5 / (1 + v / 20)` per second, v the speed: full input in 0.2 s at rest, 0.4 s at 20 m/s, 0.6 s at 40 m/s | APPROXIMATION: the speed-sensitive steering. The model already narrows its lock with speed and slews the road wheels at 3 rad/s, but at 30 m/s its full lock is reached in 25 ms, so a D-pad tap is a jerk. The rate gives a button a way to ask for part of the lock. It never scales the lock itself: full input always asks for the model's full lock |
| toward centre | 8 per second, from either source; a reversal returns to centre first | APPROXIMATION: a release settles in 0.125 s |
| a stick steer | passed through (`steer-analog-rate 0`) | the stick is its own filter; a rate would add lag |
| reverse assist, automatic gearbox only | the brake held alone below 1 m/s for 30 steps (0.25 s) asks for a down shift, which the model turns into reverse; in reverse throttle and brake swap; the throttle held alone below 1 m/s asks for first | APPROXIMATION. The model selects reverse on a down request in first below 1 m/s (docs/VEHICLE.md), and a pad with its shift buttons out of reach needs a way to ask. speed.exe has its own `REVERSE` action, whose physics was not traced |
| edges | shift up, shift down, camera and reset fire once, on the frame their button goes down, and a shift reaches the first vehicle step of that frame | as input.h's rules for presses |
| connect and disconnect | a button already down when its pad connects counts as up until seen up; a pad that disconnects contributes nothing from that frame | input.h's rules, repeated here because the driving controls read the frame themselves rather than through `InputMapper`, whose radial dead zone takes a square root |

### The default layout

The owner's Switch pad, named by position (docs/INPUT.md):

| Control | Nintendo label | Action |
| --- | --- | --- |
| `left_x` | left stick | steer |
| `dpad_left`, `dpad_right` | D-pad | steer, as buttons |
| `trigger_right` | ZR | throttle |
| `trigger_left` | ZL | brake; held at a stop, reverse |
| `face_south` | B | handbrake |
| `shoulder_right`, `shoulder_left` | R, L | shift up, down |
| `face_north` | X | the next chase view |
| `select` | - | reset the car (issue #60 takes it) |

`start`, +, is left to the application.

Switch drag also accepts right-stick up/down for one upshift/downshift per
deflection. Return the vertical axis through center before the next gesture.
Press/release hysteresis is defined by DragPadInput in switch/drag_input.h;
holding the stick or jitter near its activation threshold does not repeat.
This owner-requested binding shares the bounded HID/transition queue with
the alternate R/A buttons. R3 remains hold-to-look-back. A stick held outside
center on entry, controller change or resume must center before it can shift.

## The remapping file

ASCII, one directive per line; `format_drive_controls_config` writes the
canonical text and `parse_drive_controls_config` reads it:

```text
nfsu-controls 1
# the default layout
bind steer left_x
bind steer dpad_left invert
bind steer dpad_right
bind throttle trigger_right
bind brake trigger_left
bind handbrake face_south
bind shift-up shoulder_right
bind shift-down shoulder_left
bind camera face_north
bind reset select
axis left_x 3000 30000
axis left_y 3000 30000
axis right_x 3000 30000
axis right_y 3000 30000
throttle-floor 0.3
throttle-curve linear
steer-digital-rate 5 20
steer-centre-rate 8
steer-analog-rate 0
reverse-assist on 1 30
```

- `bind <action> <control> [invert]`: the actions are `steer`, `throttle`,
  `brake`, `handbrake`, `shift-up`, `shift-down`, `camera` and `reset`; a
  control is a button name, an axis name, or an axis name with `+` or `-`
  for one half of its travel, read as 0..1 (`bind throttle right_y+`,
  `bind brake right_y-`). `invert` makes a button count -1 or reads an axis
  the other way, and only steer takes it. The first `bind` replaces the whole
  default table, so a file that binds anything states its whole table.
- `axis <axis> <inner> <outer>`, `throttle-floor`, `throttle-curve`,
  `steer-digital-rate <rate> <speed>`, `steer-centre-rate`,
  `steer-analog-rate` and `reverse-assist <on|off> [<speed> <steps>]` each
  replace only their own default.
- Decimals are canonical fixed decimals of at most six places, read as
  integer / 10^k, as the vehicle script reads them, so every platform reads
  the same double: no sign, no leading zero, no trailing zero after the
  point.
- Refused, naming the line and the directive or token at fault: a header
  that is missing or other (on line 1) or after a comment or blank line (on
  its own line); a byte outside printable ASCII, a tab included; an unknown
  directive, action, control or keyword; a wrong number of fields; a number
  not canonical or out of its range - a floor above 0.9, a rate or the rate's
  speed of 0 or above 1000 (an analog rate may be 0), a reverse speed of 0 or
  above 5 m/s, more than 1200 hold steps, an axis edge above 32767 or an
  inner edge not below the outer (`line 2: steer-analog-rate: needs 0 or a
  rate above 0 and at most 1000`); a directive other than `bind` given twice;
  a stick axis on a digital action (`line 2: bind: right_x is a stick axis
  and cannot drive handbrake`); a throttle or brake on a whole signed axis
  or inverted; one control, or one half of an axis, bound twice; more than
  32 bindings (`line 34: bind: more than 32 bindings`) or 64 KiB. Nothing is
  changed unless the whole text is accepted.
- `format_drive_controls_config` writes every configuration the controls
  accept so that parsing the text gives it back, every double bit for bit:
  `validate_drive_controls_config` refuses, naming the directive, a floor,
  rate, speed or reverse speed that is not the double some fixed decimal of
  at most six places reads as - 1e-7 or 1/3 set in memory, say - since no
  file could state it.

`drive-replay --controls <FILE>` reads one. The console will read the same
text from the SD card; the application that does is the race application
of issue #159, and `nfsu_switch_drive` today runs the default layout.

## What the game does with its chase camera

**The camera object.** The camera-mover factory at 0x0042C6D0 switches on a
mover type (0x0042C7D7, jump table 0x0042D2F4 with index bytes 0x0042D330).
Type 1 constructs a 0x3A0-byte object with 0x00489360, vtable 0x006C7FC8,
whose update is slot 1, 0x00489690. The name `CubicCameraMover(DCC)` at
0x006C8160 fits it (drive cubic camera), but the name reaches only an
exception funclet, so the fit is an INFERENCE; the update's behaviour below
is what the rest rests on.

**Live race mover.** The event 19 PC 1.4 USA capture uses type 17, vtable
0x006C7E58, update 0x0048BCB0. It advances separate cubic channels for the
eye, look target and chase direction with 0x00593D40, then queues the next
requests through 0x0048B8C0. The chase frame originates at actor +0x50 and
the fixed view uses actor +0x60 and the full body basis. The authored
durations and acceleration limits are sampled from 0x00705C70 at the fixed
0x006B6E28 input. Four acceleration samples feed the arm and aim requests.
Changing between fixed and chase views snaps the affected channels
(0x0048B410). The ordinary camera selection is saved in Switch
presentation options. Legacy deterministic replay tooling retains the
type-1 camera so its historical frame comparisons remain useful.

The live chase uses the 0x004838E0 clearance sequence: clip the segment
from the centre of mass, then average up to three passes of polygon-normal
corrections. The track adapter uses the port's immutable collision index.
The eye also clears the actor's vertical bounds sphere from 0x00483B90.
The resulting view retains the actor's projected framing via 0x00483AD0.
The ordinary race noise table 0x006B6EB8 supplies zero gain to
0x00483640. The alternate table selected by 0x0078A344 or 0x007361A8 has
nonzero gain and belongs to a separate camera mode. Impact and countdown
effects have separate inputs and still apply.

The original x86 witnesses in tools/race_camera_native.py cover the
stationary far-to-near response, the world eye in a straight acceleration
step and wall clearance. nfsu_drive_tests and nfsu_race_session_tests
compare the corresponding results with the port.
**The anchors, per car.** The update looks up the anchor of its POV
(0x0047D680) in the car's CarTypeInfo record at +0xB50 (the record is car
+0xC, plus 0xB50 at 0x004897C2): two u32 counts, eight bytes that every
retail record holds as zero, a list at +0x10 and a list at +0x60, five
entries of 16 bytes each. 0x0047D680 reads the second list when the view
layout at 0x007364A0 is 3; 0x00408370 sets that layout from which of three
view lists hold a view (3 when the first and the second do), which reads as
two players sharing the screen, so a one-player race uses the first list.
An entry is `{s16 POV, u16 angle, f32 along, f32 height, f32 across}`; the
update reads them at 0x00489EB3..0x00489EFC. RECOVERED.

| POV | along | height | across | angle | single list | shared list |
| --- | ---: | ---: | ---: | --- | --- | --- |
| 0 | -5.8 | 1.7 | 0 | 0 | chase | along -8.2, height 1.85 |
| 1 | -4.7 | 1.7 | 0 | 0 | chase | along -6.8 |
| 2 | 0.716 | 0.79 | 0 | 0x01F4 | fixed to the car | height 0.64, angle 0xFAC2 |
| 3 | -0.195 | 0.805 | -0.44 | 0 | fixed to the car | -0.26, 0.772, -0.315 |
| 4 | -5.8 | 3.45 | 0 | 0x0BE0, 16.70 degrees | chase | along -8.2, angle 0x07EB |

These are the 240SX's. All 35 records have the same chase anchors but for
the height of POVs 0 and 1, which is 1.9 for the Golf, the Focus and the
Peugeot. POVs 0, 1 and 4 get the chase treatment below - the tests at
0x00489E5E..0x00489E6D, repeated at 0x00489EBA, 0x00489F04 and 0x0048A374 -
and 2 and 3 are fixed to the car and are not implemented here.

For a chase POV the distance behind is `-along`, the height `height`, plus
a pull-back that this layer does not reproduce (below). RECOVERED values;
the distance held fixed at them is APPROXIMATION. The word at +0x2 is
read into the eye's pitch: 0x0048AAF5..0x0048AB2D rotates the camera by its
negation. POV 4, the high one, has 16.70 degrees and the others 0, so it is
read as a look-down angle: INFERENCE, because the axis of 0x0040BA40 was not
traced. The chase camera therefore looks level from 1.7 m and down 16.7
degrees from 3.45 m.

The built-in `stock_240sx_chase_camera_table()` holds the 240SX's 176 bytes,
+0xB50..+0xBFF of record 18 (file bytes 1739648..1739823), as required by
PR #169 comment 3860. `nfsu_drive_tests` owns the encoded table hash and
checks it against a reconstructed record image. `drive-replay --root` compares the retail record with it:
`camera anchors: 240SX record +0xB50 equal to the built-in 240SX table`.

**The field of view.** 0x0048ACA4 writes a per-POV word from 0x006FBDF4
into the view at +0xC4: 13020 for POVs 0, 1 and 4 (71.52 degrees) and 16384
(90 degrees) for 2 and 3. RECOVERED. That +0xC4 is the horizontal field of
view is an INFERENCE: the projection that reads it was not traced; it is what
EA's renderers of the period hold, and 71.52 degrees across a 4:3 view is
56.75 degrees high.

**The heading it chases** (0x0048A0B6..0x0048A1C9). With F the car's
forward axis and V its velocity, both horizontal components:

1. if F . V < 0, the car is reversing, and V is reflected off the heading,
   V + (-2 (F . V)) F, so the camera does not swing round to face it;
2. s = |V| and t = the pair table at 0x00705B10 through its descriptor
   0x00705B20 and the evaluator 0x00583F20: 0 at or below 8 m/s, 1 at or
   above 13, linear between;
3. the target is the angle of (1 - t) F + t V / |V| (through speed / t and
   its reciprocal, as the code divides), in 16-bit units (bATan,
   0x00564DB0).

Below 8 m/s the camera follows the car's nose; above 13 m/s the direction it
is actually travelling, so in a slide it looks down the road, not along the
car. RECOVERED. At exactly zero speed the code divides 0 by 0; this layer
takes the heading there.

**The lag** (0x00489300, 0x0048A389..0x0048A502). The camera keeps a lag a,
the target less its own heading, in 16-bit units. Each frame a grows by the
target's move, then changes by

    trunc(-sign(a) * 1.9088 * atan16(0.000261 * |a|) * factor * dt)

atan16 being bATan(1, x), the arctangent in units, 1.9088 at 0x006B6C74,
0.000261 at 0x006B6C70, dt the frame time (0x0073457C) and trunc the
toward-zero conversion of _ftol2 (0x00674898); the camera's heading is the
target less a. For a small lag that is an exponential with a time constant
of 0.192 s; a large one closes at no more than 1.9088 quarter turns a
second, 171.8 degrees a second; and truncation leaves a dead band: at 60
frames a second a lag of 11 units, 0.06 degrees, never closes. RECOVERED.
The factor is view +0x68, which the constructor sets to 1 (0x0048947B) and
each frame moves by 0.24316 (0x006B6C78) toward 1 when car +0x3CC is above
2 and toward 0 otherwise. +0x3CC is the published wheel support count.
This ramp belongs to the type-1 mover. Compatibility runs use
`ChaseCameraApproximations::lag_factor`.

**The eye.** The anchor's offsets are turned by the lagged heading
(0x0048A7C4..0x0048A7EE) and added to car +0x60 (0x0048A826). The owner Stadium capture shows
actor +0x60 matches rigid body +0x20, the centre of mass; actor +0x50 is the
body's mesh origin. The live type-17 chase uses the mesh origin, and
its fixed bumper view uses the centre of mass.

**What was read and is not reproduced.**

- The pull-back. 0x00489DCD..0x00489E1C computes `p = max(0, 500 - 2.237 *
  [car +0x3D0]) / 500 * [view +0x384]` (500 at 0x006FBEF8, 2.237 at
  0x006CC9F0 - metres a second to miles an hour - and 0 when 500 - 2.237 v
  is negative, 0x00489DDF..0x00489DEE), or 0 when [view +0x32C] is set
  (0x00489DF4..0x00489E08). The distance behind is then `-along` plus p
  times a factor of the zoom-out blend below (0x00489ED2..0x00489EDE), and a
  p below 0 also adds 0.2 p to the height, lowering the eye
  (0x00489F15..0x00489F34). [view +0x384] is live state: the update eases it
  (0x00489A19..0x00489DC7) toward a target [view +0x388] that it builds
  (0x00489941..0x00489C3C) from a history of the last 20 ticks at view
  +0x334..+0x380, shifted once per 1/60 s tick (0x00489902..0x00489922),
  whose entries come from a vector of the car at +0x80..+0x88 turned into
  the car's frame (0x004897F1..0x00489850): 0.045 times its first component
  when that is below -0.3, 0.6 times it when that is above 0.7, 0 otherwise
  (0x00489855..0x004898C7), the target clamped to -1..6 m
  (0x00489B94..0x00489BD2). [view +0x32C] is the constructor's fourth
  argument (0x004893F1), which the factory takes from the player's byte
  +0x28 (0x0042C7FA); what that byte means and what the car's +0x80 vector
  is were not traced. So in a race where the byte is 0 the game's eye moves
  up to 6 m farther back, less at speed - up to 6 * (500 - mph) / 500 m -
  and up to 1 m nearer, as that history crosses its thresholds, where this
  layer holds the anchor's distance. APPROXIMATION of the distance, with the
  pull-back UNKNOWN in its input.

- A zoom-out: when a flag of the player's (+0x29 of the object at view
  +0x20) is clear, a blend at view +0x38 moves the distance toward 18 m, the
  height toward -3 and the pitch toward 0x1450 (0x0048A6CE..0x0048A784;
  16, -1 in the shared layout). Read as the camera before and after the race
  proper; not needed to drive.
- A tilt: 0x0048A505..0x0048A609 follows the car's pitch from the
  vertical component of its forward axis (0x00564CD0 of actor +0x98).
  Wheel support selects the angle correction; the angle difference is
  divided by eight toward zero outside the small-angle branch, then
  integrated with gain 25 and simulation dt. 0x0048A60B adds 250 times the
  positional noise from 0x00441D80 with material 8. The live view rotates
  its arm and aim with this result; its roll remains level. It holds the
  chase history while a roadside cinematic owns the view. Final terrain
  clearance and the precise landing return are still not a full match.
- An impact pulse: world 420FD2 passes the completed actor acceleration to
  575200, which starts an envelope when the weighted vector passes its
  threshold. 48ACE5 applies 575040 to the finished camera transform.
  574EB0 advances the envelope in scaled world time; the projection
  skips slow motion and alternates visible and silent half-waves.
  The live ordinary race camera uses these rules. This pulse does not
  change vehicle physics, impact admission, or the camera eye position.
- A sphere around the car (0x0048A96F..0x0048AA60): an eye closer to car
  +0x60 than the larger of two of the car's sizes plus 0.7 is lifted onto
  the sphere, and the pitch turned toward 0x2710 in proportion. A chase
  anchor 4.7 m or more behind does not reach it.
- The cubic mover at view +0x6C (0x004407B0, 0x00440A40), handed a point
  halfway to the eye and a duration of half the distance plus 1: whatever it
  smooths, the rules above already place the eye and its heading.
- The POV cycle: 0x0042E020 steps through four POV bytes of the player at
  +0x2C..+0x2F, reading POV 3 as 0 and skipping the fourth slot unless one
  of two flags (0x0078A345, 0x007361A4) is set; their initial values were
  not traced.

No query of the world's collision was found on the traced path of the
update: whatever keeps the game's camera out of a wall is elsewhere, or is
not there.

### What the camera does, as rules

| Rule | Value | Label |
| --- | --- | --- |
| anchors | the single-player list of CarTypeInfo +0xB50, POVs 0, 1 and 4 | RECOVERED |
| distance, height | `-along`, `height`, held there | RECOVERED values; APPROXIMATION to hold them: the game's pull-back (above) is not reproduced, its input not traced |
| pitch | anchor plus the live body's filtered elevation and positional noise | RECOVERED inputs and filter; final retail view composition is incomplete |
| target heading | the blend of heading and velocity by t(speed), 8 to 13 m/s, the reversing reflection | RECOVERED |
| lag | the spring above, integer, per camera update | RECOVERED for type 1 |
| update rate | once per 1/60 s tick, dt 1/60 | the game updates once per rendered frame; its world ticks at 60 Hz (docs/VEHICLE.md) |
| anchor point | type 1 uses the centre of mass in 0x0048A826 | RECOVERED; the live type-17 origins are described above |
| roll | none: the camera's up is the world's, tilted only by the pitch | Independent roll is not reproduced |
| field of view | the POV's word as the horizontal field of view of a 4:3 view, its vertical angle, 56.75 degrees, kept on any frame (`vertical_4_3`); `horizontal` keeps the width instead, 44.11 degrees high on 16:9 | INFERENCE of the axis; APPROXIMATION of the widening: the Switch is 16:9 and this is the picture the game drew on its own 4:3 screen, widened, where `horizontal` would crop the top and bottom, 44.11 degrees against 56.75. Even with the 4:3 height, POV 1 puts the rear plate on the frame's bottom edge (frame 840 below) |
| clip planes | near 0.5 m, far 2000 m | 0.5 is what 0x0048ACD0 writes into view +0xBC for every chase POV (0.4 for POV 3, 0x006FBF1C); that +0xBC is the near plane was not traced. Far is invented |
| first POV, cycle | POV 0 first, then 1, then 4 (`next_pov`); a change keeps the lag and the heading it chases, so the new view starts where the old one looked | APPROXIMATION: the game's initial POV bytes were not traced; POV 0 is the one that shows the whole car on a 4:3 view. speed.exe builds its mover with the lag zeroed (0x00489398) and the previous target seeded from car +0x3B4 (0x0048938E); whether a POV change rebuilds it or keeps it was not traced |
| collision | the segment from the pivot - the anchor point raised to the eye's height - to the eye is asked of a `ChaseCameraWorld`; a hit pulls the eye in along it to 0.2 m short, at once; once clear it moves back out at 6 m/s | APPROXIMATION: no recovered rule to follow. `ChaseCameraOpenWorld`, nothing to hit, is the default |

**Camera collision is the caller's.** `ChaseCameraWorld::clear_fraction(from,
to)` returns the free fraction of the segment, 1 when nothing is hit, as
`VehicleGround` does for the wheels. Issue #60's track contact layer can
answer it with its first-contact segment query; a value that is not finite
reads as clear and one outside 0..1 is clamped.

## The loop, the demonstration and the pin

**One tick per frame.** A controller frame is one 1/60 s tick: the
controls read it, the vehicle takes two of its 1/120 s steps, each with its
own `DriveControls::step` (so the steering filter and the reverse assist
run at the vehicle's rate), then the camera takes the frame's camera edge
and one update of 1/60 s. A frame's time field is not read: a replay is one
tick per recorded frame, and so is the live loop that recorded it, whatever
the clock said. That is the rule the console will follow: sample the pad
once per displayed frame, advance one tick.

**The record.** Each tick is hashed as 1224 bytes, little-endian, every
double as its IEEE-754 bits: the frame number (u64); per vehicle step the
input as applied (throttle, brake, steer as doubles, handbrake and shift as
u32) and the vehicle's 520-byte telemetry record; then the camera (POV,
heading, target, lag, pitch and field of view as u32; eye, forward and up;
the reach) and the frame's camera and reset edges. `DriveSession` keeps the
SHA-256 of the records in order.

**The demonstration**, `drive_demo_recording()`: 1380 frames, 23 s, of a
pad in slot 0 stamped `frame * 1000000 / 60` microseconds. The ranges below
are the recording's frames, from 0; the events are the probe's frame
numbers, the count of ticks done, from 1, as `drive-replay --every 1` prints
them.

| Frames | Held | What happens |
| --- | --- | --- |
| 0-29 | nothing | at rest |
| 30-389 | ZR | full throttle, first to second gear, 99 km/h |
| 390-569 | ZR, left stick at -16384 | a stick-steered bend to the left, up to 125 km/h |
| 570-689 | ZR, D-pad right | a button-steered bend to the right, 137 km/h, the fastest |
| 690-809 | ZL, left stick full right | braking into a right turn, down to 49 km/h |
| 810 | B, X, stick full left | the handbrake, and the camera from POV 0 to POV 1 at frame 811 |
| 811-869 | B, stick full left | the handbrake turn; the lag grows to 2769 units, 15.2 degrees, at frame 870 |
| 870-929 | nothing | coasting; the lag peaks at 2853 units, 15.7 degrees, at frames 873 and 874, then closes |
| 930-1169 | ZL | the car stops at frame 954; after 0.25 s below 1 m/s the assist selects reverse at frame 962, and the brake drives it backwards to -63 km/h |
| 1170-1379 | ZR | in reverse the throttle brakes; stopped, the assist selects first at frame 1268, and the car drives forward again, 35 km/h at the end |

**The pin.** `kDrivePinnedDemoSha256` owns the current demonstration
stream hash. `nfsu_drive_tests` checks it and
`nfsu_probe_drive_replay_guards` checks the built-in, recorded and explicitly
remapped inputs. Reproduce it with `nfsu_probe drive-replay --demo --every 0`.
The corrected arctangent changes turning states; the handbrake follows
max(service, half handbrake), rather than adding a rear lock torque.
Default/Release and console agreement must be verified for the candidate.

## The probe and the frames

```text
nfsu_probe drive-replay --demo | --input <FILE>
      [--controls <FILE>] [--pov <0|1|4>] [--frames N] [--every N]
      [--root <DIR> [--render-every N] [--out <PREFIX>] [--width N]
       [--height N]]
```

It replays the stream and prints a line every `--every` frames (60 by
default, 0 for none), the last frame, and the stream's hash. With `--root`,
the game's folder, it reads the six files of the stock frame
([`SWITCH_BUILD.md`](SWITCH_BUILD.md)), prepares the painted stock 240SX
(`stock_240sx_skin()`, whose base paint is a choice, not the game's), says
whether the record's handling and camera anchors equal the built-in tables,
and every `--render-every` frames (120 by default) renders the car from the
chase camera with the CPU reference renderer over a flat checkered ground,
640x360 unless `--width` and `--height` say otherwise, and prints the
frame's SHA-256; `--out` writes `PREFIX-NNNNNN.tga` and never replaces a
file. The car is placed by composing each stock instance's transform with
the car's body-to-world transform (`drive_place_instances`); the ground is
16 m tiles out to 160 m, checkered every 4 m by the renderer's own fallback
pattern. It exits 1 on an argument it refuses, 2 on a recording or controls
file it cannot read or parse or a frame it cannot write, 3 on a root that
cannot give the stock frame and 4 when a step is refused. The path arguments follow
[`PATH_ARGUMENTS.md`](PATH_ARGUMENTS.md); all four are converted as the
arguments are parsed.

**The frames.** `drive-replay --demo --every 0 --root <game> --render-every
120`, 640x360. These document-only retail pins were refreshed from a
successful default-build run after restoring the shared native477775 base
grip,479640/46E020 traction policy and the complete477300 rest/bump refresh
with588AB0 landing impulses. The stream hash remains test-owned
in `nfsu_drive_tests`; written frames were checked against the printed
hashes. Default and Release streams and all11 written frames match:

| Frame | POV | SHA-256 |
| ---: | ---: | --- |
| 120 | 0 | `6999531685863C172B2519598D6AE04B9150B76670616649CA1C69CDAC198911` |
| 240 | 0 | `B74055E7AA1FD294D036C275445DB8DF3BB4DBE28507C235DAB1F0FAD8C54C81` |
| 360 | 0 | `0493D75E9B113764661B1C4A2996AAD532215721080A054E67D8B075A44B564E` |
| 480 | 0 | `5859972853989A7F38120930BE1FD3D1D6EFA3F82C96AA5797D2AFD4AB1D9B88` |
| 600 | 0 | `3DBD68EDCCEA387404ED1A13DEE9663CFFEF13AD68CC72CD8B220A6171E7BF24` |
| 720 | 0 | `9EDA6021CBEB26A74CBB5FDF130CCAF3149E14AA508D38F5453E80BA0D9D9391` |
| 840 | 1 | `7577F178946C95AAADAEF787E0A6B1E8AB3A537CDACE91C41A4F41C7BD7AF867` |
| 960 | 1 | `F7D0D48B240E477B0965F96E4E5D8A94FB601F82EB3B90F9D73BDDC478508166` |
| 1080 | 1 | `CE549706D880497BE880FFD190C304451C61531132D2C6A4CBBC9BE75AB24ED2` |
| 1200 | 1 | `D06F899F0B1635C2CA54591F45547A69463A88D8CFD0E18D7F721630383C0CFE` |
| 1320 | 1 | `B7F71A9E74A86C17386B9FDE082204C93621876198EBBC7AEE2663211D974BCD` |

The stock lateral placement correction is retained. `drive-replay` does not
pass a car animation state to `drive_place_instances`, so these frames do
not qualify wheel spin, steering, suspension travel or lamp brightening.

Frames 720 and 840 needed PR #162 (issue #125). Before it, the default and
Debug builds gave `1D03ECAE...04F5` and `634293EC...39EF` for them, 12 and 51
pixels off the Release frames: `normalize` in `src/render.cpp` took
`std::sqrt`, which MinGW's libm computes in x87 extended precision at `-O0`
and rounds twice. The stream those builds drove was already equal to the bit,
so the difference was the renderer's alone; `det_sqrt` gives the one-rounding
root in every build. That rounding fix remains part of the current pins.

The stream through this run matches the test-owned demonstration pin.
`nfsu_probe_drive_replay_guards` separately pins synthetic frames at 690 and
1380, checking the printed hashes against freshly written TGAs. Both
synthetic poses changed with the corrected arctangent.

## Determinism

The controls, the camera and the session use only +, -, *, /, comparisons
and integer arithmetic, plus the vehicle library's `vehicle_sqrt`,
`vehicle_sin` and `vehicle_cos` and this layer's `chase_atan` - fdlibm's
arctangent reduction and polynomial - which are built from the same
operations. No library mathematics function is called on the per-step path,
no clock is read, and angles are 16-bit integers as the game keeps them. The
controls read each stick axis alone, with one integer subtraction and one
division, so there is no square root in them at all; `nfsu_input`'s radial
dead zone, whose square root issue #165 is routing through a correctly
rounded one, is not on this path. The three libraries inherit the project-wide
`-fno-fast-math -ffp-contract=off` rule (`/fp:precise` under MSVC), and each
source refuses to compile unless `FLT_EVAL_METHOD` is 0.

On the console, recompiling each source with the exact `build-switch`
command (devkitA64 GCC 16.1.0, `-O2`), `-S` in place of `-c`:

| Source | Options | fmadd | fmsub | fnmadd | fnmsub | fmla | fmls | total fused | fmul |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `src/drive_controls.cpp` | as configured | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 13 |
| | without the two options | 3 | 0 | 0 | 0 | 0 | 0 | 3 | 10 |
| `src/chase_camera.cpp` | as configured | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 66 |
| | without the two options | 33 | 1 | 0 | 6 | 0 | 0 | 40 | 31 |
| `src/drive_session.cpp` | as configured | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 40 |
| | without the two options | 12 | 2 | 1 | 6 | 0 | 0 | 21 | 19 |

`nfsu_switch_fma_census` disassembles every project object of the Switch
build, including these three, and fails on any fused instruction. See
[FLOATING_POINT.md](FLOATING_POINT.md) for the current whole-tree counts.

## Tests

`nfsu_drive_tests`, in every desktop build:

| What | Pinned by |
| --- | --- |
| the file | the default text and its round trip bit for bit; every value from 0.000001 to 1000 the parser can produce round-trips bit for bit as a floor, a rate and a reverse speed; a partial file keeps the other defaults; 40 refusals, each naming its line and token, each leaving the configuration unchanged; the in-memory table's own refusals, six unstatable values among them |
| calibration | the inner edge reads exactly 0, one unit past it 1/27000, the outer edge full, -32768 and out-of-range values the clamped extreme, symmetry at every seventh value, a 0..32767 zone; the steering axis read alone when the stick is also pushed up; through update(), each of the four axes read with its own zone where the zones disagree, and a file's `axis right_y 1000 31000` reaching step() as a throttle of exactly 0.5 at 16000 and 1/30 at 2000 |
| sources and remapping | ZR exactly 1 and ZL exactly 1; half of an axis half is floor + (1 - floor) / 2 bit for bit; the dead zone gives 0, not the floor; the retail curve's segment, points and ends; a remapped throttle on A with ZR driving nothing; in step(), with both sources held, D-pad left beating the stick full right, ZR beating a half-up stick and ZL a half-down one; two opposite buttons handing the steer to the stick; two pads adding and cancelling |
| the steering filter | the first step at rate(v) / 120 at 0, 20 and 40 m/s, full lock in 24, 48 and 72 steps; release in 15; a reversal through centre; a stick at once, its release at the centre rate; `steer-analog-rate` |
| edges | a shift edge on the first step only, no repeat when held; camera and reset edges; buttons held at connect latched; a disconnect ending everything; a reconnect latching again; stray bits ignored |
| the reverse assist | the down request on the 30th step of the brake below 1 m/s, the swap in reverse, the up request on the 30th step of the throttle; none while moving, with both pedals, with the manual gearbox, or switched off |
| the recovered tables | the built-in anchors rebuild the retail 176 bytes' hash; the rule constants rebuild speed.exe's 34 bytes' hash (the five FOV words at 0x006FBDF4, the blend pairs at 0x00705B10, the lag floats at 0x006B6C70: `BF6027AE2C9B62D8E3FFF05782302CAF98D178BD3AAF1BD1D52D90F800874B52`); the throttle curve its 48 bytes' (`33B5F909CC5C21AB7A2B6D13DE3DD6CB330681B17038E6B47D7DD6B874455B56`); the encoder writes 168 bytes; decode refusals by offset |
| angles | `chase_atan` within 2 ulp of the library's over 8001 points; the eight compass angles, the zero and non-finite vectors; units to radians to units exact at every thirteenth unit |
| the target heading | the heading at rest and along the velocity; no velocity weight below 8 m/s; the velocity alone above 13; 22.5 degrees at 10.5 m/s with a 45-degree slip; the reversing reflection; the blend at 71 speeds |
| the lag | a 45-degree jump decays by 376 units in the first frame, each frame the recovered expression recomputed, monotone, into the dead band; odd at 211 points; saturated below 1.9088 quarter turns a second; the 16-bit wrap; a factor of 0 |
| the camera | POVs 0, 1 and 4 where the table puts them, POV 4 16.7 degrees down; 2 and 3 refused; the cycle; the eye under a turned subject; a constant 30 degrees a second turn at 20 m/s settling to a constant lag of about 5.9 degrees and turning with the car, the eye on its 5.8 m circle; refusals |
| collision | a wall 3 m behind pulls the eye to 2.8 m; clear, it returns at 6 m/s, all the way in 19 frames; a nearer wall pulls it in at once; a wall past the eye, a NaN and a negative fraction |
| the renderer's side | the renderer's eye is the view's to 1e-12 and it looks along the view's forward vector; 56.75 and 44.11 degrees; the car placed at a pose; the ground drawn in two shades under a level camera; refusals |
| the session | the demonstration's pin, twice the same; its phases - past 120 km/h, reverse and back, POV 0 to 1 on frame 811; the recorded form replaying to the same stream; a remapped throttle and another POV changing the stream; refusals |

Eight deliberate breaks were each built and run, then the source restored
byte for byte and touched; each was killed with `test failure:` lines, and
the restored tree passes:

| Break | Killed by |
| --- | --- |
| the button no longer overrides the stick in the steering filter | the steering filter, 10 lines |
| the button rate no longer falls with speed | the steering filter at 20 and 40 m/s, 5 lines |
| the throttle floor left out | the analog throttle, 1 line |
| the lag spring's sign flipped | the lag and the camera, 278 lines |
| the reversing reflection left out | the target heading, 3 lines |
| the collision margin left out | collision, 5 lines |
| the camera updated with half the frame time | the pin, 1 line |
| throttle and brake not swapped in reverse | the reverse assist and the session, 33 lines |

## What is not known

- Where the game's steering becomes speed-sensitive, and whether a digital
  steer is filtered anywhere: not in the input handlers.
- What the flag at 0x00461994 is (the throttle curve and the cubed reverse);
  how the physics reads the `REVERSE` action.
- The default POV and the four-POV cycle's initial bytes; whether +0xC4 is
  the horizontal field of view; the axis of the pitch rotation; where car
  +0x60 is; car +0x3CC; the cubic mover's role; any camera collision with the
  world.
- The drag and look-back cameras (`DragCameras.bin`, `CAMERA_LOOKBACK`), the
  car-fixed POVs 2 and 3, and the camera shake of `INGAMEB.BUN`.

## How the Switch application reads the gamepad through it

1. Configure once: `padConfigureInput(1, HidNpadStyleSet_NpadStandard)` and
   `padInitializeDefault`, which reads the handheld controls and player 1 as
   one pad (docs/INPUT.md).
2. Each displayed frame, `padUpdate`, then build one `GamepadFrame`: slot 0
   connected when `padIsConnected`, buttons from `padGetButtons` - the held
   state, never `padGetButtonsDown`, or the connect latch is bypassed -
   through `gamepad_buttons_from_npad`, and the four axes from
   `padGetStickPos(&pad, 0)` and `(&pad, 1)`, which libnx reports in
   -32767..32767 already. `nfsu_switch_drive`'s `sample()` is that code.
3. Hand the frame to `DriveSession::advance` - one tick - and draw the car
   at `state()` from `view()`. Nothing else decides anything: the same
   frames replay to the same stream on the desktop, and
   `format_input_recording` writes a live session down so that it can be.
4. The remapping file, when the race application reads one, comes from the
   SD card as text through the platform file service and goes through
   `parse_drive_controls_config`; a refusal names its line, and the
   application keeps the default layout.

`nfsu_switch_drive` runs the demonstration against the pin, then drives the
session live from the pad with the speed, gear, inputs and camera printed
on the text console; there is nothing to see but numbers until the deko3d
renderer draws the car. It has not run on a console.

## How issues #60 and #62 plug in

- **#60** implements `VehicleGround` for the wheels and `ChaseCameraWorld`
  for the camera over the track contact layer, and takes the controls'
  `reset` edge (`DriveFrameState::reset`, the - button) to put the car back
  on the road. `DriveSession` runs on flat ground today; the track replaces
  `VehicleFlatGround` and `ChaseCameraOpenWorld` there.
- **#62** times the lap against the platform clock; the session's ticks are
  the simulation's time, one per sampled frame.
