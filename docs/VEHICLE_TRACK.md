# Vehicle on the track: ground, walls, out of bounds and the reset

Issue #60: the car of [`VEHICLE.md`](VEHICLE.md) on the collision of
[`TRACK_COLLISION.md`](TRACK_COLLISION.md) - the ground its wheels stand on,
its body against the track's walls and road, and the resets that put it back
when it leaves the track - with every rule labelled, per-step telemetry and
the same bits on the desktop and the console.

The code is `include/nfsu/vehicle_track.h`, `src/vehicle_track.cpp` and
`src/vehicle_track_course.cpp` (the library `nfsu_vehicle_track`, declared
for both configures in `cmake/vehicle_track_libraries.cmake` beside
`nfsu_track_contact`, which moved there so that the console compiles it), the
probe command `vehicle-track`, the tests `nfsu_vehicle_track_tests` and
`nfsu_probe_vehicle_track_guards`, and on the console
`nfsu_switch_vehicle_track`.

Two labels, as in [`VEHICLE.md`](VEHICLE.md):

- **RECOVERED**: read from the retail files or `speed.exe`, with the source.
  A rule is recovered when the instructions that apply it were read.
- **APPROXIMATION**: invented by this project, with the reason.

## Evidence base

| File | Size | SHA-256 | What it gives |
| --- | ---: | --- | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` | the rules; file offset = VA - 0x400000 for `.text`, `.rdata` and `.data`, checked against the PE section table |
| `Cars/240SX/GEOMETRY.BIN` | 4780816 | `002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619` | the car's collision hull, `240SX_CV` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` | the circuit's collision and route |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` | the track table ([`RACE_EVENTS.md`](RACE_EVENTS.md)) |
| `FrontEnd/FrontB.lzc` | 4182578 | `76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90` | the event table |

`GLOBALB.BUN` and not `GlobalB.lzc` or `GLOBALA.BUN`, which are 2025
modifications. Every file was read, never written; the installation's
manifest of every file's path, size and modification time (476 files) was
identical under `cmp` before the first read and after the last.

## What the game does

### The car's body is a convex hull

The car's collision body (`CarCollisionBody`, vtable 0x006C8798, constructor
0x00469B00) looks its shape up by the car model's hash in the list at
0x00779BD8 (0x00469C22..0x00469C4D). The list is filled by the chunk loader
0x00472B00, which takes container `0x80034020` and its `0x00039200` header,
a `0x00039201` array of 0x30-byte point records and a `0x00039202` array of
u16 (shape type 1, 0x00472BD0). For the 240SX that container is in
`Cars/240SX/GEOMETRY.BIN` at file offset 4778496 (2292 bytes): the header
names it `240SX_CV` with hash 0xC93AE898 at +0x18, and the point array,
payload at file offset 4778608, holds **37 points**, each at +0x10 of its
record. The `0x00039202` array, 198 u16, is read here as adjacency for a
support-point search; that reading is an inference, and nothing here uses
it.

