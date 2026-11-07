# Vehicle dynamics

Issue #58: the minimum fixed-step car the first lap of issue #159 needs -
one car, four wheels, a rigid body on four struts, tyres, an engine and a
gearbox - with every constant labelled as recovered or approximated, per-step
telemetry, and the same bits on the desktop and the console.

The code is `include/nfsu/vehicle.h`, `src/vehicle.cpp` and
`src/vehicle_script.cpp` (the library `nfsu_vehicle`, declared for both
configures in `cmake/vehicle_libraries.cmake`), the probe command
`vehicle-sim`, the tests `nfsu_vehicle_tests` and
`nfsu_probe_vehicle_sim_guards`, and on the console `nfsu_switch_vehicle`.

Two labels are used throughout:

- **RECOVERED**: read from the retail files or `speed.exe`, with the source.
  A value is recovered when its bytes are read from a named offset; a rule
  is recovered when the instructions that apply it were read. Where only the
  value is read and its meaning rests on something weaker than a traced
  reader, the text says what it rests on.
- **APPROXIMATION**: invented by this project, with the reason. Every
  approximation is a field of `VehicleApproximations` or a named rule in
  `src/vehicle.cpp`, so it can be replaced by a recovered one without
  touching the rest.

## Evidence base

| File | SHA-256 | What it gives |
| --- | --- | --- |
| `speed.exe` | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` | the rules; addresses are virtual, file offset = VA - 0x400000 for all three sections (checked against the PE section table) |
| `Global/GLOBALB.BUN` | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` | the CarTypeInfo records, chunk 0x00034600 |

`Global/GLOBALB.BUN` is the uncompressed original. The installation's
`GlobalB.lzc` and `GLOBALA.BUN` are 2025 modifications and nothing here
rests on them. Both files were read, never written; the installation's file
manifest (path, size, modification time of all 476 files) was identical
before the first read and after the last.

## Where the handling lives

