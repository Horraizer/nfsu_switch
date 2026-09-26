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
The records after the sub-object, from +0x3C0, are the per-level upgrade
tables (tyres at +0x6C0, suspension at +0x7F0 + 80 * level as
[`PART_PLACEMENT.md`](PART_PLACEMENT.md) shows, engine and gearbox before
them); a stock car does not read them and this model does not either.

The 240SX is record 18, at absolute offset 1736752.
`nfsu_probe vehicle-sim --handling <GLOBALB.BUN>` decodes the fields below
out of the retail record and compares them bit for bit with the built-in
table `stock_240sx_handling()`; on the retail file it prints
`handling: car type 18 240SX record @1736752 equal to the built-in 240SX
table`.

### The fields the model reads

| Record offset | Field | 240SX | Label and evidence |
| --- | --- | --- | --- |
| +0x110, +0x114, +0x118 | centre of mass (longitudinal, lateral, vertical), body frame | (0.25, 0, 0.28) | RECOVERED: 0x004745B8..0x004745CF copies the three into the rigid body (+0x1D0), 0x00588541..0x00588558 subtracts them from a wheel's position, and the strut preload of 0x00475460 levers the weight about the first (below) |
| +0x120 + 0x30 k | wheel k position (longitudinal, lateral, vertical) | (1.39, +-0.73, 0.08) front, (-1.04878, -+0.73, 0.08) rear | RECOVERED, [`PART_PLACEMENT.md`](PART_PLACEMENT.md) |
| +0x130 + 0x30 k | wheel k rolling radius | 0.288 front, 0.3135 rear | RECOVERED; 0x00475A82 adds it to the strut's signed distance to the ground |
| +0x1E0 / +0x200 | tyre block, front / rear (0x20 each) | below | RECOVERED: the tyre constructor 0x005868E0 keeps a pointer to it (0x005868FC), passed from 0x0047438C as sub +0xD0 + axle * 0x20 |
| tyre +0x0C | peak (static) friction coefficient | 1.8 / 1.7 | RECOVERED: 0x005878A2..0x005878CF multiplies it by the surface's static factor and the wheel load into the force limit |
| tyre +0x10 | sliding friction coefficient | 1.5 / 1.47 | RECOVERED: 0x00587546..0x0058756A, the same product with the surface's sliding factor |
| tyre +0x00, +0x04, +0x08 | slip scale, load scale, lateral scale | 12, 1, 1 | RECOVERED values; read by the lateral-force curve family at 0x00587740, where +0x00 divides an angle converted from radians to degrees (0x0058777C, 0x00587783), +0x04 divides the load term (0x005877AD) and +0x08 multiplies the result (0x005877C9). Decoded and checked, not used: that curve family is not reproduced |
| +0x220 | mass, tonnes | 1.22 | RECOVERED value; the weight rule 0x00463C51..0x00463C58 multiplies it by g = 9.8 beside the averaged friction coefficients, and the drag rule 0x0045689B divides by it. Tonnes is an inference, below |
| +0x224, +0x228, +0x22C | length, width, height, m | 4.52, 1.69, 1.29 | observed only: they equal the real car's dimensions; no reader was traced, and the model does not use them |
| +0x230, +0x244, +0x258 | inertia about the body axes, tonnes m^2: the diagonal of a 4x4 matrix at +0x230 | 0.4595537, 2.2462742, 2.367461 | RECOVERED: the rigid body keeps a pointer to +0x220 (0x0047415D into 0x00584800, stored at 0x00584806), 0x00584B21 takes the mass from it and 0x00584B3D inverts the matrix at pointer +0x10 into the body's inverse inertia. In all 35 records the twelve other entries are zero and each diagonal entry is the box inertia of the record's own mass, length, width and height rounded to float, or one float step from it: at most 8.0e-8 relative (the S2000) and 3.2e-6 absolute (the bus, whose yaw entry is 54 t m^2) |
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
| bump stop | `bump_stop_rate` = 200 kN/m on the compression beyond the limit | the recovered code stores the excess, x - 0.125, per wheel (0x00475ACA), but what reads it was not traced. With the preload the car has the full 125 mm of bump travel at rest; a 3 m drop reaches the stop |
| tyre, longitudinal | `longitudinal_stiffness` = 12 per unit slip ratio, times the load | the recovered longitudinal coupling (0x00475667..0x00475797) was not reproduced |
| tyre, lateral | `cornering_stiffness_front` = 10, `_rear` = 20 per unit tan(slip angle), times the load | the recovered lateral curve family (0x00587740, curves at 0x006F084C and 0x007043D8) was not reproduced. Chosen by the spin matrix below: with the traction limit, a front of 12 still spins the car under half brake at full lock from 30 m/s (32.8 degrees of sideslip with a rear of 18, 27.5 with 20), 11 clears it by 2 degrees (18.0) and 10 by 4.4 (15.6); at a front of 10 a rear of 20 against 18 clears the same 32 runs with one fewer spin past the default lock (3 of 46 against 4). Round 2 had 12 and 18. The rear is twice the front, so the car understeers below the limit |
| traction limit | `traction_limit` on: the engine's torque at each wheel on the ground, driving or braking, is clamped to radius * sqrt((mu_static * load)^2 - Fy^2), where Fy is the lateral force the slip angle asks for; nothing where Fy alone reaches the peak | an ideal traction control. Without it, full throttle at full lock from 8 to 12 m/s spins whatever the tyre stiffnesses (61 to 79 degrees at 10/20), and so does a lift-off at full lock from 20 to 25 m/s at 10/20 (26 to 28 degrees), because engine braking at the rear takes grip the cornering force needs. The recovered coupling of engine and wheels, 0x00475667..0x00475797, was traced only in part ("What is not known") and is not reproduced, so how the game keeps its cars from this is unknown |
| slip denominator | never below `slip_speed_floor` = 2 m/s | the tyre is a damper, not a division by zero, at a standstill |
| friction circle | up to the static coefficient the linear force stands; past it the force falls toward the sliding coefficient: limit = slide + (peak - slide) * peak / demand | the two coefficients are recovered; the shape between them is not |
| wheel spin | implicit against the linear tyre, explicit once the tyre saturates; `wheel_inertia` = 1.2 kg m^2 | stable at 120 Hz with a stiff tyre |
| brakes | `brake_torque_front` = 1400, `_rear` = 450 N m per wheel at full input | not traced; below what the recovered static friction holds on each axle at the loads full braking puts on it, so no wheel locks: about 1.05 g from the brakes, and 1.24 g on average from 100 km/h with the engine braking the automatic gearbox adds (measured below) |
| handbrake | 3000 N m on each rear wheel | not traced; enough to lock them |
| steering | lock = `max_steer` / (1 + (v / `steer_speed_reference`)^2) with 0.55 rad and 12 m/s; slews at `steer_rate` = 3 rad/s | not traced; the lock falls with the square of the speed so that what full input asks of the tyres changes slowly with speed, as the path radius a given acceleration allows grows with v^2. With the round-3 tyres full input corners at 0.96 to 1.34 g, under the 1.55 g limit ("Drivability") |
| rolling resistance | 0.015 of the load, faded in over 0.5 m/s | not traced |
| engine braking | the recovered curve times `engine_brake_scale` = 0.5, and only while the wheel rpm is above idle | at full strength it brakes the car at about 0.5 g in first gear, where the recovered gearbox keeps it below 64 km/h, and with the brake on it locked the rear wheels; below idle it would push a slow car backwards. The recovered coupling that carries it to the wheels was not reproduced, so how much reaches a wheel in the game is unknown. Even halved, it outweighs the throttle below about 0.30 at every engine speed ("Drivability" below). Kept at 0.5 in round 3: with the traction limit, which bounds engine braking at each wheel too, 0.25 gives the same spin matrix (below) |
| rev limiter | the throttle part of the crank torque is cut while the wheel rpm exceeds the maximum | the recovered code only clamps the stored engine speed; without a cut a manual first gear drives on past it |
| shift hold | `auto_shift_hold_steps` = 36 (0.3 s) after an automatic shift | the recovered rule has no hysteresis, and this model's tyre slip drops the wheel rpm across an upshift by more than the rule's margin, which shifted straight back down |
| automatic reverse | a down request in first below 1 m/s selects reverse, an up request in reverse selects first | the recovered reverse is a mode (engine +0x134 = 0) whose control was not traced |
| surfaces | every surface uses surface 0's friction factors, 1.0 and 1.0 (table 0x006EF2F0, stride 0x94) | the track's surface ids are not decoded (issue #158) |

### The spin matrix

Round 3 of the review (comment 3622) found that full steering input spun
the round-2 car. The matrix below is what chose the round-3 tyres and the
traction limit: from each speed, full steering input with a constant
throttle of 0, 0.5 and 1 for five seconds, and half and full brake with full
lock from 30 m/s for four. A run spins when the body points more than 20
degrees off its path while the car still moves faster than 3 m/s; the cells
are the largest sideslip, in degrees, and `*` marks a spin. The 16 degrees
at low speed are close to the kinematic sideslip of full lock at walking
pace, atan(1.30 / 4.3) = 16.8 degrees (the centre of mass 1.30 m ahead of
the rear axle, which turns on a 4.3 m radius): a car that coasts to a crawl
at full lock reaches it without sliding.

Before, head 650d308 (fronts 12, rears 18, no traction limit): 15 of 32.

| throttle | 8 | 10 | 12 | 15 | 18 | 20 | 22 | 25 | 30 | 35 m/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 16.1 | 16.1 | 16.1 | 16.4 | 45.9* | 74.8* | 98.0* | 143.4* | 162.7* | 82.0* |
| 0.5 | 8.2 | 5.3 | 3.4 | 2.0 | 1.4 | 2.1 | 2.6 | 3.0 | 3.3 | 3.2 |
| 1 | 48.8* | 56.0* | 66.0* | 89.6* | 107.8* | 122.0* | 136.6* | 3.1 | 2.9 | 2.9 |

Half brake with full lock from 30 m/s: 137.5*; full brake: 29.3*.

After (fronts 10, rears 20, the traction limit): 0 of 32.

| throttle | 8 | 10 | 12 | 15 | 18 | 20 | 22 | 25 | 30 | 35 m/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 16.0 | 16.0 | 16.0 | 16.0 | 16.0 | 16.0 | 14.3 | 9.6 | 2.4 | 2.3 |
| 0.5 | 7.9 | 5.0 | 3.2 | 1.8 | 1.1 | 1.3 | 1.7 | 2.0 | 2.1 | 2.1 |
| 1 | 5.8 | 3.8 | 2.6 | 1.7 | 1.8 | 1.8 | 1.8 | 1.8 | 2.0 | 1.9 |

Half brake with full lock from 30 m/s: 15.6; full brake: 14.4.

An extended set of 46 runs adds throttles of 0.25 and 0.75 at the same
speeds, brakes of 0.3, 0.5 and 1 with full lock from 15 to 35 m/s, and locks
held at 1.25, 1.5, 2 and 3 times the default one at 15, 20 and 25 m/s with
a throttle of 0.6 (where they stay within 0.55 rad). The candidates, all
with the brakes, steering and everything else as above:

| fronts / rears | lift | engine braking | traction limit | spins of 32 | of 46 | |
| --- | ---: | ---: | --- | ---: | ---: | --- |
| 12 / 18 | 0.6 | 0.5 | off | 15 | 23 | round 2 |
| 10 / 20 | 0.8 | 0.25 | off | 3 | 8 | full throttle from 8, 10 and 12 m/s |
| 10 / 20 | 0.6 | 0.5 | off | 7 | 12 | also the lift-offs from 20 to 25 m/s and half brake from 30 |
| 12 / 18 | 0.6 | 0.5 | on | 3 | 11 | full throttle from 18 and 20 m/s, half brake from 30 |
| 12 / 20 | 0.6 | 0.5 | on | 1 | 8 | half brake from 30 m/s |
| 11 / 19 | 0.6 | 0.5 | on | 0 | 5 | |
| 10 / 18 | 0.6 | 0.5 | on | 0 | 4 | |
| 9 / 21 | 0.6 | 0.5 | on | 0 | 2 | |
| 10 / 20 | 0.8 | 0.25 | on | 0 | 4 | |
| 10 / 20 | 0.8 | 0.5 | on | 0 | 3 | |
| 10 / 20 | 0.6 | 0.25 | on | 0 | 3 | |
| **10 / 20** | **0.6** | **0.5** | **on** | **0** | **3** | **chosen** |

Neither the stiffnesses nor the traction limit alone clears the 32 runs;
together they do, and moving the lift or the engine braking then changes
no count in either set, so both keep the values round 2 chose them for. The
three spins left in the extended set are all locks the default steering
cannot reach: 1.5 times the default lock at 20 m/s, and 1.5 and 2 times at
25 m/s. Further understeer costs cornering at full input: 9 / 21 would
clear one more extended run and give 1.15 g at 20 m/s where 10 / 20 gives
1.28 and 11 / 19 1.43 ("Drivability").

At 20, 25 and 30 m/s, past the default lock, the car snaps into oversteer
inside a window of locks, not past a threshold. The window was swept with
the lock held fixed at f times the default full-input lock, f from 1.00 in
steps of 0.05 up to the largest step within the full 0.55 rad, for five
seconds each from the speed, with `run_steady`'s speed controller; a spin
is more than 20 degrees of sideslip while the car moves faster than
3 m/s. The cells give f and, in brackets, the largest sideslip in
degrees:

| speed | swept | last steady sample below | spinning samples | first steady sample above |
| --- | --- | --- | --- | --- |
| 15 m/s | 1.00 to 2.55 | - | none (the largest sideslip is 9.6, at 1.30) | - |
| 20 m/s | 1.00 to 3.75 | 1.25 (1.8) | every one from 1.30 (31.1) to 1.65 (22.0) | 1.70 (18.9) |
| 25 m/s | 1.00 to 5.30 | 1.40 (2.9) | every one from 1.45 (51.5) to 2.60 (20.5) | 2.65 (18.4) |
| 30 m/s | 1.00 to 7.25 | 1.65 (3.6) | every one from 1.70 (93.4) to 3.95 (21.0) | 4.00 (19.7) |

Each edge lies between the two samples beside it, so it is known to 0.05
times the default lock and no better. With a constant 0.6 throttle instead
of the controller every sample falls on the same side except one: at
30 m/s 4.00 also spins (20.7), and 4.05 is the first steady sample above
(19.5). Just above the window the sideslip is still close to the
20-degree mark (18.9, 18.4 and 19.7 degrees at the first steady samples);
no sample above the window spins, and at the largest one the sideslip is
1.6 degrees at 20 m/s, 3.0 at 25 and 4.4 at 30. `test_snap_oversteer`
brackets the lower edge at 20 and 25 m/s: 1.25 times at 20 m/s and 1.4
times at 25 m/s corner steadily, 1.4 times at 20 m/s and 1.5 times at
25 m/s spin. No test pins the upper edges.

The coupling of engine and wheels at 0x00475667..0x00475797 was traced
first, since a recovered limit would beat an invented one, and not
settled; what was read is under "What is not known". It subtracts from
each driven wheel's torque a term scaled by that wheel's drive fraction,
which is the shape a slip feedback would have, and its speed table gives
0.38 to 0.42 at 8 to 12 m/s, 76 to 84% of its largest value. But the
term also multiplies a per-wheel value whose writer was not found, so what
it does at 8 to 12 m/s is unknown. The traction limit stands in for it,
labelled.

### What is not known

- The tyre force curves: the lateral family of 0x00587740 (six curves by
  slip angle, blended, and a seventh past them) and the longitudinal
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
- +0x2C4 (500), +0x2D4 (0.04), +0x2DC (2), +0x30C (0.00023), the
  per-level tables from +0x3C0, and whatever the record holds past the
  engine block.
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
test `nfsu_switch_vehicle_fma_census` disassembles both objects of every
Switch build and fails on any fused instruction.

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

**The pin**: the built-in `mixed` script, 2400 steps from rest on flat
ground with the stock 240SX, hashes to

    B9EE418C4829E3ACC12EFC906EFD89A6381376E4E334EDD82AE7E9F37DFCF8E3

It was 20724B57...EC085 before round 3 of the review (comment 3622): the
record grew by the applied drive torque, 32 bytes, which changes every
script's hash, `rest` included, and the cornering stiffnesses and the
traction limit change the telemetry of every script that turns or drives.
Before that it was 85900502...0ADD5, until the fix round of review comment
3579: the strut preload and the recovered centre of mass, inertia, tyre
force height and brakes changed every script's telemetry.

`kVehiclePinnedMixedSha256` holds it; `nfsu_vehicle_tests` requires it and
`nfsu_probe_vehicle_sim_guards` requires the probe to print it. Reproduce:

```text
cmake -S . -B build -G Ninja
cmake --build build
build/nfsu_probe vehicle-sim mixed --every 0

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
build-release/nfsu_probe vehicle-sim mixed --every 0
```

Both print `telemetry: records 2400 bytes 1248000 sha256 B9EE418C...CF8E3`;
so does either with `--handling <Global/GLOBALB.BUN>` over the retail file.
"Debug" here and below is the default configure, `CMAKE_BUILD_TYPE` empty
(no `-O`, no `-g`), not `-DCMAKE_BUILD_TYPE=Debug`. All eight built-in
scripts give the same telemetry in both builds:

| Script | Records | SHA-256 |
| --- | ---: | --- |
| `rest` | 240 | `5C8E9F6D82A8F6CD9F9A8F551B630DD84E17C10C2D63B0F283721FDD56D25374` |
| `accelerate` | 3660 | `9045D7A4721733863790BB3C66440B427E853D78B34C01467B60CD58DC0B652F` |
| `brake` | 960 | `74AB8F49ED1DC642A1B40E9E1F5AF5F7578163422FB59BA6F1947EED83D83AB5` |
| `corner` | 1200 | `08EAC862DB52F7884AD7F948AA762250F20DCAEC363048BBBAE324B74A28F278` |
| `slide` | 780 | `07C5C60E2CF00197D9BC1E4DD7AE298674090E9ABD009BBE1D0160E145D21D43` |
| `top-speed` | 14400 | `E6C334F9473F80235A7C3C55D6DF09D6D20F6270069117A8FB9403EE3B051ECC` |
| `manual` | 1800 | `6B859D78DB23BE058DDAB12986101548F1A724D5E3D0566AB376EB6FF3C3ACAA` |
| `mixed` | 2400 | `B9EE418C4829E3ACC12EFC906EFD89A6381376E4E334EDD82AE7E9F37DFCF8E3` |

### On the console

The Switch tree builds `nfsu_switch_vehicle.nro`, which runs the eight
scripts through the AArch64 compile of the same sources and options, prints
each hash and its run time, and for `mixed` prints `result: EQUAL to the
pinned desktop telemetry` or `DIFFERENT from`. The Switch CTest suite runs
on the build machine, which cannot execute AArch64 code, so it asserts the
census above and the NRO's structure, not the hash. **Nothing here has run
on a console**: the console's hash is unobserved until someone runs

```text
nxlink -s build-switch/switch/nfsu_switch_vehicle.nro
```

and reads the `result:` line.

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

| What | Pinned by |
| --- | --- |
| the arithmetic | `vehicle_sqrt` within 4e-16 relative of the library root and exact on perfect squares; sine and cosine within 2e-15 on [-102, 102] |
| the curve | 0x00583EA0's interpolation, ends and quirk; the 240SX's torque points at 800 + 900 i rpm |
| the record | the retail values' stored bits at their offsets, the centre of mass and inertia among them; the round trip; 24 refusals, each by its offset |
| the model's refusals | no mass and a centre of mass ahead of the front axle, each by its offset; a negative, NaN or zero-divisor approximation; a tyre force lift above 1, by name; reset and step on an invalid model, a non-finite pose or state, or a gear past top |
| the gearbox tables | the 240SX's thresholds; the crossing at, and not one search step before, each threshold; 40 synthetic cars against a restatement of the search, some crossing between the points of a 100 rpm grid |
| rest | the recovered preloads, 3183.6 and 2794.4 N, add up to 9.8 * 1.22 t; every strut stays within 1 mm of zero compression, the loads within 0.5% of the preloads, the car within 0.1 mm, and it settles below 1e-4 m/s; the centre of mass about half a metre up |
| the strut force | the recovered rule with the preload, recomputed bit for bit from the telemetry for every wheel on the ground off its bump limit over `mixed` and `brake`, more than 100 of them with the strut extended below rest; a 3 m drop held within 0.25 m of rest by the bump stop, and sinking further without it |
| the drive layouts | front (rear fraction 0), four-wheel (0.5) and rear (1) drive with unequal finals, 3.9 front and 4.3 rear, over `mixed`: the wheel rpm and every wheel's drive torque bit for bit against the recovered expressions, which pins the cross-pairing of the finals, and the driven axles |
| the idle rule | at idle, throttle 0.04 and 0.0499 give no torque at all, 0.0501 and 0.3 the torque curve's share |
| forced first gear | an automatic gearbox in neutral is put in first with neutral's zero rpm for that one call, as 0x00475905 does, and first gear's on the next |
| acceleration | 0-100 km/h in 5.0 to 7.5 s (6.08 s); every upshift crosses its threshold; four upshifts to fifth; no downshift under full throttle; straight |
| braking | 100-0 km/h in 25 to 35 m (31.4 m) and 1.6 to 2.6 s, never below v^2 / (2 * 1.8 * g) = 21.9 m; no wheel locks; the held brake holds; the automatic gearbox starts in second, never shifts up, shifts down only once the rpm in the old gear is below its down threshold and into a gear whose up threshold is not passed, and ends in first |
| cornering | at 15 m/s and a steer of 0.3: left and right mirror to 1e-9; steady within 0.5%; lateral acceleration = speed * yaw rate within 2%; understeer between 0.5 and 1 of the kinematic yaw rate; no tyre sliding; load moves outward; the road wheels slew at the rate limit; 90% of the yaw rate within a second |
| the grip limit | the stable regime, with the lock held fixed at 15 m/s: doubling it from a quarter to half the default full-input lock (0.215 rad) doubles the lateral acceleration within 5%; the default lock slides no tyre and corners at 1.36 g; 1.25, 1.5 and 2 times it slide the rear tyres and corner at the same 1.55 g within 2%, under the rear's 1.7; every run keeps within 10 degrees of its path and above 0.6 of its speed, where a spin passes 25 degrees and falls below 0.3; the handbrake locks and slides the rear wheels and turns the car in |
| the spin matrix | the 32 runs of "The spin matrix": none passes 20 degrees of sideslip above 3 m/s (the worst is 16.0); without the traction limit full throttle at full lock from 10 m/s passes 40 |
| snap oversteer | with the lock held fixed, 1.25 times the default at 20 m/s and 1.4 times at 25 m/s steady (within 5 degrees, above 0.85 of the speed), 1.4 times at 20 m/s and 1.5 times at 25 m/s spin (past 25 degrees, below 0.3 of the speed) |
| the tyre force height | at half input from 20 m/s, the load moved across the track puts the lateral forces at 0.4 of the centre of mass's height within 8%, which is `tyre_force_lift` = 0.6 (measured 3.2% high, 4.1% at full input, growing with the body's roll, which the balance leaves out; a lift of 0.5 or 0.7 moves it by a quarter) |
| the traction limit | recomputed for every wheel on the ground whose tyre grips, over `mixed`, `accelerate`, full throttle at full lock from 10 m/s and full throttle from rest dropped from 3 m: 26760 wheel-steps, 644 of them held by the limit; every unbraked wheel's spin changes by exactly (applied torque - longitudinal force * radius) / inertia per second, gripping, sliding or in the air, over 26384 wheel-steps; in the air a wheel gets the whole recovered split and no longitudinal force, over 120 driven wheel-steps from the drop |
| top speed | 216 to 252 km/h (232.5) in fifth below the redline after two minutes, settled; the tyres' drive balances the recovered drag plus rolling resistance within 1% |
| the drivetrain | the wheel rpm is the recovered expression, bit for bit, at every step of `mixed`; the engine speed its clamp; full-throttle crank torque the torque curve; rear drive only, half each; the limiter; manual up, up, down on the steps asked; neutral drives nothing; manual and automatic reverse |
| inputs and ground | NaN and out-of-range inputs sanitised; a state with any non-finite value refused and left untouched; a ground that answers nothing gives free fall; a normal five units long gives the flat ground's telemetry exactly; a normal facing away, a distance before the segment and one past it are misses; the flat ground's segment rule; the same run turned 90 degrees and moved follows the same path |
| scripts | the decimal reader; the parser and 23 refusals by line; the limits; the built-ins |
| determinism | the same script twice; a run resumed from a copied state on a second model; the record's layout, 520 bytes with the applied torque at 248; the pin |

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