The first time a car uses a shape, 0x00472E40 subtracts the car's centre of
mass (the handling's +0x110..+0x118, [`VEHICLE.md`](VEHICLE.md)) from every
point, in float, and marks the shape done (+0x3E). So the hull is held about
the centre of mass, which is the rigid body's position: 0x0046A1C0 answers
the body's position as the rigid body's +0x20, or the car's +0x60, which
0x004278F9..0x0042791D offsets by the centre of mass to get the frame origin.

In the file's own frame, the meshes' (+x forward, +y left, +z up, as the
wheel table's), the hull runs from -2.054 to 2.367 along, -0.881 to 0.874
across and 0.088 to 1.037 up: from 0.30 m above the road at rest to the
roofline. `stock_240sx_hull()` holds the 37 points as their IEEE-754 bits.
It is a small numeric table from a retail file, kept under the owner's rule
of PR #169 comment 3860: it reproduces 444 of the `0x00039201` payload's
1776 bytes - the 12 at +0x10 of each record - which hash, in record order,
to

    FCB0D6B795AEB3A1E0F34ED4CB0C2BFF3EB4560DA56AC00CFDD587226CE2A019

(`kVehicle240sxHullBytesSha256`). `nfsu_vehicle_track_tests` encodes the
table into an all-0x00 and an all-0xFF payload, takes the bytes equal in
both - exactly those 444 - and requires that hash. The whole 1776-byte
payload hashes to `9096085924151CC247C7336C23E6F4ED43FD7EFE0C740A423DEDCE8586E0015A`,
for identification only.

A car with no listed shape gets a box instead: 0x00469C79..0x00469C99 passes
0x00473CA0 the corners (-2.217, -0.971, -0.2) and (2.165, 0.971, 0.624),
about the centre of mass (shape type 3, 8 points). The 240SX has its hull,
so the box is recorded (`kVehicleFallbackBoxMin`, `kVehicleFallbackBoxMax`)
and not used.

### The track is one body per polygon

Every collision polygon is a `TrackPolygonCollisionBody` (vtable
0x006C8720): inverse mass 0 (0x00466770), a zero inverse inertia
(0x00466780), never moved (its "movable" slot, 0x00575940, answers 0) and
deaf to impulses (0x005391C0 returns at once). Its plane is held at +0x70 of
its polygon object, the normal's vertical part at +0x78. The broad phase that
pairs the car's body with the polygons near it, and how many pairs a frame
resolves, were not traced.

### One contact: separation, then impulse

For each pair the game first separates the two bodies, then resolves the
impulse.

**The separation, 0x0046B260.** The direction is the polygon's normal,
turned to face the car's centre (0x0046B070); the bodies are moved apart by
a distance that starts at 0.005 (0x0046B3FA) and grows by a step that
itself grows by 0.01 ([0x006CC79C]) until it reaches 0.1 ([0x006CC7B8]):
0.005, 0.02, 0.045, 0.08, 0.125, ... 0.5 at the tenth try and 1.55 at the
twentieth, in float. The vertical part of the move is held to +-0.15
([0x006B724C], 0x0046B453..0x0046B492). After each move the pair's witness
(0x00472830, 0x00472950) tests them again; the first move at which they no
longer intersect is kept at +0x20 of the collision record; after 20 tries
(0x0046B5A6) nothing is stored. The movable body is then moved by it
(0x0046B80F..0x0046B87B).

**The contact, 0x0046B8C0.** The point is the midpoint of the witness's two
closest points, the bodies once apart; the normal is 0x0046B070's.

**The impulse, 0x0046BAE0.** With n facing the car and vn the relative
velocity of the contact point along it:

- nothing happens unless the contact is closing, vn < 0
  (0x0046BD89..0x0046BD98);
- the restitution e is the larger of the two bodies' (0x0046BDB2..0x0046BDCB),
  and both bodies answer 0.1 (0x00466900, [0x006CC7B8]); **halved**
  ([0x006B7254] = 0.5) by 0x0046C620 when a car meets a wall polygon - its
  normal's vertical part below 0.1 - with the contact's arm, made unit, lying
  within |cos| < 0.9 ([0x006B7250]) of the car's forward axis: a side hit;
- the car's inverse mass is never above 1/1.85 t (0x0046A080,
  [0x006B727C]): **the car collides as at least 1.85 t**, where the 240SX
  weighs 1.22;
- its inverse inertia is scaled by 1 when the car's +0x38 is 1 and by 0.4
  ([0x006B7280]) otherwise (0x0046A0C0). +0x38 is a collision state: the
  constructor sets 0 (0x0042575C); 0x00465D00, which the collision record's
  update 0x00465830 calls (0x004658B0), sets 1 (0x00465EC0) and zeroes the
  car's +0x4CC (0x00465EC3) after a hit of |J| >= 30 t m/s ([0x006B72BC])
  while +0x38 is 0, when the pair is car-vs-car (both bodies of kind 0) or a
  car against a body of kind 4 (0x004663D0); and the frame update 0x0042A530
  returns it to 0 once all four wheels are down and the time since +0x4CC
  exceeds [0x006B7C00], and moves it between 0 and 2. A track polygon is
  kind 3, so **a car that meets only the track takes 0.4**; after a hard hit
  on another car it takes 1 until that time runs out;
- j = -(1 + e) vn / (1/m + n . ((I^-1 (r x n)) x r)), with r the arm from
  the centre of mass (0x0046BE0F..0x0046BE8C, 0x0046BA50);
- **j is at least 0.3 / max(1/m_a, 1/m_b)** ([0x0070009C],
  0x0046BE69..0x0046BEA5): a closing car always leaves at 0.3 m/s at least.
  This, with the separation, is what keeps a car out of a wall; there is no
  other push-out;
- friction: the larger of the bodies' coefficients, both 0.3 (0x00466910,
  [0x006CC7B4]) - **except** that against a body that cannot move, a car
  whose up axis has a vertical part above 0.7 ([0x006CCA04]) - upright -
  slides with **0.1** (0x0046C00C..0x0046C0A3). The friction impulse is
  mu j along the contact point's tangential velocity vt, reversed, scaled by
  |vt| below 1 m/s ([0x007000A8], 0x0046C0E4..0x0046C0FB) and nothing below
  1e-5 ([0x006CCB7C]);
- an upright car faster than 30 m/s ([0x006CCAC0]) against such a body
  (0x0046C223..0x0046C3D9) loses a horizontal impulse of 0.4 t m/s
  ([0x006B7258]) against its velocity, and its arm is shortened by
  |J| / (|J| + v / 1.75) ([0x006B7260]), J in t m/s, v in m/s, when
  |J| + v > 1: at speed a wall costs a little and turns the car less;
- the four-wheel lever blocks (0x0046C3FE..0x0046C52C), which for a car
  with all four wheels on the ground (+0x3CC is 4) drop the arm's part along
  its up axis, run only when the collision record's +0x10 is set
  (0x0046C3F3..0x0046C3F8, the record in edi since 0x0046C16F). The
  record's constructor 0x00465640 clears it (0x004656B7), and the one
  writer that sets it is 0x00465EB4, in the block of 0x00465D00 that sets
  +0x38 above: a car-vs-car or car-vs-kind-4 hit of 30 t m/s or more. A
  track polygon is kind 3, so **against the track the impulse turns the car
  about its full arm**, planted or not. This layer applies no lever rule.
  (Round one of this branch applied it to every planted car, labelled
  RECOVERED; review comment 4166 found the gate.)

**Applying it**, 0x0046A260 and 0x00584F60: the impulse is scaled by
(1/1.85) / (1/m) when the car is lighter than 1.85 t, so the car answers as
the collision mass would; the rigid body then clamps the linear impulse to
1000 t m/s ([0x006B5B4C]) and r x J to 10 t m^2/s ([0x006B5B50]), scales the
angular part by the same factor, 1 or 0.4, and adds both to its momenta.

**Nothing breaks.** Besides the two bodies' momenta the solver writes the
collision record (0x00465830), which accumulates the impulse and picks
effects and sounds by the surface table's thresholds (0x006EF2D0, stride
0x94, +0x80..+0x90; 0x00598260), and through the record's 0x00465D00 the
car's +0x874 (0x00465E28, on a hit of 35 t m/s, or 15 against two kind-4
models, while the car's +0x28 is 1) and its +0x38 and +0x4CC (above). No
damage or deformation was found on that path; this layer models none.

### The wheels' ground

0x00440A40 answers the height under a point from the planes of the polygons
under it - -(a x + b z - d) / c, 0x00440AA9..0x00440BBE. It first answers
the car's cached last polygon (+0x2B0 of the cache at the car's +0x540)
whenever the point lies inside it horizontally, at any height
(0x00440A59..0x00440ACB; 0x004400E0 is a two-dimensional edge test). Then,
through a cache of polygons within 8 of the point (0x00440B04), it considers
surfaces up to 1.0 above the point (0x00440B11..0x00440B1A) and answers the
first polygon the point lies strictly inside at once, not the highest
(0x00440BD6..0x00440BEB); when nothing lies under that limit it makes a
second pass with none (0x00440C18..0x00440C2C). Whether the wheels use it was not
traced. The model of issue #58 casts a segment per wheel along the body's
down axis; this layer answers that cast from the track (below).