NFSU keeps each car's handling in its CarTypeInfo record: 35 records of
0xC90 bytes in chunk 0x00034600 of `GLOBALB.BUN`
([`PART_PLACEMENT.md`](PART_PLACEMENT.md) decodes the table's layout, names
and wheel records). The 0x2B0-byte setup sub-object at record +0x110, which
0x00575960 copies into each car instance, holds the wheels, tyres, body,
struts, gearbox and engine; the physics code reaches every field below
through a pointer to that sub-object (engine object +0x18, set at
0x00475174), so a code address quoted with "sub +X" means record +0x110+X.
The record from +0x3C0 to +0x9BB is the per-level upgrade data, which
[`VEHICLE_TUNING.md`](VEHICLE_TUNING.md) decodes (issue #57). This model does
not read it, and a car with nothing installed reads all of it only through two
rules of 0x00575960 that run whatever the levels are: the inertia rebuild
below, and the addition of the second torque curve at +0x334 into +0x310. Both
are no-ops on the stock 240SX, whose +0x334 is zero and whose stored inertia
already is the rebuild's answer, which is why this model's pins hold without
them; the Eclipse, the RX-7 and the Skyline have a non-zero +0x334 in stock.

The 240SX is record 18, at absolute offset 1736752.
`nfsu_probe vehicle-sim --handling <GLOBALB.BUN>` decodes the fields below
out of the retail record and compares them bit for bit with the built-in
table `stock_240sx_handling()`; on the retail file it prints
`handling: car type 18 240SX record @1736752 equal to the built-in 240SX
table`.

The built-in table is a small numeric table from a retail file, kept in the
source under the owner's rule recorded on PR #169 (comment 3860): it states
its source, `Global/GLOBALB.BUN` (SHA-256 39DF38FA...8DB9), chunk
0x00034600, record 18 at file offset 1736752, and the SHA-256 of the bytes
it reproduces, and a test rebuilds those bytes from it. It does not
reproduce the whole record. It reproduces 316 of its 3216 bytes: the 79
fields in "The fields the model reads" below, which are the bytes
`encode_vehicle_handling` writes, in 23 runs:

    +0x110..+0x11B  +0x120..+0x12B  +0x130..+0x133  +0x150..+0x15B
    +0x160..+0x163  +0x180..+0x18B  +0x190..+0x193  +0x1B0..+0x1BB
    +0x1C0..+0x1C3  +0x1E0..+0x1F3  +0x200..+0x213  +0x220..+0x233
    +0x244..+0x247  +0x258..+0x25B  +0x280..+0x29B  +0x2A0..+0x2BB
    +0x2C0..+0x2C3  +0x2C8..+0x2D3  +0x2D8..+0x2DB  +0x2E0..+0x30B
    +0x310..+0x333  +0x358..+0x363  +0x370..+0x373

Those 316 bytes, concatenated in offset order, hash to

    54394827CC13AEE7DE095524810B790A238FFC24763E7C2A2BA236A1A2430DBD

both as the retail record holds them and as the built-in table encodes
them (every one of the 316 is equal). `kVehicle240sxRecordBytesSha256`
holds the hash beside the table, and `nfsu_vehicle_tests`
(`test_record_source`) encodes the table into an all-0x00 and an all-0xFF
record image, takes the bytes that are equal in both - the ones the
encoder writes - and requires 316 of them in those 23 runs, hashing to
that value. It needs no retail file. The other 2900 bytes of the record,
which the table does not hold, are not covered; for identification only,
the whole 3216-byte record hashes to
212F69AD8E739250F95E04C54FB6613A39A7C76014CB402DF966F64B5D9FFF9B.

### The fields the model reads

| Record offset | Field | 240SX | Label and evidence |
| --- | --- | --- | --- |
| +0x110, +0x114, +0x118 | centre of mass (longitudinal, lateral, vertical), body frame | (0.25, 0, 0.28) | RECOVERED: 0x004745B8..0x004745CF copies the three into the rigid body (+0x1D0), 0x00588541..0x00588558 subtracts them from a wheel's position, and the strut preload of 0x00475460 levers the weight about the first (below) |
| +0x120 + 0x30 k | wheel k position (longitudinal, lateral, vertical) | (1.39, +-0.73, 0.08) front, (-1.04878, -+0.73, 0.08) rear | RECOVERED, [`PART_PLACEMENT.md`](PART_PLACEMENT.md) |
| +0x130 + 0x30 k | wheel k rolling radius | 0.288 front, 0.3135 rear | RECOVERED; 0x00475A82 adds it to the strut's signed distance to the ground |
| +0x1E0 / +0x200 | tyre block, front / rear (0x20 each) | below | RECOVERED: the tyre constructor 0x005868E0 keeps a pointer to it (0x005868FC), passed from 0x0047438C as sub +0xD0 + axle * 0x20 |
| tyre +0x0C | peak (static) friction coefficient | 1.8 / 1.7 | RECOVERED: 0x005878A2..0x005878CF multiplies it by the surface's static factor and the wheel load into the force limit |
| tyre +0x10 | sliding friction coefficient | 1.5 / 1.47 | RECOVERED: 0x00587546..0x0058756A, the same product with the surface's sliding factor |
| tyre +0x00, +0x04, +0x08 | slip scale, load scale, lateral scale | 12, 1, 1 | RECOVERED record values; the lateral curve family at 0x00587740 divides angle and load by the first two and scales force by the third. The port now evaluates this family with a range-reduced deterministic slip angle. Full wheel/contact coupling and original live handling acceptance remain separate requirements. |
| +0x220 | mass, tonnes | 1.22 | RECOVERED value; the weight rule 0x00463C51..0x00463C58 multiplies it by g = 9.8 beside the averaged friction coefficients, and the drag rule 0x0045689B divides by it. Tonnes is an inference, below |
| +0x224, +0x228, +0x22C | length, width, height, m | 4.52, 1.69, 1.29 | RECOVERED: they equal the real car's dimensions, and 0x005762FC reads all three into the inertia diagonal above. The model does not use them, because it reads that diagonal |
| +0x230, +0x244, +0x258 | inertia about the body axes, tonnes m^2: the diagonal of a 4x4 matrix at +0x230 | 0.4595537, 2.2462742, 2.367461 | RECOVERED: the rigid body keeps a pointer to +0x220 (0x0047415D into 0x00584800, stored at 0x00584806), 0x00584B21 takes the mass from it and 0x00584B3D inverts the matrix at pointer +0x10 into the body's inverse inertia. In all 35 records the twelve other entries are zero and each diagonal entry is the box inertia of the record's own mass, length, width and height. 0x005762FC is why: it rebuilds the diagonal from +0x220..+0x22C on every car instance, with the float 1/12 at 0x006CCA6C, and all 105 entries are then bit for bit what that rule gives ([`VEHICLE_TUNING.md`](VEHICLE_TUNING.md), "The inertia rebuild"). |
| +0x280 / +0x2A0 | strut block, front / rear (0x20 each) | below | RECOVERED: 0x00475A94 reaches it as sub +0x170 + (wheel / 2) * 0x20 |
| strut +0x00 | progressive term, 1/m | 5.4 / 5.1 | RECOVERED, the force rule below |
| strut +0x04 | spring rate, kN/m | 35 / 30 | RECOVERED; with the preload below the springs work about the rest position, so at rest x = 0 and the rate is 35 kN/m |
| strut +0x08 | damping while compressing, kN s/m | 5.4 / 5.0 | RECOVERED: selected when the rate is above 0 (0x00475B56..0x00475B77) |
| strut +0x0C | damping otherwise, kN s/m | 5.8 / 5.65 | RECOVERED, same selection |
| strut +0x10 | anti-roll rate on the difference within the axle, kN/m | 37 / 30 | RECOVERED: 0x00475B90..0x00475B98, partner from the table at 0x006FFF14 (1 0 3 2) |
| strut +0x14 | compression limit, m | 0.125 | RECOVERED: 0x00475AB9..0x00475AD4 clamps to it and keeps the excess |
| strut +0x18 | extension limit, m | -0.13 | RECOVERED: 0x00475A9B..0x00475AB7; below it the wheel is off the ground |
| +0x2C0 | shift fraction | 0.8 | RECOVERED: 0x0046D1FC puts the neutral slot's threshold 0.8 of the way from idle to redline, and 0x004751C7 then scales that slot by a drive-type factor from 0x006F101C (1.0, 1.2, 1.1 for front, four and rear drive). Where the neutral slot is read was not traced; the model keeps the unscaled value and uses it nowhere |
| +0x2C8 | final drive for wheels 2 and 3 | 4.083 | RECOVERED: 0x0047563D (sub +0x1B8) |
| +0x2CC | final drive for wheels 0 and 1 | 4.083 | RECOVERED: 0x0047563D (sub +0x1BC); see "the engine speed" for the pairing |
| +0x2D0 | rear drive fraction | 1.0 | RECOVERED: 0x00475550..0x00475572 gives the rear wheels this fraction and the front ones the rest; 0x0047517E classifies < 0.2, 0.2..0.8, > 0.8 |
| +0x2D8 | forward gear count (u32) | 5 | RECOVERED: 0x00475915 bounds the upshift loop with it |
| +0x2E0 | gear ratios by slot: reverse, neutral, 1st .. 6th | -3.657, 0, 3.321, 1.902, 1.308, 1.0, 0.9, 0 | RECOVERED: 0x00475644, 0x00475943, 0x00475999 index them by the gear slot at engine +0x124, which 0x004751EE starts at 2 |
| +0x300 | idle rpm | 800 | RECOVERED: the engine speed's lower clamp (0x004759BA) and both curves' x0 (0x0047509D) |
| +0x304 | redline rpm | 7500 | RECOVERED: the shift search's start and limit (0x0046D233, 0x0046D2EF); the top gear's threshold (0x0046D36D) |
| +0x308 | maximum rpm | 8000 | RECOVERED: the upper clamp (0x004759D8) and the curves' x1 (0x00475097) |
| +0x310 | torque curve, 9 points, kN m | 0.14 0.15 0.16 0.18 0.21 0.22 0.19 0.165 0.145 | RECOVERED: 0x004750A9..0x004750EA builds a 9-point uniform curve over [idle, max] on it |
| +0x358 | engine braking curve, 3 points, kN m | 0.12 0.18 0.13 | RECOVERED: 0x004750F2..0x00475128, a 3-point curve over the same range |
| +0x370 | drag, kN per (m/s)^2 | 0.0004 | RECOVERED: 0x0045687A..0x0045689B computes drag * v^2 / mass as a deceleration |

The record's layout is not the 240SX's alone. Over the 20 racers of the
same file the same offsets give, with no exception, what the real cars
have: the rear drive fraction +0x2D0 is 0 for every front-wheel-drive car
(Golf, Focus, Neon, Civic, Peugeot, Celica, RSX, Lancer, Integra, Tiburon,
Sentra), 1 for every rear-wheel-drive one (Miata, RX7, Supra, S2000, 350Z,
240SX) and 0.5 for the three four-wheel-drive ones (Eclipse, Impreza,
Skyline); the gear count +0x2D8 is 6 for the Celica, Supra, S2000, RSX,
Tiburon, 350Z, Sentra and Skyline and 5 for the rest, as those cars'
manual gearboxes are in their best-known versions; the S2000's
redline +0x304 is 9000; the masses run from 1.04 (Miata) to 1.55 (Supra)
and the bus's is 6.5 with a length of 9.4. That is what the field names
above rest on where no reader is quoted.

### Units

The record does not state units. Three facts fix them: the weight rule at
0x00463C51 multiplies +0x220 by 9.8 (0x007000AC), so +0x220 is a mass and
the product a force; the rigid body's gravity at 0x00585AE7 is the same
product with the mass it took from +0x220; and the masses are 1.04 to 1.55
for the racers and 6.5 for the bus. Mass is therefore in tonnes and force in kN. The strut rates
(35 kN/m) and dampers (5.4 kN s/m) are the right size in those units for a
1.2 t car, and the 240SX's torque peak, 0.22 kN m, is the real KA24DE's
217 N m. The model runs in SI and multiplies every record force, torque,
rate and mass by `unit_scale` = 1000 (APPROXIMATION: an inference about
units, not a traced value). Not every car's torque matches its real engine
(the 350Z's peak is 0.304 against 371 N m), which says the values are the
game's tuning; it does not change the unit.

## What was recovered, as rules

**The fixed tick.** 0x00585D70 turns each frame's time into a whole number
of 1/60 s world ticks (0x006CC8B0 = 60, 0x006CC7B0 = 1/60) and carries the
remainder. RECOVERED.

**The torque curves** (0x00583EA0). A uniform table of n values from x0 to
x1: u = (x - x0) * (n - 1) / (x1 - x0), i = u truncated toward zero; below
index 0 the first value, at or past n - 1 the last, otherwise
`(1 - t) * v[i] + t * v[i + 1]` with t = u - floor(u). Truncation and floor
disagree for -1 < u < 0, where the recovered code mixes v[0] and v[1] by
u + 1; `vehicle_uniform_curve` keeps the quirk and a test pins it. The
engine never evaluates below idle, because its speed is clamped there.

**The crank torque** (0x00475588..0x00475626). With throttle th in [0, 1]:
`T = torque(rpm) * th - engine_brake(rpm) * (1 - th)`, and T = 0 when
th < 0.05 (0x006CC964) and the engine speed is exactly idle. The divisor at
engine +0x138 is 1.0 (0x004751F8) and the nitrous term (engine +0x194, curve
0x006FFF34) is off for a stock car; neither is modelled.

**The wheel torque** (0x0047562C..0x00475661). Wheel k gets
`final_k * ratio[gear] * T * fraction_k * 0.5`, with final_k +0x2CC for
k < 2 and +0x2C8 otherwise, fraction_k 1 - rear for k < 2 and rear
otherwise.

**The engine speed** (0x00475847..0x004758DC, 0x004759BA..0x004759F6). From
the wheels' rolling speed w (rad/s) and K = 9.549304 (0x006CCB78,
60 / 2 pi): for a rear fraction below 0.2, `(w1 + w0) * final(+0x2C8) *
0.5 * K`; above 0.8, `(w3 + w2) * final(+0x2CC) * 0.5 * K`; otherwise the
mean of the two. Times the gear ratio, that is the wheel rpm; the engine
speed is it clamped to [idle, max]. The pairing of the finals with the
wheels here is the opposite of the wheel torque's; every retail racer has
equal finals, so it changes nothing, and the model keeps both as read.

**The brake-axis wheel pin** (0x004757DA..0x00475845, recovered while
fixing the tachometer drop). Before the engine speed is derived, the same
function walks the four wheels: where the brake input exceeds 0.95
(0x006CC968) and a wheel's spin is slower than 30 rad/s (0x006B719C), the
spin is pinned to exactly zero and a per-wheel flag at engine+0xB8+4k is
set. Under full brake, then, the original's needle follows the wheels down
and rests at idle from the moment the driven wheels fall below 30 rad/s,
never below it. The brake the wheels see combines two axes:
0x00474B4B..0x00474B6F takes the larger of the processed service brake and
half the handbrake axis (0x6CC7DC = 0.5). That handbrake axis is the
handbrake's own merged input - 0x00461E10 stores its digital code 0x91 at
car+0x2A8 and its analog code 0x92, doubled and clamped to 0xFF, at
car+0x2A9, and 0x00461E45 takes the digital byte when it is not zero, else
the analog's; the service brake never passes through 0x00461E10. The model
follows the max rule since the review of the tachometer fix (the fix's
first round wrongly took 0x91 for the service brake and gave the handbrake
the axis at full value); the pin at exactly 30 rad/s is not reproduced, so
the model's full-brake engine speed still follows the road all the way to
a stop instead of resting at idle from about 9 m/s.

**The automatic gearbox** (0x004757D0..0x004759B4). Gear slots: 0 reverse,
1 neutral, 2 first. In the automatic mode the slot is at least 2; while the
slot is below the top and the wheel rpm exceeds up[slot], shift up; then
while the slot is above 2 and the wheel rpm is below down[slot], shift down;
the rpm is recomputed with each new ratio. Both loops may move several
slots in one call.

**The shift thresholds** (0x0046D1C0, with the flag at 0x00700030 set, as
it is in the retail data). For each slot g from first to the one below top:
search from (redline + idle) / 2, in steps of 50 rpm (0x006B7244), for the
first speed at which the next gear's wheel torque,
`torque(rpm / r_g * r_next) / r_g * r_next`, exceeds this gear's; if the
search reaches the redline, redline - 100 (0x006CC9F8). A ratio no larger
than 1e-5 (0x006CCB7C) is skipped. The top slot's threshold is the redline,
and down[g + 1] = r_next / r_g * up[g], the speed the upshift lands at. For
the 240SX: up 7400, 7400, 7150, 6750, 7500 (first to fifth) and down
4238.12, 5088.96, 5466.36, 6075.00 (second to fifth).

**The struts** (0x00475A00..0x00475BB6). The compression x is the rolling
radius plus the strut's signed distance to the ground along the body's
vertical: zero with the ground exactly one radius below the attachment
point. Below the extension limit the wheel is off the ground; above the
compression limit x is held there. With v = (x - x_previous) / dt:

    F = (1 + progressive * max(x, 0)) * spring * x + damping(v) * v
        + L_k + anti_roll * (x - x_partner),   F >= 0

in that order: the spring and damper, then `fadd [ecx+0x110]` at
0x00475B8A, which is L_k, then the anti-roll term.

**The strut preload** L_k (0x00475460, called from 0x0047461E and
0x00474637, written at 0x004754BB..0x004754D1). With the flag at
0x006FFDE4 set - its retail value is 1 - cg is the centre of mass's first
component (record +0x110, sub +0x00), otherwise 0. With a = wheel 0's
longitudinal - cg and L = a - (wheel 3's longitudinal - cg):
rear = a / L * 0.5 for each rear wheel and front = 0.5 - rear for each
front wheel, each times the total T = mass * 9.8 + arg * (+0x37C). For the
240SX, L_k is 3183.6 N on each front wheel and 2794.4 N on each rear one,
53.3% front, and they add up to the weight: the car rests at x = 0 on every
strut with the whole 125 mm of bump travel ahead of it. The argument's term
(+0x37C is 0.01 for the 240SX) grows with a speed-like value built at
0x004745E6..0x00474610 from the car's +0x3D0 and the aero block's +0x374
and +0x378; that argument was not traced, so the model uses the preload at
rest, arg = 0 (APPROXIMATION).

**Drag** (0x0045687A..0x0045689B): a deceleration of
(1 - [car +0x47C]) * drag * v^2 / mass. The factor (1 - [car +0x47C]),
probably a drafting term that is 0 for a car alone, is left out
(APPROXIMATION), and so is the question of where the physics applies drag:
that function is a helper that predicts a deceleration, and the step that
integrates the body was not traced to its drag.

**Gravity**: 9.8 (0x007000AC).

## The model

`VehicleModel::step` advances a `VehicleState` by exactly 1/120 s. Per step:

1. Gear requests, then the engine speed for the gear now selected.
2. Steering slews toward the input times the speed-dependent lock.
3. The crank torque from the engine speed the previous step left, shared to
   the wheels by the recovered rule.
4. For each wheel, one ground query from the strut's attachment point,
   lifted by `probe_lift`, down the body's vertical; the compression and
   the recovered strut force.
5. For each wheel on the ground, the tyre: slip, the traction limit on the
   engine's torque, the wheel's spin solved against it, the friction
   circle, rolling resistance.
6. Drag, gravity, then semi-implicit Euler: velocity, angular velocity,
   position, orientation (a unit quaternion, renormalised).
7. The engine speed and the automatic gearbox from the new wheel spins.

The state is the centre of mass's position and velocity, the orientation
and angular velocity, each wheel's spin and compression (and the previous
compression, for the damper's rate), the road-wheel angle, the engine speed
clamped and unclamped, the gear slot, the gearbox mode and the shift hold.

### Frames

SI units. The world is right-handed with z up, which is the raw slot order
of the track collision records (`include/nfsu/track_contact.h`, on main
since PR #164: component 2 vertical, larger is higher). The body frame is
the wheel table's: +x forward, +y to the car's left, +z up. The state's
position is the recovered centre of mass, (0.25, 0, 0.28) in that frame for
the 240SX, about half a metre above the ground; the body frame's origin,
which the car's meshes are placed in, is position - R * centre of mass. A
positive steer turns left, a positive yaw rate.

### The approximations

| Field or rule | Value | Why |
| --- | --- | --- |
| step rate | 120 Hz | two steps per recovered 60 Hz tick; the recovered struts (35 kN/m, progressive) and the tyre-to-wheel coupling are stiff, and 120 Hz keeps the explicit terms well inside their stability bound while dividing the display rate evenly |
| `unit_scale` | 1000 | tonnes and kN to kg and N, "Units" above |
| inertia selector | the plain inverse inertia (body +0x150) | 0x00584B42..0x00584B69 also builds a second inverse (body +0x190) with the yaw moment times +0x270 (2.0 for the 240SX, 1.88 for the Supra and the Skyline), and 0x0058514D picks it when body +0x138 is non-zero; what sets +0x138 was not traced |
| strut preload speed term | arg = 0: the preload at rest | the argument of 0x00475460 was not traced (above) |
| strut force direction | along the ground normal, at the contact point | along the body's vertical, a pitched car at rest would be pushed along the ground; where the game applies it was not traced |
| tyre force height | `tyre_force_lift` = 0.6: a tyre's force acts 0.6 of the way from the contact point up to the centre of mass's height along the body's vertical | at the contact point, the recovered centre of mass (about 0.5 m up) and friction (1.8) roll the car over at about 1.46 g (half the track, 0.73 m, over 0.5 m), before the tyres slide - and this model did exactly that under full lock at 25 m/s before the lift was added. The game does not roll its cars at the grip limit, so its tyre forces cannot act at the contact with these values; where they act was not traced. 0.6 leaves an effective height of about 0.2 m. Kept at 0.6 in round 3: with the traction limit, 0.8 gives the same spin matrix (below), so no measurement argues for moving it |
| anti-roll order | both wheels' current compressions | the recovered loop runs wheel by wheel, so wheel 0 sees wheel 1's previous value; the model is symmetric instead |
| bump stop | live `bump_stop_rate` defaults to zero | original588A5D multiplies capped excess compression by BSS734538; authenticated owner recordings observe zero after initialization and during circuit, drag and drift play. See STRUT_CONTACT_AUDIT.md for scope and consumer checks |
| tyre, longitudinal | `longitudinal_stiffness` = 12 per unit slip ratio, times the load | the recovered longitudinal coupling (0x00475667..0x00475797) was not reproduced |
| tyre, lateral | RECOVERED and implemented (0x00587740, Unicorn-verified): the slip angle blends seven load curves - p = slip x 343.775/slip_scale, the 240SX's 12 making p half the slip in degrees, curve k covering 2k..2k+2 - each evaluated at x = 0.8 x wheel load kN / lateral_load_scale, the blend x lateral_scale x 2.5; curve 0's BSS values are zero so force leaves zero with slip; the friction circle then scales both components. The old invented stiffnesses (10 front / 20 rear) are gone: the real curves give 1.69 g saturating grip where the linear model gave 1.36, curve 1 dipping past its peak, and full lock rotates the car above 18 m/s - the recovered breakaway the owner felt as a live rear end |
| traction limit | `traction_limit` on: the engine's torque at each wheel on the ground, driving or braking, is clamped to radius * sqrt((mu_static * load)^2 - Fy^2), where Fy is the lateral force the slip angle asks for; nothing where Fy alone reaches the peak | an ideal traction control. Without it, full throttle at full lock from 8 to 12 m/s spins whatever the tyre stiffnesses (61 to 79 degrees at 10/20), and so does a lift-off at full lock from 20 to 25 m/s at 10/20 (26 to 28 degrees), because engine braking at the rear takes grip the cornering force needs. The recovered coupling of engine and wheels, 0x00475667..0x00475797, was traced only in part ("What is not known") and is not reproduced, so how the game keeps its cars from this is unknown |
| slip denominator | never below `slip_speed_floor` = 2 m/s | the tyre is a damper, not a division by zero, at a standstill |
| friction circle | up to the static coefficient the linear force stands; past it the force falls toward the sliding coefficient: limit = slide + (peak - slide) * peak / demand | the two coefficients are recovered; the shape between them is not |
| wheel spin | implicit against the linear tyre, explicit once the tyre saturates; `wheel_inertia` = 1.2 kg m^2 | stable at 120 Hz with a stiff tyre |
| brakes | `brake_torque_front` = 1400, `_rear` = 450 N m per wheel at full input | the recovered brake itself is traced now: 0x00475699..0x004756C0 makes it spin-proportional damping, -0.5 x brake x clamp(spin, +-20) in the record's torque units (500 N m per rad/s of clamped wheel speed at the full axis, the same on every wheel), answered by the pin rule above. Implementing that damping alone was tried and reverted the same day: with the engine-wheel feedback operand unrecovered (engine+0x74+8k, whose writer no static scan has found), the damping alone locks all four wheels almost at once - 100-0 km/h in 24.0 m with every wheel sliding, and the handbrake re-locks the driven wheels, resurrecting the tachometer drop the first round fixed. The invented constants stay until the feedback term is recovered; the measured attempt is in build/tach-glm of the tachometer worktree |
| handbrake | none of its own: the wheels see max(service brake, 0.5 x handbrake) | RECOVERED at 0x00474B4B..0x00474B6F over the 0x00461E10 handbrake axis (docs/CONTROLS_CAMERA.md, docs/VEHICLE.md "The brake-axis wheel pin"); the earlier invented 3000 N m rear lock pinned wheel-derived engine speed at idle the moment it was pulled at speed, which the owner reported as the tachometer dropping toward zero |
| steering | lock = `max_steer` / (1 + (v / `steer_speed_reference`)^2) with 0.55 rad and 12 m/s; slews at `steer_rate` = 3 rad/s | not traced; the lock falls with the square of the speed so that what full input asks of the tyres changes slowly with speed, as the path radius a given acceleration allows grows with v^2. `nfsu_vehicle_tests` checks the approximate input path; production `recovered1` uses the separate recovered steering path |
| rolling resistance | 0.015 of the load, faded in over 0.5 m/s | not traced |
| engine braking | the recovered curve times `engine_brake_scale` = 0.5, and only while the wheel rpm is above idle | at full strength it brakes the car at about 0.5 g in first gear, where the recovered gearbox keeps it below 64 km/h, and with the brake on it locked the rear wheels; below idle it would push a slow car backwards. The recovered coupling that carries it to the wheels was not reproduced, so how much reaches a wheel in the game is unknown. Even halved, it outweighs the throttle below about 0.30 at every engine speed ("Drivability" below). Kept at 0.5 in round 3: with the traction limit, which bounds engine braking at each wheel too, 0.25 gives the same spin matrix (below) |
| rev limiter | the throttle part of the crank torque is cut while the wheel rpm exceeds the maximum | the recovered code only clamps the stored engine speed; without a cut a manual first gear drives on past it |
| shift hold | `auto_shift_hold_steps` = 36 (0.3 s) after an automatic shift | the recovered rule has no hysteresis, and this model's tyre slip drops the wheel rpm across an upshift by more than the rule's margin, which shifted straight back down |
| automatic reverse | a down request in first below 1 m/s selects reverse, an up request in reverse selects first | RECOVERED (docs/CONTROLS_CAMERA.md, the reverse row): engine+0x134 is the controls context's gear slot +0x1D4 through vtable+0x24 (`0x00424FF0: mov eax,[ecx+0x1D4]; ret`), where -1 is the automatic mode, 0 forces the reverse slot and ratio at `0x004759A8`, and a manual gear clamps. The port's down-request-into-reverse and up-request-out follow that slot mechanism; the engage thresholds of the machine's own zeroing at `0x00461C34`, and the reverse input axis 0x8F/0x90 (car+0x2A4, no driving consumer found), remain unported |
| surfaces | every surface uses surface 0's friction factors, 1.0 and 1.0 (table 0x006EF2F0, stride 0x94) | the track's surface ids are not decoded (issue #158) |

### Controllability and breakaway

`nfsu_vehicle_tests` measures every case in the full-lock throttle/brake
matrix and reports the complete spin count. Its low-speed assertions and
`test_snap_oversteer` distinguish controllable inputs from breakaway in this
model. Current values live in the test output, not a copied document table.
The earlier matrix measured an invented linear tyre model and is not evidence
for the recovered lateral curve family now used.

The matrix exercises `VehicleModel`'s approximate raw-input steering lock.
Production `RaceSession` with `recovered1` instead supplies road angles from
`steering_step` through `VehicleInput::road_steering`; these paths must not be
conflated. Meaningful production acceptance compares the same recovered1
steering, controls and handling on the console, including braking, rear slip
and unexpected rollovers. Passing a numerical matrix alone does not close it.

The engine-wheel feedback at `0x00475667..0x00475797` remains incomplete.
Its per-wheel operand still lacks authenticated producer/cadence evidence,
so the existing traction limit and force application height remain labelled
approximations. No new stiffness or damping is introduced to hide breakaway.

### What is not known

- The complete wheel/tyre coupling: the lateral family of 0x00587740 is
  now implemented with authenticated curve samples and load/angle scales;
  its surrounding live-wheel coupling remains incomplete, as does the longitudinal
  coupling of 0x00475667..0x00475797, including the 0.008 divisor
  (0x006B71B8) and the speed table 0x00705E78. What was read of the
  coupling, the second half of the per-wheel loop of 0x00475530 (the
  engine's virtual slot 6):
  - The speed table's descriptor at 0x00705E78 points at three (x, y)
    pairs at 0x00705E60: (0, 0.05), (2, 0.322) and (20, 0.5). 0x00475742
    pushes it and 0x00475747 calls 0x00583F20, the pair-table evaluator:
    linear between pairs, the end values held outside them, `ret 8`. It is
    not 0x00583EA0, the uniform curve of the torque curves above. Its
    argument is the length of the three-float vector at +0xA0 of the
    object at engine +0x14, read here as the car's velocity; at 8, 10 and
    12 m/s the table gives 0.381, 0.401 and 0.421.
  - 0x0047574C..0x00475767: for a driven wheel, one whose drive fraction
    ([esp + 0x10], set at 0x00475562 and 0x00475572: 1 - the rear
    fraction at the front, the rear fraction at the rear) is above 0, the
    table's value becomes table / (A * B) * fraction, with A = 1.25
    (0x006B71A0) and B = 1 (0x006B71BC), both scaled up when engine
    +0x138 is above 1 (0x004756C6..0x0047570F). An undriven wheel keeps
    the undivided table value.
  - 0x0047576B..0x00475781: the wheel's result is (B * drive + brake -
    that * [edi + 0x74 + 8 k]) / 0.008, where drive is the wheel's share
    of the crank torque (0x00475655..0x00475661) and brake is 2.5 times
    the brake input (engine +0x12C) times the wheel's spin clamped to
    +-20 times -0.2, that is -0.5 * brake * spin.
  - What writes [edi + 0x74 + 8 k] was not found. Without it the rule
    cannot be reproduced.
  The model's tyre is the largest approximation here.
- Where the struts' and tyres' forces are applied, the brakes, the
  steering and the clutch, none of which was traced; what sets body +0x138,
  which doubles the yaw inertia; where the physics applies drag, and car
  +0x47C.
- +0x374 and +0x378 (0.14, 0.155 for the 240SX) and +0x37C (0.01):
  0x00456969 averages the first two, multiplies by a speed-like argument and
  adds 9.8 * mass, and the preload's total adds the argument times +0x37C;
  that reads like downforce, but the argument was not traced.
- +0x2C4 (500), +0x2D4 (0.04), +0x2DC (2), +0x30C (0.00023), and whatever
  the record holds past +0x9BB. The per-level tables from +0x3C0 are
  decoded in [`VEHICLE_TUNING.md`](VEHICLE_TUNING.md), which also lists
  what of them is still unread: +0x384..+0x38C and +0x3B0..+0x3BF have
  writers and no reader.
- Whether the game integrates one 1/60 s step per tick or subdivides it.

## Determinism

The pin below holds because every step is the same sequence of IEEE-754
binary64 operations on every platform:

- Only +, -, *, / and comparisons, plus `vehicle_sqrt` (Newton's iteration
  from an exponent-halved guess, seven rounds) and `vehicle_sin` /
  `vehicle_cos` (Cody-Waite reduction to [-pi/4, pi/4], Taylor polynomials),
  which are built from the same operations. No libm function is called in
  the step: MinGW's `sqrt` is not correctly rounded (x87 double rounding)
  and sin, cos, atan2, exp and pow differ between libms. `finite` is
  `(x - x) == 0`, not `std::isfinite`; truncation and floor are integer
  conversions. The script parser reads numbers as integer / 10^k, one
  correctly rounded division, never `strtod`.
- Floating-point contraction is disabled on the library
  (`-ffp-contract=off -fno-fast-math`, `/fp:precise` under MSVC), and
  `src/vehicle.cpp` refuses to compile unless `FLT_EVAL_METHOD` is 0, which
  rules out x87 excess precision.
- A fixed step, no clock, no uninitialised state (every member has a
  default), no unordered container, and a ground query that must answer the
  same way for the same segment.

The pin is not decoration on the console. Recompiling `src/vehicle.cpp`
with the exact `build-switch` command (devkitA64 GCC 16.1.0, `-O2`), `-S`
in place of `-c`:

| Options | fmadd | fmsub | fnmadd | fnmsub | fmla | fmls | total fused | fmul |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| as configured | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 339 |
| `-ffp-contract=off -fno-fast-math` removed | 148 | 25 | 4 | 27 | 2 | 0 | 206 | 146 |

`src/vehicle_script.cpp` has no fused instruction either way. The Switch
test `nfsu_switch_fma_census` includes both objects in its census of every
project object and fails on any fused instruction (issue #175,
[FLOATING_POINT.md](FLOATING_POINT.md)).

## The telemetry and its pin

One record per step, the state at the end of the step and the forces that
produced it: the step count, position, velocity, the body's forward axis,
angular velocity, forward and lateral speed, engine rpm clamped and
unclamped, crank torque, gear slot, the inputs as applied, the road-wheel
angle, and per wheel its compression, load, spin, slip ratio, lateral slip,
longitudinal and lateral force, the drive torque the recovered split gives
and the part of it the traction limit let through, surface, contact and
sliding flags. `encode_vehicle_telemetry` writes it as 520 little-endian
bytes -
every double as its IEEE-754 bits, every integer and flag as a u32 - in
that order; `VehicleTelemetryHash` is the SHA-256 (`nfsu/sha256.h`) of the
records in step order.

The built-in `mixed` script's current pin is owned by
`kVehiclePinnedMixedSha256`. `nfsu_vehicle_tests` and
`nfsu_probe_vehicle_sim_guards` check it. The tyre curve, handbrake and
arithmetic changes affect these recordings; current hashes belong in those
tests, not in this document. Reproduce the telemetry with:

```text
build/nfsu_probe vehicle-sim mixed --every 0
build-release/nfsu_probe vehicle-sim mixed --every 0
```

The probe accepts `--handling <Global/GLOBALB.BUN>` to compare the retail
handling with the built-in record. Default and Release agreement must be
verified at the same candidate head.

### On the console

The Switch tree builds `nfsu_switch_vehicle.nro`, which runs the eight
scripts through the AArch64 compile of the same sources and options, prints
each hash and its run time, and for `mixed` prints `result: EQUAL to the
pinned desktop telemetry` or `DIFFERENT from`. The Switch CTest suite runs
on the build machine, which cannot execute AArch64 code, so it asserts the
script census and the NRO's structure, not execution of the model. It is run with

```text
nxlink -s build-switch/switch/nfsu_switch_vehicle.nro
```

and the `result:` line read off the screen.

Historical console evidence at `5521427` is recorded in
[PR 169 comment 3856](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/169#issuecomment-3856).
That measurement does not establish telemetry agreement or timing for the
current model. A new candidate requires a new hardware run.

## Scripted input

The text form, one directive per line, `#` starting a comment line:

```text
speed <m/s>                  optional, before any segment: initial forward speed
gearbox <automatic|manual>   optional, before any segment
<steps> <throttle> <brake> <steer> <handbrake 0|1> <none|up|down>
```

Numbers are fixed decimals: an optional `-`, at most 9 integer digits, an
optional `.` and at most 6 decimals, read as integer / 10^k so that every
platform reads the same double. A shift request acts on its segment's first
step. Past the script's end there is no input. Every refusal names its line;
the limits are 1 MiB, 100000 segments and 12000000 steps.

| Built-in | What it drives |
| --- | --- |
| `rest` | two seconds of nothing from rest |
| `accelerate` | half a second at rest, then thirty seconds of full throttle |
| `brake` | full brake from 100 km/h for eight seconds |
| `corner` | half throttle and a steer of 0.2 left from 20 m/s for ten seconds |
| `slide` | from 25 m/s: straight, full left input for three seconds, then the handbrake with half the input, then straight |
| `top-speed` | two minutes of full throttle |
| `manual` | the manual gearbox: first into the limiter, up, up, down |
| `mixed` | the pin: rest, full throttle, left and right at half throttle, braking, the handbrake with a steer, throttle, coasting, in twenty seconds |

## The probe

```text
nfsu_probe vehicle-sim <script-name> | --script <FILE>
      [--steps N] [--every N] [--handling <GLOBALB.BUN> [--car NAME]]
```

It prints the handling's source, the script, the shift thresholds, a line
every `--every` steps (120 by default, 0 for none), the last step and the
telemetry hash, and exits 0; 1 on an argument it refuses, 2 on a script file
it cannot read or parse, 3 on a handling file that cannot drive the model.
It reads, never writes. `nfsu_probe_vehicle_sim_guards` pins every exit and
diagnostic, and requires the file form of `rest` and `mixed` to hash the
same as the built-in ones.

## Tests

`nfsu_vehicle_tests`, in both desktop builds:

The test owns arithmetic tolerances, telemetry hashes and measured bounds.
It checks record decoding, model refusals, suspension, damping, braking,
cornering, grip saturation, breakaway, force height, drivetrain, ground/input
sanitization and determinism. The range-reduced arctangent has full-domain
accuracy and monotonicity checks, including extreme slip.

The full-lock matrix prints every observed spin count across its entire
matrix, including high-speed and braking cases. Low-speed controllability
assertions remain distinct from the measurement; passing them does not
establish original high-speed handling or console acceptance. The recovered
lateral family replaces the earlier linear stiffness approximation, so old
matrix and snap-oversteer measurements do not describe current behavior.
The force application height and speed-dependent steering lock are still
explicit approximations and require comparison with original wheel coupling.


Fourteen deliberate breaks of `src/vehicle.cpp` were each built and run,
then the source restored byte for byte and touched: the eight mutants the
review of comment 3579 found surviving (the four-wheel-drive engine speed
summed, the front-wheel-drive one read from the rear wheels, the idle rule
removed and its threshold moved to 0.5, the finals' pairing swapped in the
engine speed and in the wheel torque, the progressive term applied to
negative compression, the bump stop ignored), the preload left out, a
100 rpm shift search step, drag with its sign flipped, no upper clamp on the
engine speed, the curve's fraction by truncation instead of floor, and the
anti-roll partners crossed between the axles. Each was killed with a
`test failure:` line, and the restored tree passes.

Round 3 ran sixteen more the same way, each killed by the check named:
the tyre force lift at 0.5 and at 0.7 (the tyre force height; 0.5 is the
review's h03, which before this round failed only through a spin's
recovery); the automatic gearbox's downshift loop removed (the review's
m14: braking ends in second); the traction limit off (the grip limit, the
spin matrix and the limit's recomputation), blind to the lateral force,
from the sliding coefficient, applied as a force instead of a torque, on
driving torque only (the lift-off runs of the matrix), left out of the
implicit wheel solve (the wheel balance), left out of the sliding branch
(the grip limit and the matrix), and reported unclamped in the telemetry
(the recomputation and the balance); fronts at 12 and rears at 18 (the grip
limit and snap oversteer); the lift refusal's round-2 message (its name);
the applied torque left out of the record (the layout); and the engine
braking at 0.25, which only the pin catches. The traction limit's clamp of
the lateral force to the peak was taken out of the code as an equivalent
mutant: `vehicle_sqrt` answers 0 for a negative argument, which is what the
clamp gave. Round 4 added the three the review of comment 3666 found
surviving on the branch for a wheel in the air - the engine's torque
ignored, doubled, and reported as 0 applied - which no run reached until
the driven drop; the wheel balance and the whole-split check now kill all
three.

## Drivability

`nfsu_vehicle_tests` covers standing starts, braking, approximate steering,
traction saturation and breakaway. Its full-lock matrix is a measurement of
the approximate steering mode, not a qualification of production controls.
`nfsu_steering_tests` exercises the recovered native steering pins, the actual
RaceSession pad path, and moving-car service/handbrake engine-speed behavior.

Production `recovered1` can produce supported sliding under full input.
Sideslip above a measurement threshold alone does not prove a spin or a
rollover. Qualification records initial speed, pedal inputs, actual speed,
body orientation, wheel support and response to releasing the stick. Do
not infer console controllability from an isolated flat-ground matrix.

The tyre curves use the recovered load/slip scaling. Wheel inertia, the
implicit longitudinal solve and the engine feedback interface still include
approximations. Completing the original wheel/engine coupling and measuring
the owner's runtime behavior remain acceptance work; no cosmetic RPM lag
substitutes for that evidence.

## How issues #59 and #60 plug in

- **#59, controls and camera.** A controller frame becomes one
  `VehicleInput` per step: throttle, brake and steer in their ranges,
  handbrake, and a shift edge. The model clamps and sanitises what it is
  given, so a controller can pass its axes straight through. Baseline uses the model's approximate lock; production recovered steering
  supplies the road-wheel angle through the RaceSession adapter. Run the
  model at 120 Hz regardless of the display rate - two steps per 60 Hz
  frame - and give the camera the telemetry's position, forward axis and
  velocity. The position is the centre of mass; the meshes' origin is
  position - R * centre of mass, with R from the state's orientation.
- **#60, vehicle to track collision and reset.** Implement `VehicleGround`
  over the track contact world: `cast(from, direction, length)` is one
  segment query, and the track contact layer's first-contact query answers
  exactly that, in the same frame, with the normal facing the segment's
  start. Return the distance, the normal and, once decoded, the surface;
  the model makes the normal unit length and treats a hit off the segment,
  or one whose normal faces away from the car, as a miss. The track side
  is float and the model double, so the adapter converts; and since the
  model takes any upward-facing hit as ground, the adapter should refuse
  walls, as `kTrackContactRoadMinUp` classifies them.
  The model asks four segments a step, each from half a metre above a
  strut's attachment down the body's vertical, at most 0.5 + radius + 0.13
  m long. A reset is `VehicleModel::reset` with a `VehiclePose`: the ground
  point under the centre of mass, the heading and a speed; it takes the
  ground there as level. Walls, the body's own collision and out-of-bounds
  are #60's: the model only has wheels.