What the model does with the default approximations, measured with the
built-in scripts, for #59 to map a controller onto:

- **Standing start**, full throttle, automatic: 0-100 km/h in 6.08 s;
  upshifts at 59, 107, 152 and 189 km/h (the wheel rpm includes the driven
  tyres' slip, so these are a little below the thresholds' road speeds).
  Top speed 232.5 km/h in fifth after two minutes, drag-limited. Round 3
  changed none of this: in a straight line the traction limit does not
  hold the drive back.
- **Braking** from 100 km/h at full brake: 31.4 m in 2.29 s, a mean of
  1.24 g - the brakes alone give about 1.05 g, and the automatic gearbox's
  downshifts add engine braking. From 150 km/h 72.9 m, from 200 km/h
  131.0 m. No wheel locks. Unchanged in round 3.
- **Cornering** at full input, held at speed: 1.13 g at 10 m/s, 1.34 at
  15, 1.28 at 20, 1.13 at 25, 0.96 at 30, 0.81 at 35 and 0.68 at 40.
  Round 2 gave 1.24, 1.59, 1.64, 1.48 and 1.32 from 10 to 30 m/s. Review
  comment 3666 measured 1.09, 1.34, 1.30, 1.15, 0.98 and 0.69 g at 10, 15,
  20, 25, 30 and 40 m/s with its own speed controller; the small
  differences are the controllers. The car's limit is about 1.55 g, from
  the recovered friction, 1.8 front and 1.7 rear, used as a Coulomb
  coefficient; a stock 240SX manages about 0.85 g. The default steering
  reaches it only at about 1.25 times its lock, so from 10 to 30 m/s full
  input leaves 0.2 to 0.6 g in hand, which is the margin that keeps it
  out of the snap window. That window's lower edge lies between 1.25 and
  1.30 times the lock at 20 m/s, between 1.40 and 1.45 at 25 and between
  1.65 and 1.70 at 30 ("The spin matrix"). Below the
  limit the car understeers; the handbrake slides the rear and turns it
  in.