### Out of bounds and the other reasons for a reset

A reset is a flag, the car's +0x87C, set by:

- **a RESET zone** of the track path container (0x00427F53) and **more than
  5000 from the tracked path node** (0x00426BA2), which
  [`RACE_ROUTE.md`](RACE_ROUTE.md) recovered and reports;
- **out of bounds** (0x00426B03..0x00426B59): on every 32nd frame - the
  frame counter 0x006F08A4 plus 7, modulo 32, is 0 - a car whose +0x28 is
  set (0x00426B19..0x00426B1E) and whose position has no collision polygon
  under it, from 99999.1 down (0x00440CE0 over
  0x00440A40), nor under the point one unit further along +x
  ([0x006CC7BC]), is flagged;
- the other writers of 0x00428080, which sets it: **sunk**, 0x0042A4D0,
  called every frame just before the flag is read (0x0042650F): with all
  four wheels down and a flag at [car+0x3C]+0x1C set, a surface the ground
  query 0x00440A40 answers above the car's position by more than 0.25
  ([0x006CC90C]) resets the car at once; and three not traced here (0x0044B4E3 in the AI,
  0x00479FD5 and 0x0047ACE8);
- the reset button, input event 0x99, `JOY_EVENT_RESET_CAR` (the name table
  entry at 0x006F1974); its handler was not traced.

No rule that resets an upside-down or a stuck player's car was found among
these: in retail, as far as this was traced, an overturned player's car
stays overturned until one of them fires or the player presses reset.

A `RESET_TO_POINT` zone does not set the flag: 0x00427ED0 puts the car at the
zone's point, facing its direction, at **8.0** ([0x00700D58], through
0x00427E60 and 0x00427C50, below), on the frame it is found - unless the
zone has a height and the car is at or above it (0x00427ED9..0x00427EF6).

### The reset itself

On the frame the flag is found (0x00426518), a player's car - the car's
+0x14 record is of kind 1 without +0x734 - is reset by 0x004314A0 and the
flag cleared. 0x004314A0 has two branches:

- when the car's route object (+0x2C, virtual +0x14) has a route, 0x00427F70
  puts it on the route's current point (0x004422B0 at offsets 0 and 0),
  facing that point's direction in race direction, at min(max(v, 0) *
  max(path direction . car forward, 0), 8.0);
- otherwise **the nearest track path node** - every node of the table,
  nearest first by horizontal distance in float, a node tried only when
  strictly nearer than the best so far and kept when the ground query finds
  a polygon under it (0x0043164C..0x00431781) - **facing the path's
  direction at the car's coordinate, in race direction, taken before it
  moves** (0x004317A2..0x004317B4), **at speed 0** (0x004317C1), through
  0x00427C50.

0x00427C50 updates the car's path coordinate (0x00427C7F) and sets the
position and the heading (+0x3B4); the stored speed (0x00427CBA) and each
wheel's spin (0x00427D36..0x00427D7E) take the speed itself, and the
velocity is the car's forward axis times **the speed plus 0.1**
([0x006CC7B8], 0x00427CD2..0x00427D10). A car reset at speed 0 therefore
leaves the node creeping forward at 0.1 m/s, and one sent by a
`RESET_TO_POINT` zone moves at 8.1.

Either way the car is marked as reset for 2.0 s (+0x884 the time, +0x888 =
[0x006B7B5C]; 0x004280A0 asks), which 0x004266E4 reads; what that window
gates was not traced. There is no delay before the reset beyond the frame
the flag waits, and no fade in the physics.

## Units, frames and conversions

- **Frame.** The vehicle's world frame is the track's raw slot order: x is
  `px`, y is `pz`, z is `py`, the vertical. Both are right-handed with z up
  ([`VEHICLE.md`](VEHICLE.md), [`TRACK_COLLISION.md`](TRACK_COLLISION.md)),
  so nothing is rotated.
- **Units.** One world unit is taken as one metre, the inference
  [`TRACK_COLLISION.md`](TRACK_COLLISION.md) records (the 240SX body
  measures 4.374 units against a 4.31 m car); nothing is scaled. Retail
  masses are tonnes and impulses t m/s; every retail limit above is
  multiplied by the model's `unit_scale`, 1000, as the model does.
- **Precision.** The track is float, the vehicle double. A query's two ends
  are rounded to float once; a contact's point and normal are widened back
  exactly.
- **Normals** are the contact layer's: unit length, facing the query's start.
  A wheel's cast starts above the wheel, so a road normal faces up; a hull
  probe starts at the centre of mass, so a wall's normal faces the car.
- **Road or wall.** For a wheel, a triangle is road when its normal's
  vertical part is at least `kTrackContactRoadMinUp`, 0.5, the contact
  layer's own rule, an APPROXIMATION there. The body meets every surface.
- **Headings** are the game's 16-bit units, 1/65536 of a turn from +x
  toward +y (`track_angle`), and `vehicle_track_heading(units)` is units *
  pi / 32768.

## What this layer does

### The ground under a wheel

`TrackVehicleGround::cast` is one `query_track_contact_segment` from the
model's start to start + length * direction, both rounded to float. A
**road** contact is ground: the distance is the contact point's projection
on the cast's own line from the unrounded start, held to [0, length], and
the normal is the contact's. A **wall**, nothing (**clear**), no cell
(**outside**) or a refusal is no ground: the model treats any upward-facing
hit as ground, and a wall is not one (APPROXIMATION of how retail's wheels
ignore walls, which was not traced). The surface id is 0, as the model
expects until surface types are decoded. The telemetry records what each of
the four casts met.

### The body against the track

After each model step, for each of the hull's 37 points, one segment query
from the centre of mass to the point. The centre of mass is inside the hull,
so whatever the segment meets lies between the two, and the point is on or
beyond that surface by its depth along the surface's normal. The points
that met something are grouped by polygon - retail pairs the car with each
polygon's body - and each polygon is **one contact at its deepest point**
(the lowest point index on a tie), taken in polygon order. For each:

1. **Separation**: the retail search above along the contact's normal, in
   float, with the vertical hold; "no longer intersecting" is taken as the
   move's reach along the normal covering the point's depth, less what
   earlier contacts of the step already moved the car along that normal
   (APPROXIMATION: the witness's hull-against-polygon test is not
   reproduced).
2. **Impulse**: the retail rule above, at the point's own arm - the witness
   midpoint of 0x0046B8C0 once the bodies are apart, which for a hull point
   against a polygon is that point.

### Ticks and resets

Every second step ends a 60 Hz tick, and on it, in this order:

1. a reset asked for on an earlier tick, or a sunk car found now, is
   performed (the flag is read at 0x00426518, after 0x0042A4D0);
2. the race progress of [`RACE_ROUTE.md`](RACE_ROUTE.md) steps to the car's
   position: the coordinate, off the path (asks for a reset), the zones - a
   `RESET` zone asks for a reset, a `RESET_TO_POINT` zone resets now, each
   unless its height gate says otherwise - and the laps;
3. on every 32nd tick, the out-of-bounds check (asks for a reset);
4. the two approximations, when asked for (below).

A reset asked for is performed on the next tick; the first reason asked for
is the one reported. The reset is the no-route branch of 0x004314A0: the
nearest node with ground under it, the path's direction at the car's
coordinate, speed 0, and `VehicleModel::reset` at the first surface a column
meets under the node from above the course, after which the velocity is set
to the forward axis times the speed plus 0.1, as 0x00427C50 sets it; the
model's reset gives the wheels the speed itself. A course without a route
reports the reset and cannot perform it.

### Telemetry

One record per step, sized by `kVehicleTrackTelemetryRecordBytes`: the step,
the model's own record sized by `kVehicleTelemetryRecordBytes`
(the car before its body met the track), then the state the next step starts
from - position, orientation, velocity, angular velocity, after the body's
contacts and any reset - what each wheel's cast met, the hull's hits, the
contacts and impulses, the deepest contact's polygon, kind and depth, the
separation and impulse applied, whether the step ended a tick, the
out-of-bounds check, the reset performed and the one pending, the resets so
far, and the race progress's lap, percent, zone and off-path flag. Every
double as its bits, every integer and flag as a u32, little-endian;
`VehicleTrackTelemetryHash` is their SHA-256 in step order.

## Approximations

| Rule | This project | Why |
| --- | --- | --- |
| the body's shape test | the hull's 37 points, each probed from the centre of mass; a polygon is met where a point lies beyond it | the witness (0x00472830, 0x00472950) that tests the hull against a polygon was not traced; a thin surface met between two points is not seen, which a surface a few metres across never is |
| separated means | the move's reach along the normal covers the deepest point's depth | same |
| contacts per step | one per polygon met, at its deepest point, in polygon order, once a step; the car's upright flag, speed and forward axis are taken once before the contacts | the broad phase and the order of pairs were not traced; retail recomputes those three for each pair, which differs only when a step has two or more contacts |
| a separation that fails | no move | after 20 tries retail leaves the pair's separation (+0x20) as it was and 0x0046B80F..0x0046B87B still moves the bodies by it; there is no pair record here |
| when the body is resolved | after every model step, 120 Hz | where the collision pass sits against the 60 Hz tick was not traced |
| the car's collision state | +0x38 taken as 0 for every contact: angular factor 0.4, no lever rule | only a car-vs-car or car-vs-kind-4 hit sets 1 (0x00465EC0); here there is one car and the track, whose polygons are kind 3 |
| the sunk check's flag | [car+0x3C]+0x1C taken as set | what sets it was not traced |
| the sunk check's query | the first surface a column meets from 1.0 above the centre of mass down to 0.25 above it | 0x00440A40 answers the car's cached last polygon first at any height, then the first polygon the point is strictly inside, and makes a second pass with no upper limit (above); the cache is not modelled. A car 1.5 m under its last polygon, four wheels down on a lower surface, is reset by retail and not here |
| the out-of-bounds check's gate | the car's +0x28 taken as set | as [`RACE_ROUTE.md`](RACE_ROUTE.md) takes it |
| which reset branch | the nearest node, 0x004314A0's no-route branch | the car's route object and the AI routes of chunk 0x00034121 are not decoded ([`RACE_ROUTE.md`](RACE_ROUTE.md)); whether the player's car has a route was not traced |
| the reset's height | the first surface under the node from above the course | 0x00427C50 writes a height of 0, and how the car then meets the ground was not traced |
| the grid's height | the first surface from 2 above the marker to 8 below it | retail copies the marker's height (0x00422766); on track 1003 slot 0's marker is 3 mm above the road |
| the tick's order | the reset, then the progress, then the out-of-bounds check | the wrong-way pass 0x00430730 that finds the zones runs apart from the car update; its order against it was not traced |
| the frame counter's phase | ticks counted from the start of the run | as in [`RACE_ROUTE.md`](RACE_ROUTE.md) |
| walls under a wheel | no ground | the model takes any upward-facing hit as ground |
| upside down | off unless `upside_down_steps` is set: the up axis below the horizon at under 2 m/s for that many steps asks for a reset; **on for the first lap**, 120 steps (below) | no retail rule was found; a controller can map the reset button instead |
| stuck | off unless `stuck_steps` is set: throttle of 0.5 or more at under 0.5 m/s for that many steps | same |
| the restitution's other halving | only the side-hit case of 0x0046C620 | none other was found |
| the replay's previous position | moved with the car on a reset | the route layer's wrong-way stand-in reads a position step as the heading, and a reset is not a drive |

## The proving ground

`build_vehicle_proving_ground` builds, in code, a collision image and a route
that hold one of everything the tests and the console need; no retail data.
Coordinates are raw slots, heights `py`:

- road at 10 in 32-unit squares from px 0 to 192, pz 0 to 96 - seams every
  32 - a ramp rising to 16 from px 192 to 232 (8.5 degrees), a plateau at 16
  to px 288, and a pad at px -4064..-4000, pz -3072..-3008, more than 5000
  from the route;
- walls from 4 to 24 along pz 96 (9 panels), px 0, the west half of pz 0
  (px 0 to 96), px 288, and an oblique one from (112, 8) to (160, 36); the
  south edge from px 96 to 288 is open over no cell;
- a closed route of 24 nodes anticlockwise along pz 40, px 176, pz 72 and
  px 16, one forward lap; a `RESET` zone at px 120..136, pz 80..92 and a
  `RESET_TO_POINT` zone at px 144..160, pz 80..92 that sends the car to
  (40, 56) facing +x; grid slot 0 at (32, 48) facing +x.

`build_vehicle_track_course` builds any such course from quads, a loop, zones
and a grid point; the tests build four more with it.

## The pins

Every run below is the stock 240SX, the built-in table of
[`VEHICLE.md`](VEHICLE.md). "Debug" is the default configure, as there.

Round two moved nine of the nineteen, all by dropping the four-wheel lever
rule for track contacts (above): on the proving ground `wall`,
`wall-glance`, `wall-fast`, `wall-square`, `wall-oblique` and `drop`, and
on the circuit `wall`, `wall-square` and `drop` - every run in which a
planted car's body meets the track. The other ten are unchanged.

### The proving ground

```text
build/nfsu_probe vehicle-track --proving-ground <scenario>
build-release/nfsu_probe vehicle-track --proving-ground <scenario>
```

| Scenario | What it drives |
| --- | --- |
| `rest` | at the start, the brake held for two seconds |
| `drive` | full throttle for six seconds from (64, 48) across the seams at px 96 and 128 |
| `slope` | full throttle from (152, 48) up the ramp onto the plateau |
| `wall` | full throttle from (32, 80) at 45 degrees into the north wall, then along it |
| `wall-glance` | the same at 15 degrees from (32, 88) |
| `wall-fast` | from (32, 88) at 35 m/s, 15 degrees into the north wall |
| `wall-square` | full throttle from (48, 80) straight at the north wall |
| `wall-oblique` | full throttle from (128, 48) along -y into the oblique wall |
| `drop` | dropped from five metres above the start, no input |
| `leave` | full throttle from (176, 20) along -y off the open edge: out of bounds, reset |
| `zone` | 0.6 throttle from (128, 60) into the `RESET` zone |
| `zone-point` | 0.6 throttle from (152, 60) into the `RESET_TO_POINT` zone |
| `far` | at rest on the pad more than 5000 from the route: off the path, reset |

Current record counts and hashes are owned by the proving-ground fixtures
in `src/vehicle_track_course.cpp`, `nfsu_vehicle_track_tests` and
`nfsu_probe_vehicle_track_guards`. The commands above print the measured
values and compare them with those fixtures. The historical probe label
`pinned: EQUAL to docs/VEHICLE_TRACK.md` refers to these source fixtures;
this document no longer duplicates their current values.

### Event 1's circuit

The grid, walls, a drop and leaving the track on `TRACKB1003.lzc`, from grid
slot 0 of event 1 (marker 9, (1645.6929, 297.2468), angle 0x4000; the road
under it at 55.9388), with the event's two forward laps read from the tables
as `race-route` reads them. With `R` the installation root:

```text
build/nfsu_probe vehicle-track $R/Global/GLOBALB.BUN $R/FrontEnd/FrontB.lzc $R/Tracks/TRACKB1003.lzc 1 <scenario>
build-release/nfsu_probe vehicle-track $R/Global/GLOBALB.BUN $R/FrontEnd/FrontB.lzc $R/Tracks/TRACKB1003.lzc 1 <scenario>
```

The circuit scenarios are `grid`, `drive`, `wall`, `wall-square`, `drop`
and `leave`. Their current record hashes and semantic reset/contact checks
are checked by `nfsu_vehicle_track_circuit_retail`, which executes all six
actual retail inputs with `nfsu_probe` and compares fresh telemetry with
`src/vehicle_track_course.cpp`. It also requires supported grid/drive,
start-line crossing, wall/road collision impulses without hidden resets,
an airborne-to-supported landing, and the performed bounds reset. The test
is registered with the read-only retail fixture in both default and Release
gates. `nfsu_vehicle_track_tests` only checks the circuit fixture inventory;
`nfsu_probe_vehicle_track_guards` covers missing-input refusals.

### On the console

The Switch tree builds `nfsu_switch_vehicle_track.nro`, which runs the
thirteen proving-ground scenarios and, when
`sdmc:/nfsu/Tracks/TRACKB1003.lzc` is on the card where
[`SWITCH_ASSETS.md`](SWITCH_ASSETS.md) puts it, the six circuit scenarios
(event 1's race as two forward laps, lap-skip 0, as the desktop reads the
tables; the console does not read them), and prints each hash, its run time
and `EQUAL` or `DIFFERENT`, then `result: N of M runs EQUAL to the pinned
desktop telemetry`. The Switch CTest suite, which cannot run AArch64 code,
checks the NRO's structure and the census below. It is run with

```text
nxlink -s build-switch/switch/nfsu_switch_vehicle_track.nro
```

**Not yet run on the owner's console.** Until it is, "the same bits on the
Switch" rests on the census and on the model's own console run
([`VEHICLE.md`](VEHICLE.md), "On the console"), not on this NRO's result.

## Determinism