- **For #59's driver and corner speeds**: plan with the figures above, not
  with the 1.55 g limit. The review's pure-pursuit driver on its 1272 m
  street course (comment 3666, its own harness, not run here) ran wide by
  up to 25 m when it planned corners at 1.4 g, and held within 5.5 m at
  1.1 g; understeer is the safe failure. The other way out is a
  `steer_speed_reference` of 13 m/s instead of 12: it keeps the spin
  matrix at 0 of 32 (3 of 46) and gives 1.43 g at 20 m/s, 1.28 at 25 and
  1.10 at 30, for less margin to the snap window; 14 m/s spins once in the
  32.
- **The traction limit is an ideal traction control** (review note N17):
  no wheel on the ground ever gets more engine torque, driving or
  braking, than its tyre has left beside the cornering force, so the
  model cannot power-slide the 240SX or kick its tail out on a lift-off;
  only the handbrake slides the rear. A Drift event that wants power
  slides can set `traction_limit` to false, and then gets the spins the
  spin matrix records for it (7 of 32 at full input, full throttle at
  full lock from 8 to 12 m/s among them), which its own assists would
  have to handle.
- **Braking in a turn** (review note N9): full brake from 22 m/s with a
  steer of 0.2 saturates a rear tyre on 86 of 480 steps (115 in round 2)
  with at most 2.9 degrees of sideslip; half brake with full lock from
  30 m/s reaches 15.6 degrees and full brake 14.4, where round 2 spun
  (137.5 and 29.3). The extended matrix still spins nothing under braking.
  The forward centre of mass and the 450 N m rear brakes still saturate
  the rear first; a brake assist in #59 is no longer needed to keep the
  car from spinning.
- **Throttle deadband**: with the wheel rpm above idle, a throttle at or
  below about 0.30 is net engine braking at every engine speed (the
  break-even is 0.5 * B / (C + 0.5 * B) for torque C and engine braking B,
  0.30 to 0.31 over the curves). From rest, 0.10 to 0.30 creeps at a few
  km/h in first. A digital throttle is unaffected; an analogue one wants a
  mapping that gives the lower third of its travel little weight.

## How issues #59 and #60 plug in

- **#59, controls and camera.** A controller frame becomes one
  `VehicleInput` per step: throttle, brake and steer in their ranges,
  handbrake, and a shift edge. The model clamps and sanitises what it is
  given, so a controller can pass its axes straight through. The speed-
  dependent steering lock lives in the model, not in the controls. Run the
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