- Every operation in the step is IEEE-754 on doubles and floats compiled
  without contraction; square roots are `vehicle_sqrt` (the model's) and
  `det_sqrt` (issue #165); nothing reads a clock or calls library
  mathematics.
  `src/vehicle_track.cpp` refuses to compile unless `FLT_EVAL_METHOD` is 0.
- **The contact layer's square roots.** `src/track_contact.cpp` called
  `std::sqrt` three times: two on floats and, at the old line 202, one on a
  double, the sweep's length, which every query on the step reaches. A
  build that calls the library for it - MinGW at `-O0` reaches libmingwex's
  x87 `fsqrt`, which rounds a double twice - need not give the correctly
  rounded root the hardware instruction of every other build gives. PR #189
  (issue #165) put the double one through `det_sqrt`, which is that
  instruction in every build (`sqrtsd`, AArch64 `fsqrt`), in the platform
  library `nfsu_det_sqrt` that the neutral `nfsu_input` and
  `nfsu_track_contact` link. This branch takes the two float ones through
  it as well, rounding its double root to float - the correctly rounded
  float root, since 53 >= 2 * 24 + 2 - so that no library root is
  referenced on the step, and so do this layer's three float roots (a hull
  point's arm, a node's distance, a loop's chords). Round one used an
  integer-only root of its own, `track_contact_sqrt`; review comment 4166
  measured it equal to `sqrtsd` on every float and 8e9 doubles, and it was
  dropped for the one root. No answer changes: every contact output of [`TRACK_COLLISION.md`](TRACK_COLLISION.md)'s
  retail acceptance, `nfsu_track_contact_tests`, and the other pins listed
  under "Nothing earlier moved" are byte for byte what `origin/main` gives.
- **The census.** `nfsu_switch_fma_census` disassembles
  every project console object, including `nfsu_vehicle_track` and
  `nfsu_track_contact`, and fails on any fused instruction. The following
  measurements preceded the project-wide rule. Recompiling each with the exact
  `build-switch` command, `-S` for `-c`:

| Object | As configured: fused / fmul | `-ffp-contract=off -fno-fast-math` removed: fused / fmul |
| --- | --- | --- |
| `vehicle_track.o` | 0 / 180 | 98 / 89 |
| `vehicle_track_course.o` | 0 / 35 | 21 / 14 |
| `track_contact.o` | 0 / 81 | 43 / 38 |

  The rest of the step and the geometry decoder now inherit the same
  project-wide contraction rule and participate in the same whole-tree
  census. The decoder previously held 20 fused instructions (one in
  `parse_polygons`, 19 in the ray query); all are now unfused. See
  [FLOATING_POINT.md](FLOATING_POINT.md) for current per-object counts.

## Cost

Timed on this machine, the best of five runs each of a one-step and a
12000-step run on the circuit (full throttle and a slight steer, 100 s,
through 588 steps of wall contact), the difference over 12000: **56.4
microseconds a step in Release** and 207.7 in Debug. That is 41 segment
queries a step - 4 wheel casts and 37 hull probes, about 1.3 microseconds
each - the model's own step, the tick's checks and the telemetry hash; at
120 steps a second, 6.8 ms of every second in Release. Round one measured
77.4, when every query also took an integer-only square root of 54 turns;
`det_sqrt` is one instruction. The console's is not
measured; the NRO prints each scenario's run time, and the model's alone
took 22.6 microseconds a step there ([`VEHICLE.md`](VEHICLE.md)).

## Tests and deliberate breaks

`nfsu_vehicle_track_tests` holds, over synthetic geometry only:

| What | Held |
| --- | --- |
| the hull's source | 37 points; the encoder writes exactly the 444 bytes at +0x10..+0x1B of each record, hashing to the retail bytes' SHA-256; a short payload refused; the hull's extremes |
| the separation | the retail sequence in float, 0.005 to 1.55; the first try that reaches a depth across a wall; nothing past the twentieth; the vertical hold at +-0.15 up and down; a slanted normal |
| the ground | a column onto road 1 down, facing up, surface 0; the distance from an unrounded start; a start on the road; the ramp's height and normal; five seams and corners; walls refused, head-on and slanted; clear; outside, west and past the open edge; seven malformed or overlong casts refused; a world that is not valid |
| road | at rest the course follows `VehicleFlatGround` within 1e-6 with and without the body, no hull hit, every wheel on road |
| seams | `test_seams` pairs the enabled track and flat model at the same spawn and controls; every step runs, every wheel finds road, no hull hit, both seams crossed; the height residual meets the unchanged test limit while absolute excursion is printed; raised and lowered valid-format slab negatives violate the same residual predicate |
| slope | on the ramp the car rides half a metre over it and reaches the plateau; the brake holds it to a creep |
| walls at 15, 45 and 90 degrees, at 35 m/s, and oblique | contacts and impulses; no hull point ends a step more than 0.12 beyond the wall; the centre of mass stays 0.7 from it; the first contact is with a wall; square on, the car ends at rest against it; at 15 and 45 degrees it ends along it, moving; at 35 m/s it slides on above 25 m/s, turning at most 2 rad/s |
| airborne and landing | over clear air; lands; no hull point 5 cm into the road; at rest three seconds later |
| friction against a wall | a car in the air sliding 0.75 m/s along the north wall: the friction impulse is 0.1 of the normal one times the slide upright and rolled 30 degrees (up.z 0.866), 0.3 times it rolled 50 degrees (0.643) |
| the clamps | a car falling at 4000 m/s, its step ending 0.1 into the road: one contact, the impulse held to 1000 t m/s exactly and its moment held, the car turning under 20 rad/s; no proving-ground scenario reaches either, and the first needs a fall of about 1850 m/s |
| out of bounds | the check runs on exactly every 32nd tick; off the open edge it finds nothing and asks; the reset on the next tick onto node 10, moving 0.1 along +x, on the road's height |
| the side probe | a car resting across a gap a metre wide, its centre of mass over the gap: the check finds the road one unit along +x, and nothing is reset; over a hole with road one unit along -x only, it is out of bounds |
| zones and off the path | `RESET` asks on entry and resets on the next tick onto node 15 facing -x; `RESET_TO_POINT` resets at once to (40, 56) at 8.1 along +x; 5000 from the route, off the path on the first tick and reset on the second |
| sunk | a deck 0.5 over the centre of mass with four wheels down resets on the first tick; 1.125 over it does not; a car a metre over the road with no wheel down, the deck 0.5 over its centre of mass, is not sunk; with the body's collision off, a deck a metre square 0.2 over the centre of mass is within the 0.25 margin and one 0.3 over it resets |
| a hole | the reset skips the nearest nodes, over a hole, for the nearest with ground, the first of two equal; the jump back against the path is not read as driving the wrong way |
| a separating contact | a nose 0.01 inside a wall on a car leaving it at 0.05 m/s is moved out and given no impulse |
| the square root | `det_sqrt` against fourteen correctly rounded roots as bits - five of them rounded up - and on +-0, infinity, NaN and negatives |
| the reset button and the approximations | a request is performed once on the next tick; upside down and stuck reset when on and never when off; the first lap's approximations are the defaults with the upside-down reset on after 120 steps |
| refusals | a course whose collision is not valid, a model that is not, an empty hull, 257 points, a NaN point, a negative approximation, a step before the start, a state or a pose that is not finite; no route: the reset reported, not performed; a triangle's degenerate half never met; the image builder's refusals |
| determinism | the record's layout from `kVehicleTrackTelemetryRecordBytes`; every proving-ground pin; the same run twice |

`nfsu_probe_vehicle_track_guards` drives the command: 15 refusals of
arguments, missing tables, every proving-ground scenario against its pin,
the lines the resets and a wall print, `--every` and `--steps`, and script
files - the file form of `rest` hashes as `rest` does, a malformed one and a
missing one exit 2.

Each break below was made alone - in `src/vehicle_track.cpp`, or for 41 in
`src/track_contact.cpp` - the probe and the two test programs rebuilt in the
Debug tree, and `nfsu_vehicle_track_tests` and
`nfsu_probe_vehicle_track_guards` run, with `nfsu_track_contact_tests` for
41; the source was then restored, checked against its SHA-256 and touched.
A run is classified by what it printed, not by its exit status alone: a unit
run is a kill only with a `test failure:` line and the `check(s) failed`
summary and a pass only with exit 0 and `all checks passed`, the guards
likewise with `guard case(s) failed` and `100% tests passed`; anything else,
a crash or a timeout, would be unknown. **All 45 were killed; none is
unknown.** Breaks 16, 18, 21, 22, 40 and 41 survived a first sweep, and the
clamp, separating-contact, side-probe, wheel-less sunk, wrong-way and
square-root tests above were written for them; 42 to 45 were added with
them. The sweep's script is not tracked.

| # | Break | Unit tests | Guards | Contact tests | Verdict | First failure |
| ---: | --- | --- | --- | --- | --- | --- |
| 01 | a wall accepted as ground | killed | passed | - | killed | "a cast into the north wall meets a wall and is refused as ground" |
| 02 | ground distance from the float start | killed | killed | - | killed | the `rest` pin |
| 03 | no separation move | killed | killed | - | killed | the `wall` pin |
| 04 | no smallest impulse | killed | killed | - | killed | the `wall` pin |
| 05 | no restitution | killed | killed | - | killed | the `wall` pin |
| 06 | no side-hit halving | killed | killed | - | killed | the `wall` pin |
| 07 | the upright test inverted | killed | killed | - | killed | the `wall` pin |
| 08 | no friction | killed | killed | - | killed | the `wall` pin |
| 09 | no friction ramp below 1 m/s | killed | killed | - | killed | "three seconds after the drop the car rests on its struts" |
| 10 | no angular factor in the solver | killed | killed | - | killed | the `wall` pin |
| 11 | no angular factor applied | killed | killed | - | killed | the `wall` pin |
| 12 | no four-wheel lever rule | killed | killed | - | killed | the `wall` pin |
| 13 | no fast-car rule | killed | killed | - | killed | the `wall-fast` pin |
| 14 | no fast-car drag | killed | killed | - | killed | the `wall-fast` pin |
| 15 | no collision mass floor | killed | killed | - | killed | the `wall` pin |
| 16 | no impulse clamp | killed | passed | - | killed | "the impulse is held to 1000 t m/s" |
| 17 | each polygon at its shallowest point | killed | killed | - | killed | the `wall` pin |
| 18 | an impulse on a separating contact | killed | passed | - | killed | "the point is moved out and no impulse is given" |
| 19 | bounds checked every 16 ticks | killed | killed | - | killed | "the bounds check runs on exactly every 32nd tick, step 18" |
| 20 | bounds phase 0 | killed | killed | - | killed | "the bounds check runs on exactly every 32nd tick, step 50" |
| 21 | no side probe one unit along +x | killed | passed | - | killed | "one unit along +x the check finds the road, and nothing is reset" |
| 22 | sunk without four wheels down | killed | passed | - | killed | "a car with no wheel down is not sunk" |
| 23 | sunk reach 2.0 | killed | passed | - | killed | "a deck 1.125 over the centre of mass is beyond the query's reach" |
| 24 | sunk reset deferred a tick | killed | passed | - | killed | "a deck 0.5 over the centre of mass with four wheels down resets on the first tick" |
| 25 | node reset heading 0 | killed | killed | - | killed | "facing -x, the path's direction on the north side" |
| 26 | node reset speed 1 | killed | killed | - | killed | "the car is put on node 10, (176, 40), moving 0.1" |
| 27 | a node without ground accepted | killed | passed | - | killed | "over the hole the car is reset onto (24, 8), the nearest node with ground" |
| 28 | reset-to-point speed 0 | killed | killed | - | killed | "the RESET_TO_POINT zone puts the car at (40, 56) moving +x at 8 plus 0.1" |
| 29 | zone height gate inverted | killed | killed | - | killed | "entering the RESET zone asks for a reset" |
| 30 | reset-to-point deferred like RESET | killed | killed | - | killed | "the RESET_TO_POINT zone puts the car at (40, 56) moving +x at 8 plus 0.1" |
| 31 | off-path ignored | killed | killed | - | killed | "5000 from the route the car is off the path and is reset onto node 0" |
| 32 | manual request ignored | killed | passed | - | killed | "a requested reset happens on the next tick, once" |
| 33 | hull not moved by the centre of mass | killed | killed | - | killed | "the underside meets the road once" |
| 34 | reset without the coordinate update | killed | killed | - | killed | the `leave` pin |
| 35 | ground normal taken as up | killed | killed | - | killed | "the ramp is ground 1 down at px 212 with its own normal" |
| 36 | separation vertical hold off | killed | passed | - | killed | "up, 0.14 is reached by the move held to 0.15" |
| 37 | separation first step 0.01 | killed | killed | - | killed | "across a wall, 0.001 needs the first try that reaches it" |
| 38 | stuck never | killed | passed | - | killed | "held against the wall at full throttle, the car is reset as stuck" |
| 39 | upside down never | killed | passed | - | killed | "upside down for a second, the car is reset" |
| 40 | previous position kept across a reset | killed | passed | - | killed | "the reset is not read as a drive the wrong way" |
| 41 | contact sqrt truncated, not rounded | killed | passed | passed | killed | "track_contact_sqrt(2) is correctly rounded" |
| 42 | no angular clamp | killed | passed | - | killed | "its moment is held", the car turning at 222 rad/s |
| 43 | no lead velocity after a node reset | killed | killed | - | killed | "the car is put on node 10, (176, 40), moving 0.1" |
| 44 | no lead velocity after a reset to a point | killed | killed | - | killed | "the RESET_TO_POINT zone puts the car at (40, 56) moving +x at 8 plus 0.1" |
| 45 | lead of 0 instead of 0.1 | killed | killed | - | killed | "the car is put on node 10, (176, 40), moving 0.1" |

That sweep ran on round one's code. Round two (review comment 4166)
dropped the four-wheel lever rule, which break 12 had removed, and added the
friction, east-only side probe and sunk-margin tests above for four values
the reviewer's own sweep found unpinned. The same harness, over the round
two code:

| # | Break | Unit tests | Guards | Verdict | First failure |
| --- | --- | --- | --- | --- | --- |
| L1 | the lever rule put back for a planted car's track contacts | killed | killed | killed | the `wall` pin: its hash is round one's exactly |
| B01 | side probe at x - 1 | killed | passed | killed | "the check does not look west: the car is out of bounds" |
| I05 | friction ramp below 0.5 m/s | killed | killed | killed | "upright, the friction is 0.1 of the normal impulse scaled by a slide under 1 m/s" |
| I14 | upright threshold 0.9 | killed | passed | killed | "rolled 30 degrees, up.z 0.866 is above 0.7 and the car still slides with 0.1" |
| R05 | sunk column down to z | killed | passed | killed | "a deck 0.2 over the centre of mass is within the margin" |

The reviewer's other survivors are left as they are: they are equivalent in
practice or test this project's own approximations.

## What is not known

- The witness's shape test and closest points (0x00472830, 0x00472950),
  the broad phase, and the order and number of pairs a frame resolves.
- Where the collision pass runs against the physics step and the 60 Hz tick.
- What +0x38's value 2 means (0x0042A530), and what sets the flag at
  [car+0x3C]+0x1C.
- Whether the wheels use 0x00440A40, and where the struts' forces are applied
  ([`VEHICLE.md`](VEHICLE.md)).
- The player's route object: whether it has a route, which point is current,
  and the AI routes of chunk 0x00034121 that the other reset branch uses.
- What the 2 s window after a reset gates, the reset button's handler, and
  the three reset requests not traced (0x0044B4E3, 0x00479FD5, 0x0047ACE8).
- Surface types, which the collision records carry and this project does not
  decode, and with them any per-surface restitution or friction.

## How the desktop lap uses it

`load_vehicle_track_course` with event 1's laps, direction and lap-skip -
from the tables, as `vehicle-track` reads them - gives the collision, the
route and grid slot 0; a `VehicleTrackSession` over it, `start` at
`course.start`, and one `step` per 1/120 s with the controller's
`VehicleInput` (#59), two a 60 Hz frame. The telemetry's position and
orientation place the car and the camera; `progress()` holds the lap
counter and the percent the timing of #62 reads, and `events()` the tick's
crossings, laps and finish; `request_reset()` is the reset button. Resets
happen by themselves off the track, in reset zones and far from the route.

**The first lap runs with the upside-down reset on.** Nothing in retail
rights an overturned player's car (above), and nothing in this layer
did: review comment 4166 drove a car at 35 m/s round event 1 with round
one's rules and found it leaving the ground at the crest near (1351, 484)
and ending on its roof; and the first lap (#159) may come before a reset
button is mapped (#59). So the session is built with
`vehicle_track_first_lap_approximations()`: the defaults, with the car
reset onto the route after 120 steps (one second) with its up axis below
the horizon at under 2 m/s. It is an APPROXIMATION of the reset button, not
a retail rule, and with a button mapped it can be turned off. The probe and
the pins keep the defaults, in which it is off.
The step costs about 56 microseconds on the desktop. The console lap needs
the same TRACKB file on the card and the deko3d renderer.

## Nothing earlier moved

Built from `origin/main` at 2f5e747 with `git archive`, default and
Release, against this branch's default, Debug and Release builds: the
standard output and exit status of all 17 commands of
[`TRACK_COLLISION.md`](TRACK_COLLISION.md)'s retail acceptance, the nine
`vehicle-sim` runs of [`VEHICLE.md`](VEHICLE.md) (the eight built-in
scripts and `mixed` over the retail handling), the five `race-route` runs of
[`RACE_ROUTE.md`](RACE_ROUTE.md) and the four `race-timing` runs of
[`RACE_TIMING.md`](RACE_TIMING.md) are byte for byte the same in all five;
`mixed` still hashes to `B9EE418C...CF8E3`, the race-route listing to
`3A764B95...3F00` and the race-timing runs to `B3ECE3B4...`,
`CF8D4752...`, `DE68AE9C...` and `EFCCB2CC...`. Round one made the same
comparison against f892678 and after merging 174af93.

## Read-only

The retail installation was read only. A manifest of every file's path,
size and modification time (`find . -type f -printf '%P\t%s\t%T@\n' |
LC_ALL=C sort`, 476 files) was taken before the first read and after the
last, and the two are identical under `cmp`. No retail file or asset is in
the repository; the one table from a retail file is the hull above, with its
source and hash.
