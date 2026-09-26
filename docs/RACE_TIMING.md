# Race timing: the countdown, the race clock, laps and the finish

Clean-room notes on how Need for Speed: Underground counts a race down,
starts it, keeps its clock, times its splits and laps, finishes it, gives
its result, restarts it and leaves it. Everything here comes from reading
a user-provided retail installation and its executable read-only. No game
data is stored in this repository: this note holds addresses, constants,
hashes and field meanings.

The answer first, for issue #62 and the first-lap target of #159 (event 1,
a two-lap forward circuit on track 1003,
[`RACE_ROUTE.md`](RACE_ROUTE.md)):

- **The race clock is the world timer, in ticks of 1/4000 s.** Every race
  time is a whole number of ticks of `0x0073AD34`. The timer advances once
  a frame, after the frame's updates, by the frame's length rounded to a
  tick - and the frame's length is first rounded to whole 3600ths of a
  second - so **a 60 Hz frame adds 67 ticks, not 66.67**: at 60 frames a
  second the retail clock runs 0.5 percent fast. A race's world timer
  starts at 4000 ticks when the race is built, and again after a restart.
- **The countdown is three steps of 0.9 s.** The race status counts 3, 2
  and 1 from the moment the countdown starts, and the race **goes on the
  first frame whose world time is at least 3 x 0.9 s after it** - at 67
  ticks a frame, 162 frames. "Go" stays on the HUD for 0.9 s more.
- **A car first moves on the frame after go - an approximation.** Every
  stage but racing holds each driver (the player's driver class is not
  traced), and go is set in the race status update, after the frame's
  update of the car's path coordinate that the lap update reads - so the
  go frame's lap update cannot see a move the release caused. This model
  has the lap update see the car move first on the frame after go. Were
  the car's position integrated between that coordinate update and the
  status update, it would be one frame later, and event 1's laps would read
  1:50.65 and 1:50.53 (3:41.18); were the player's car not held on the go
  frame, one frame earlier: 1:50.62 and 1:50.53 (3:41.15).
- **Times are rounded to hundredths.** The race start, the race time and
  every crossing are rounded to the nearest 1/100 s, half up, by
  `0x00586690` in float - which returns 40 ticks a hundredth, and for a
  few hundredths 41.
- **A crossing is dated back** from the frame that detected it by the
  distance the car is past the line over its speed, before rounding.
- **Lap 1 is timed from go, not from the start line.** The grid is behind
  the line, and the first crossing of it after go raises no checkpoint, so
  it times nothing. Every later forward crossing of a split or the line
  that raises the car's checkpoint is timed from the lap start, and a
  timed crossing of the line ends the lap and starts the next.
- **A car finishes when its highest lap reaches the race's laps**, in the
  same frame as that crossing. Its race time is the crossing less the race
  start - exactly the sum of its lap times - and its place is the first
  one no car holds yet.
- **Nothing but quitting ends a single-player race without a result.** A
  restart builds the race again; the wrong way and a reset leave the clock
  running.

`nfsu::step_race_timing` (`include/nfsu/race_timing.h`) runs all of it from
fixed steps and `nfsu::step_race_progress`'s events, and `nfsu_probe
race-timing` replays event 1 through it. For event 1's built-in forward
drive the race goes at step 162, lap 1 is `1:50.63` and lap 2 `1:50.54`,
and the race time is `3:41.17`.

## Reference samples

| File | Size | SHA-256 |
| --- | ---: | --- |
| `speed.exe` | 3178496 | `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7` |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `FrontEnd/FrontB.lzc` | 4182578 | `76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90` |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` |

The same four files [`RACE_ROUTE.md`](RACE_ROUTE.md) reads, for the same
reasons. Executable addresses are `speed.exe` virtual addresses;
`file_offset = VA - 0x400000` for `.text` (VA 0x401000, file 0x1000),
`.rdata` (0x697000, 0x297000) and `.data` (0x6D4000, 0x2D4000), read from
the section table.

## The clock

### The world timer

`0x0073AD34` is an `int` of ticks, and every reader turns it into seconds
with `[0x006CCA38]` = 0.00025 (`0x3983126F`) and back with `[0x006CC9FC]` =
4000.0. Once a frame, at the end of the main loop (0x00447A1A, after the
frame's updates), 0x00585E00 adds the frame's world time to it:

```text
00585E01  fld   dword ptr [0x73457c]      ; the frame's world dt, seconds
00585E11  fcomp dword ptr [0x6cc7a4]      ; 0.0: a paused frame adds nothing
00585E6C  fld   dword ptr [0x73457c]
00585E72  fmul  dword ptr [0x6cc9fc]      ; 4000.0
00585E78  fadd  dword ptr [0x6cc7dc]      ; 0.5
00585E7E  call  0x674898                  ; _ftol, truncating
00585E83  mov   ecx, dword ptr [0x73ad34]
00585E89  add   ecx, eax
00585E90  mov   dword ptr [0x73ad34], ecx
```

and counts the frame (`0x006F08A4`, 0x00585E8B..0x00585EAD). No remainder is
carried here: each frame's world time is rounded to ticks on its own.

The frame's world time comes from its real time, which does carry a
remainder. 0x00585C54..0x00585CD1 take the measured frame plus the
remainder R kept at `0x00734570`, raise it to at least the display period
(`[0x006CC7B0]` = 1/60, `0x3C888889`, with `0x006F0880` set,
`[0x006CC7D0]` = 1/50 without) and lower it to at most `0x006F0894` - by
inference the frame's count of vertical blanks - times the period:
`dt = min(blanks * period, max(R + measured, period))`. 0x00585CD5..
0x00585CE8 round it to whole 3600ths of a second, `(int)(dt * [0x006CC7E0]
+ 0.5)` with `[0x006CC7E0]` = 3600.0, and
0x00585CEE..0x00585D1A keep `R = dt - (int)(units * [0x006CCDE8] + 0.5) *
0.00025`, with `[0x006CCDE8]` = 1.1111112; 0x00585D52..0x00585D63 store the
units at `0x00734568` and the seconds, units times `[0x006CC7E4]` = 1/3600
(`0x3991A2B4`), at `0x0073AD38`. The race path of the main loop hands that
through the frame pacer 0x00420110 (which multiplies by a time scale at
+0x2E0, 1.0 unless slowed, 0x004202EC..0x004202F1) and 0x0043A8A0 (which
multiplies by 1.0, `[0x006B7994]`, off line) into `0x0073457C`
(0x00447924..0x00447942). So a frame of the period itself, 1/60 s:

```text
units   = (int)(0.016666668 * 3600 + 0.5)   = 60
seconds = 60 * 0.00027777778 (float)         = 0.016666668
ticks   = (int)(0.016666668 * 4000 + 0.5)    = (int)67.16667 = 67
```

67 ticks is 16.75 ms. `nfsu::race_ticks_per_frame` is that arithmetic in
float; it gives 67 at 60 frames a second, 80 at 50, 53 at 75, 40 at 100,
33 at 120, 28 at 144, 133 at 30, and 308 at 13 (where the `+ 0.5` of the
units decides). At 60 Hz the remainder settles at `1/60 - 67/4000` =
-1/12000 s, so `R + measured` is just below the period and the floor makes
every frame exactly 1/60 s. That a 60 Hz frame is one blank, and so one
period, is inference from the display rate.

The timer is set to 0xFA0, 4000 ticks, when a race's world is built
(0x0041F7DA, in 0x0041F6E0, which the race set-up calls at 0x00421FB9) and
when it is torn down (0x0041FB22, in 0x0041F9B0, which the teardown
0x00422130 calls at 0x004221FF).

### Rounding to hundredths: 0x00586690

Seven calls round a race time to hundredths: the race start (0x00434C90),
each car's lap start at go (0x00434C1B), the race time (0x004355D9), a
crossing (0x004339C4), a finish under a time limit (0x00435151) and the
estimate 0x00433E20 makes of an unfinished car's time (0x00433F42,
0x00433FCD). Each pushes 0.01 (`0x3C23D70A`), calls 0x00586690 with the
ticks, and turns the seconds it returns back into ticks with `fmul
[0x006CC9FC]`, `fadd [0x006CC7DC]`, `_ftol`; an eighth, in the frontend
(0x004C5D3E), pushes 1.0 and rounds to whole seconds. 0x00586690:

```text
x = ticks * 0.00025 / 0.01             ; 0x00586693..0x0058669F, stored as float
f = trunc(x), less 1 if above x         ; 0x004387E0, 0x005866AD..0x005866C5: floor
if x - f >= 0.5: f += 1                 ; 0x005866C9..0x005866E4: half up
y = f * 0.01 * 4000                     ; 0x005866EA..0x005866F5, stored as float
c = trunc(y), plus 1 if below y         ; 0x00438800: ceiling
return c * 0.00025                      ; 0x005866FD
```

The ceiling is what makes a few hundredths 41 ticks rather than 40: for
1619 hundredths, `1619 * 0.01 * 4000` in float lands just above 64760, and
the ceiling takes it to 64761. Over the first minute 160 tick values
(four hundredths) round that way. `nfsu::race_quantize_ticks` is the routine
and its callers' conversion in float in this order; the integer is
converted to seconds as `fild` then `fmul` do - exactly, then rounded once
- which a double product reproduces for any tick count below 2^29.

The Direct3D device is created without `D3DCREATE_FPU_PRESERVE`, so the
x87 runs at 24-bit precision and every one of these operations rounds to
float ([`RACE_ROUTE.md`](RACE_ROUTE.md) gives the evidence and says it is
inference); the library is compiled without contraction so that each is
one float operation on every platform.

## Stages

The race status (0x00434270, kept at `0x0073619C`) holds a stage at +0x00
the executable names in the table at 0x00700A88: `RACE_STAGE_NONE`,
`RACE_STAGE_FLYBY`, `RACE_STAGE_PRERACE`, `RACE_STAGE_COUNTDOWN` and
`RACE_STAGE_RACING`, 0 to 4. 0x00434B50 sets it.

- **Staging.** The race set-up 0x00421F00 builds the status (0x00421F8C) and
  resets it at stage none (0x00434590 from 0x00422047, which also
  initialises every car's lap record through 0x00434A80 and 0x004333C0).
  It chooses the stage to go to next and the coordinator's state with it
  (0x00421FAD..0x00422036): the countdown, in `RCSTATE_RACE_IN_PROGRESS`,
  when `0x0073465C` and the coordinator's +0x44 are both clear; the flyby,
  in `RCSTATE_CAMERA_FLYBY`, otherwise; racing at once only in the two demo
  states, `RCSTATE_AI_DEMO_MODE` for `0x0078A308` and
  `RCSTATE_REPLAY_DEMO_MODE` for `0x0078A309`. The flyby and the prerace are
  the camera's; the race coordinator moves on to the countdown with
  0x00434B50(3) (0x004233A5, and 0x0042347B from prerace).
- **Countdown.** 0x00434B50(3) takes the world time as the countdown's start
  (`[status + 8]`, 0x00434C61..0x00434C73).
- **Racing.** Only the countdown's end reaches it (below).

For every stage but racing, 0x00434B50 calls each racing driver's vtable
+0x1C, and for racing +0x20 (0x00434BB0..0x00434C00). In the AI driver's
class (vtable 0x006C8AD0) +0x1C is 0x004607F0, which sets a hold flag at
+0x278, and +0x20 is 0x00461510, which clears it; the player's driver class
was not traced.

When does a released car first move, as the lap update sees it? The main
loop sets the frame's time (0x00447942) and calls the world's per-frame
update 0x00420B40 (0x00447969), whose last call is the race status update
0x00435570 (0x00421155); the timer advances after it (0x00447A1A). Inside
0x00420B40, the loop 0x00420E2B..0x00420E4B calls 0x00426930 for every car
of the world, and that updates the car's path coordinate from its position
+0x60 (0x00426A9B..0x00426AFE, calling 0x00443A10) - the coordinate whose
fraction 0x004338C0 reads. So the lap update of a frame sees the car where
it stood at that call, and the release, inside 0x00435570, comes after it:
the go frame's lap update cannot see a move the release caused. Where the
position itself is integrated was not traced. Just before that loop the
world update calls 0x00420310, which calls every active object of the list
at `0x00779C70` through its vtable +0x28 with the frame's time, and
0x00420380, which tests pairs of objects for overlap (0x004203F8..
0x0042042A); by their shape these are the simulation step, which would make
the lap update see the first move on the frame after go. That is what this
model assumes - an approximation, labelled below. If the position were
integrated after the coordinate update and before 0x00435570 (among
0x00420E51..0x00421146, 0x00421012 among them), the first move would be
seen a frame later; if the player's car were not held on the go frame, a
frame earlier. Replayed through the same route events, event 1's forward
drive gives:

| First move seen | Lap 1 | Lap 2 | Race time |
| --- | ---: | ---: | ---: |
| one step earlier (step 162) | 1:50.62 (442480) | 1:50.53 (442120) | 3:41.15 (884600) |
| this model (step 163) | 1:50.63 (442520) | 1:50.54 (442160) | 3:41.17 (884680) |
| one step later (step 164) | 1:50.65 (442600) | 1:50.53 (442120) | 3:41.18 (884720) |

### The countdown: 0x00434D10

The countdown step T is `[status + 0x0C]`: `[0x006B79E0]` = 0.9
(`0x3F666666`), or `[0x006B79E4]` = 1.0 when `0x0078A368` is set
(0x00434316..0x00434327). 0x00434D10 answers what the countdown shows:

```text
if the race has gone ([status + 4]):  4 ("go") while (now - go) * 0.00025 <= T, else 0
else if it counts ([status + 8]):     s = (now - start) * 0.00025
                                       3 if s < T, 2 if s < T + T, 1 if s < T * 3.0, else 4
else                                   0
```

(0x00434D1A..0x00434D45 and 0x00434D4B..0x00434DBA, `[0x006CC7AC]` = 3.0.)
In float, `T * 3.0` for T = 0.9 is 2.6999998 (the exact product is a tie,
rounded to even). The race status update, first thing every frame
(0x00435570..0x004355B5), turns the stage to racing when the stage is the
countdown and 0x00434D10 answers 4. So for T = 0.9 the countdown shows 3
from 0 ticks, 2 from 3600, 1 from 7200 and goes at 10800 (2.7000000 s in
float); at 67 ticks a frame, on frames 0, 54, 108 and 162, and "go" is
gone from frame 216. For T = 1.0 the edges are 4000, 8000 and 12000
ticks, and "go" stays through exactly 4000 ticks (1.0 in float) and is gone
at 4001.

What `0x0078A368` is was not traced to a name. It has two writers:
0x004B5122 clears it, and 0x004B64DC copies the option `0x0073466C`, whose
one writer, 0x00568AA6, stores a value read from a bit stream; a time limit
of 1200000 ticks (300 s) is stored at `0x0078A310` when it is set
(0x004B64FD..0x004B6501). The career race start 0x004ABC20..0x004ABC80
writes neither. Which of the two setup paths a career race takes was not
traced; this project takes the flag as clear: T = 0.9.

### Go: 0x00434B50(4)

```text
00434C7A  mov  eax, [0x73ad34]; mov [status + 0x10], eax      ; the race start
00434C89  push 0.01; call 0x586690; ... mov [status + 0x10], eax   ; rounded
00434CAC  mov  [status + 4], now                              ; when it went
00434CB9  mov  [status + 8], 0                                ; the countdown is over
00434C0A  mov  [car + 0x24], now, rounded (0x00434C14..0x00434C31)  ; each car's lap start
00434C33  mov  word ptr [car + 6], 0                          ; highest checkpoint 0
```

and, for a point-to-point race (`0x0078A318`), the lap counter and the
checkpoint 0 (0x00434C37..0x00434C42). The car's lap start is the same
rounded time as the race start.

### The race time

While racing, every frame (0x004355BA..0x004355EF):

```text
[status + 0x14] = round(now - [status + 0x10])
```

It runs on after the player finishes; it is the race's clock, not the
player's time.

## Splits and laps: 0x004338C0

Every frame, for every car not yet finished (0x00435600..0x0043563C, the
`cmp word ptr [edi + 0x10], 0` at 0x00435603), 0x004338C0 walks the
boundaries and asks 0x00433650 whether each was crossed; the rule is
[`RACE_ROUTE.md`](RACE_ROUTE.md)'s. On a forward crossing it computes the
checkpoint `5 * lap + boundary` (after the finish line has added its lap),
and **only when that is above the car's highest checkpoint** does it time
anything (0x0043398B..0x004339A3):

```text
004339A7  mov  eax, [esp + 0x20]          ; the backdate 0x00433650 returned
004339AF  mov  edx, [0x73ad34]
004339B5  sub  edx, eax                   ; now - backdate
004339B7  push 0.01; call 0x586690 ...    ; rounded: the crossing time
004339EF  mov  ecx, [edi + 0x24]          ; the lap start
004339FE  [edi + 0x38 + 4b + 0x14] = [edi + 0x38 + 4b]         ; the previous split
00433A09  [edi + 0x38 + 4b] = crossing - lap start           ; the split
00433A11  [edi + 0x38 + 4b + 0x3C] = [edi + 0x38 + 4b + 0x28]  ; the previous best
00433A17  [edi + 0x38 + 4b + 0x28] = min(best, split)          ; the best
00433A1F  if b is the finish line and 0 <= lap < 11:
00433A3C      [edi + 0x88 + 4 lap] = split                     ; the lap's time
00433A4E      [edi + 0xB4 + 4 lap] = split (+ [edi + 0xB0 + 4 lap] for lap > 0, 0x00433A55..0x00433A66)
00433A6D  [edi + 0x24 + 4b] = crossing    ; the finish line's is the next lap's start
```

The split, lap and total are skipped when the lap start is empty (0 or
0x7FFFFFFF, 0x004339EF..0x004339FC), but the crossing is still stored. The
arrays are 5 boundaries and 11 laps wide (0x00433400..0x00433412,
0x004343BE..0x004343D0); the bests start at 0x7FFFFFFF (0x00433408).

Not modelled: when the object at `0x007345D0` holds a non-zero first word
(0x004339DA..0x004339E9), 0x004338C0 times none of that and, for a race
type `0x0078A30C` of 2, stores only the line's time since the lap start and
the line's crossing (0x00433A72..0x00433A9F). By inference that object is
replay playback: the race set-up creates it (0x00421F73) and the frame
pacer 0x00420110 reads a stream through it when its first word is set. A
career race, as far as traced, does not take that branch.

### The backdate: 0x00433650

Beside its verdict, 0x00433650 returns how long ago the car crossed:

```text
past     = wrap(percent - boundary)            ; 0x00433656..0x00433697
distance = (path length * past) * 0.01         ; 0x004336D5 `fld [0x7372b0]`, 0x004336DD, 0x004336E5
seconds  = distance / speed, if speed > 1.0    ; 0x00433761..0x0043377A, the car's +0x3D0
ticks    = (int)(seconds * 4000 + 0.5)         ; 0x00433780..0x00433792
```

and 0 when the car's speed is 1 or less. The crossing is `now - ticks`,
then rounded.

### Why lap 1 runs from go

0x004333C0 starts a car with checkpoint -1 and highest checkpoint -1; go
sets the highest to 0. The grid (`ForwardStartingPosition`) is behind the
start line, so the first forward crossing of the line after go takes the
lap counter from -1 to 0 and makes the checkpoint `5 * 0 + 0` = 0, which is
not above 0: nothing is timed and the lap start stays at go. Lap 1 is
therefore the time from go to the second crossing of the line, and it
includes the short run from the grid to the line. Every lap after it runs
from one timed crossing of the line to the next. Backing over the line and
crossing it again raises no checkpoint either, so nothing is timed twice.

A split skipped beyond the crossing window has no time on that lap
(nothing crossed it) and the lap still counts; a line skipped that way
loses the lap, as [`RACE_ROUTE.md`](RACE_ROUTE.md) describes.

## The finish and the result

After every car's lap update the race status finishes, through 0x004350D0
with status 1, every car not yet finished whose highest lap equals the
race's laps (0x00435A17..0x00435A5D; not for a race of 0 laps). 0x004350D0
stores the status at +0x10, then, off line (0x0043536C..0x004353B5), the
first place no car holds yet at +0x0C (0x00435BD0) and the race time at
+0x110:

```text
[car + 0x110] = [car + 0x24 + 4 * finish] - [status + 0x10]   ; the last crossing less the race start
```

Both are rounded times, so the race time is a whole number of rounded
ticks and equals the sum of the lap times exactly. The executable names
the statuses in the table at 0x00700A5C: `RACING_CAR_NOT_FINISHED`,
`RACING_CAR_NORMAL_COMPLETION`, `RACING_CAR_ARRESTED`,
`RACING_CAR_KNOCKED_OUT`, `RACING_CAR_BE_THE_COP_WON`,
`RACING_CAR_BE_THE_COP_LOST`. The code reaches 0x004350D0 with 3 and 5 to 9
as well, from paths a career circuit does not take: a race type
`0x0078A30C` of 3 (0x004356DC..0x0043577A), the one-lap flags `0x0078A344`
and `0x007361A8` (0x0043577C..0x0043585D), a time limit `0x0078A310`
(0x0043585F..0x004358F6) and the online game (0x00435963..0x00435A15).

For each human racer the status keeps the best lap any has driven at +0x18
(0x0043560F..0x00435630). The coordinator's per-frame message asks
0x00435B30 whether every player's +0x58 is set (0x00423629..0x00423637) and
leaves the race after a delay counted in frames (0x00423639..0x00423658);
that +0x58 marks a player's finish is inference.

## Restart, quit, the wrong way and resets

The race coordinator's states are named by the table at 0x00700DE8
(`RCSTATE_NONE`, `RCSTATE_LOADING`, `RCSTATE_CAMERA_FLYBY`,
`RCSTATE_PRERACE`, `RCSTATE_RACE_IN_PROGRESS`, ..., `RCSTATE_PAUSE`,
`RCSTATE_POSTRACE`) and its messages by the table after it (`RC_MSG_NONE`,
`RC_ACTIVATE`, ..., `RC_RESTART_RACE` = 8, `RC_NEXT_RACE` = 9,
`RC_QUIT_RACE` = 10, ...).

- **Restart.** In the handler whose switch is at 0x0042418A (the pause
  state's, by its cases), message 8 moves the coordinator to state none (0x00421AE0 with 0)
  and queues message 8 again (0x004243C1..0x004243E3); state none's
  handler 0x00422BB0 answers it with the teardown 0x00422130 - which deletes
  the race status (0x004221C0..0x004221F4) and the world, resetting the
  timer to 4000 (0x0041FB22) - and the set-up 0x00421F00, which builds both
  again (0x00422BD4..0x00422BDA). A restart is a new race from stage none,
  with fresh lap records and the world timer at 4000, and so times exactly
  as the first attempt did.
- **Quit.** Message 10 leaves the same way (0x0042442E..0x0042443D) and state
  none answers it with 0x00495E90 and the frontend's entry point 0x00449A10
  (0x00422BE8..0x00422C07): no result.
- **The wrong way** changes nothing in a single-player race. The wrong-way
  flag at +0x4AC (0x00430730) is read by the HUD and by several style and
  speech gates; the HUD's `WrongWay_CountDown` (string 0x006C71E0) shows a
  number of seconds only while the online manager is active (0x004A3C88..
  0x004A3C94: 0x0043A2E0 on `0x0077B240`, the object whose update is
  `OnlineManager::Update`, 0x0043A360).
- **A reset** changes nothing in the race status either: 0x00427ED0
  (0x00427ED9..0x00427F62) either places the car at a zone's point
  (0x00427E60) or sets its +0x87C, and reads and writes no time. The clock
  runs on, and the car's lap record is whatever its new position makes of
  it.

## The HUD

What the on-screen timers read, for the milestone's lap timer:

- **The lap timer** (`CurrentLapTime`, 0x004A19B1..0x004A1A38): when the lap
  start `[car + 0x24 + 4 * start]` is set, the world time since it,
  **unrounded**, read from `0x0073AD34` itself (0x004A19DE, 0x004A19F5);
  when the car has just completed a lap (+0x0D), that lap's time
  (`[car + 0x38 + 4 * finish]`) instead, held while it is at most 3.0 s old
  (`[0x006CC7AC]`). Without a lap start, a text in place of a time
  (0x004A1A58..0x004A1A60, string hash 0x61C9B7A2).
- **The best lap** (`BestLapTime`, 0x004A1A91..0x004A1AE0):
  `[car + 0x60 + 4 * finish]`; "--:--:--" (0x006C72DC) for a best of 0
  (0x004A1AB0..0x004A1ABD), and any other value through 0x00586580 - the
  empty 0x7FFFFFFF, which a car holds until its first timed lap, included.
- **The format** (0x00586580, 0x005864F0, 0x00586090): whole seconds
  `ticks / 4000`, milliseconds `(ticks % 4000) * 1000 / 4000` and
  hundredths `milliseconds / 10`, all truncated. 0x00586580 hands 0 and
  0x7FFFFFFF alike to 0x00586090 as four zero fields (0x0058658C..
  0x005865A9). 0x00586090 picks a template (0x006B9570..0x006B9648) by
  language (0x006EE6F0), by its flags (the HUD passes 4, 0x004A1A33) and by
  whether the hours, the minutes or the seconds are the first non-zero
  field, and writes the digits over every placeholder of it (0x005860A8..
  0x00586461) - so a zero time and the empty time both print as a zero time
  (" 0:00.00" in the variant with a point), and the HUD's only dashes are
  the ones it chooses itself above. This project prints the digits,
  `M:SS.hh`, without the widths, the separators or a leading space.
- **The countdown**: 0x00434D10 above.

`nfsu::race_timing_hud` answers the same questions from the state after a
step: the countdown value, the race time, the lap timer (running or held),
the best lap, the laps completed and the race's, the wrong-way flag and the
finish. It reads the world time of the step it follows; when retail's HUD
runs relative to the timer's advance (0x00585E00) was not traced, so the
running lap timer and the 3 s hold may differ from retail's by one frame
(labelled below). The first-lap build reads it once a frame, after running the steps
the step clock hands out, and formats each time with
`nfsu::format_race_ticks`.

## What this project implements

`include/nfsu/race_timing.h` and `src/race_timing.cpp`, the library
`nfsu_race_timing`, declared for both configures in
`cmake/race_libraries.cmake`. It links `nfsu_race_route`, reads no file and
no clock, calls no transcendental function or square root, and is
classified neutral for the platform-neutrality gate. Its float arithmetic is
compiled with `-ffp-contract=off -fno-fast-math`.

- `race_quantize_ticks`, `race_ticks_per_frame` and `format_race_ticks` are
  0x00586690 with its callers, the frame rule, and the HUD's split.
- `start_race_timing` builds a race at stage none with the world timer at
  4000; `set_race_stage` moves it forward through flyby, prerace and the
  countdown (the countdown's start is the world time of the next step);
  `race_countdown_value` is 0x00434D10; `race_car_held` is true for every
  stage but racing.
- `step_race_timing` is one frame: go, the race time, each unfinished
  racer's progress events of that step - the lap counter, the checkpoint,
  the timed and untimed crossings with their backdates, splits, laps and
  totals, the lap timer's hold, the wrong-way flag and its duration, the
  reset zones and the off-path flag - the best lap, and the finish with the
  place and the race time; then the world timer advances. Each racer brings
  its events from `nfsu::step_race_progress` and its speed.
- `restart_race_timing` and `quit_race_timing` are the two coordinator
  messages; `race_timing_results` lists the finished racers by place and the
  others after them; `race_timing_hud` is the HUD above.
- `RaceStepClock` turns the platform's clock into steps: each call with
  `TimingService::now_us()` hands out the steps due since the first call
  (step k is due k / 60 s after it), at most a cap at a time, dropping a
  backlog beyond that. It is integer arithmetic only, so the fake clock of
  `include/nfsu/platform_fakes.h` drives it to the same steps everywhere.

Refused, with a reason and the state untouched: no racer or more than 64,
more humans than racers, a step rate below 1, a step of fewer than 1 or
more than 4000 ticks, a countdown step that is not finite and positive, a
world start outside 1 .. 2^29, a route without boundaries or with more
than five, a start line that is not boundary 0, a finish line outside them,
a point-to-point route, a path length or boundary that is not finite; and,
for a step, inputs naming a boundary the route does not hold, a lap outside
a signed byte, a percent that is not finite, a speed that is not finite or
is negative. A step that would take the world timer past 2^29 ticks ends
the race (`clock-limit`).

`nfsu_probe race-timing <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>
(--path <FILE> | --drive <forward|backward> [--skip <node> <node>])` loads
the event's route as `race-route` does, runs the race at 60 steps a second
from the start of the countdown, holds the car at the replay's first
position until the step after go and then moves it one position a step,
feeding each step's position to `step_race_progress` and its events and the
step's speed (its length over 1/60 s) to `step_race_timing`. It prints the
boundaries, the clock's rules, every stage and countdown change, every
forward crossing with its time or why it timed nothing, the backward and
missed crossings the route reports, every wrong-way change and reset, the
finish, each lap's time, the result, the race and the HUD. Exit status 1
for a usage error, including no replay; 3 on `race-route`'s refusals; 0
otherwise. It writes nothing.

## Event 1 on track 1003

Four runs over the retail files from the repository root in Git Bash, with
`R` the installation root. Each exits 0. The hashes are of standard output
with CR LF folded to LF (`| tr -d '\r' | sha256sum`), and are the same from
the default, the Release and the Debug build:

```text
T="build/nfsu_probe race-timing $R/Global/GLOBALB.BUN $R/FrontEnd/FrontB.lzc $R/Tracks/TRACKB1003.lzc 1"
$T --drive forward               34 lines 2469 bytes  B3ECE3B489FCBF3A0367EDA0394302B28A51C5E0772DA8C618AF02A6CD0BFD56
$T --drive backward              36 lines 2367 bytes  CF8D4752D487DF07F99DB4A1568DA6209D7831A7BB03EAF595DBCB52C9870D11
$T --drive forward --skip 23 3   33 lines 2428 bytes  DE68AE9C6B7B7A46C0A61BEC0DE817C7E67AFD4DB4FE7689C2D2554F9BADF67D
$T --drive forward --skip 10 20  33 lines 2338 bytes  EFCCB2CCF7F17B2D5D59F80E5A6AE2379B6F3DCC62B6AC91BB85C56A134C6A57
```

The five `race-route` pins of [`RACE_ROUTE.md`](RACE_ROUTE.md) are unchanged
from the same builds. Review comment 3941 (F2) changed the backward drive's
pin, which was 2370 bytes with `236EA9F6...D6727A`: its empty best lap and
fastest lap now print as 0x00586580 prints 0x7FFFFFFF, a zero time, where
the first round printed dashes. The other three are as they were.

**Two laps, finished.** The route replay's crossings (its steps 6, 6605 and
13204) come 162 steps later here, the car having held the grid through go:

```text
clock: 4000 ticks a second (0x006CC9FC), the world timer built at 4000 (0x0041F7DA), 60 steps a second of 67 ticks (0x00585CD5..0x00585E90), times rounded to 1/100 s (0x00586690)
countdown: three steps of 0.9 s (0x006B79E0), go at 2.6999998 s (0x00434D9F)
step 0 tick 4000 countdown 3
step 54 tick 7618 countdown 2
step 108 tick 11236 countdown 1
step 162 tick 14854 go race-start 14840
step 168 tick 15256 untimed boundary 0 ForwardStartLine lap 0 (no new checkpoint)
step 216 tick 18472 countdown off
step 3074 tick 209958 split boundary 1 SplitTimeBoundary1 lap 0 time 0:48.78 (195120) crossing 209960 backdate 12
step 3792 tick 258064 reset-zone zone 13 (the clock runs on)
step 5008 tick 339536 split boundary 2 SplitTimeBoundary2 lap 0 time 1:21.16 (324640) crossing 339480 backdate 46
step 6767 tick 457389 lap 1 boundary 0 ForwardStartLine time 1:50.63 (442520) total 1:50.63 (442520) crossing 457360 backdate 16
step 9673 tick 652091 split boundary 1 SplitTimeBoundary1 lap 1 time 0:48.68 (194720) crossing 652080 backdate 12
step 11607 tick 781669 split boundary 2 SplitTimeBoundary2 lap 1 time 1:21.07 (324280) crossing 781640 backdate 46
step 13366 tick 899522 lap 2 boundary 0 ForwardStartLine time 1:50.54 (442160) total 3:41.17 (884680) crossing 899520 backdate 16
step 13366 tick 899522 finished position 1 time 3:41.17 (884680)
result: racer 0 finished position 1 laps 2 of 2 time 3:41.17 (884680) best-lap 1:50.54 (442160)
hud: countdown off race-time 3:41.17 lap-time 1:50.54 held best-lap 1:50.54 lap 2 of 2 wrong-way no finished yes position 1
```

(an excerpt: the header lines, the two stage lines, the "go" countdown
line, the second visit to reset zone 13, the two lap-time lines and the
`race:` line are omitted here and included in the hash). The drive moves
0.5 a step, 30 units a second; lap 1 is 6605 steps of it from go, 110.08 s
of simulated time, and the clock reads 110.63 s: 6605 frames of 67 ticks,
less the rounding of the start and the backdate. Lap 1's split 1 is 0.10 s
longer than lap 2's, and lap 1 0.09 s longer, because both include the
grid's 0.08 percent (2.6 units) behind the line.

**Reversed: the wrong way, no lap.** The car goes the wrong way from its
first move (step 163), the route's reverse crossings take the lap counter to
-3, and the race never finishes; inside reset zone 13 the flag drops for two
steps, as [`RACE_ROUTE.md`](RACE_ROUTE.md) explains:

```text
step 163 tick 14921 wrong-way-on
step 3131 tick 213777 reset-zone zone 13 (the clock runs on)
step 3131 tick 213777 wrong-way-off after 0:49.71 (198856)
step 3133 tick 213911 wrong-way-on
result: racer 0 not-finished position 0 laps 0 of 2 time none best-lap 0:00.00 (2147483647)
race: outcome running stage racing steps 13419 go-step 162 race-start 14840 race-time 3:42.04 (888160) fastest-lap 0:00.00 (2147483647) wrong-way yes wrong-way-steps 13252 resets 2
```

**A skipped split.** Jumping from node 23 to node 3 misses split 1 on lap 1,
which then has no split 1 time; the lap counts, 1:18.39 for the shortened
lap:

```text
step 2941 tick 201047 route missed-forward boundary 1 SplitTimeBoundary1 lap 0 percent 10.535881
step 3083 tick 210561 split boundary 2 SplitTimeBoundary2 lap 0 time 0:48.92 (195680) crossing 210520 backdate 46
step 4842 tick 328414 lap 1 boundary 0 ForwardStartLine time 1:18.39 (313560) total 1:18.39 (313560) crossing 328400 backdate 16
step 11441 tick 770547 finished position 1 time 3:08.92 (755680)
```

**A skipped finish line.** Jumping from node 10 to node 20 misses the line:
the splits of the next lap raise no checkpoint (the car is still on lap 0),
lap 1 runs 3:05.99 to the next crossing, and the drive ends a lap short:

```text
step 6600 tick 446200 route missed-forward boundary 0 ForwardStartLine lap 0 percent 68.603714
step 7573 tick 511391 untimed boundary 1 SplitTimeBoundary1 lap 0 (no new checkpoint)
step 11266 tick 758822 lap 1 boundary 0 ForwardStartLine time 3:05.99 (743960) total 3:05.99 (743960) crossing 758800 backdate 16
result: racer 0 not-finished position 0 laps 1 of 2 time none best-lap 3:05.99 (743960)
```

## Tests

`tests/race_timing_tests.cpp` (ctest `nfsu_race_timing_tests`) checks:
the frame rule at 1, 13, 17, 30, 50, 60, 75, 100, 120 and 144 frames a
second and its refusals; the rounding at pinned values (19, 20, 59, 60,
14854, 24100, 442549, 64740, 64760, -20, -60, 2^29) and against the SHA-256
of a numpy float32 transcription of 0x00586690 and its callers, written
outside the repository by this worker, over every tick of the first minute
(`41AD16D1...` of 960000 bytes), every tick from -4000 to 3999
(`7CEC9F7A...`) and 1025 ticks spread over -2^29 .. 2^29 (`ED757A8F...`);
the format; every stage change allowed and refused; the countdown's edges
one tick at a time for T = 0.9 and 1.0, and at 67 ticks a step; progress
events during the countdown (timed as retail times them, and go still on
time); two laps with every time exact, the first crossing untimed, the
totals, the bests, the fastest lap and the finish; the backdate (667 ticks
for 5 units at 30 a second, 0 at a speed of 1, 20000 just above 1, 50 for
event 1's first crossing, 267 across the path's seam, and saturation at
2^29, and 378 ticks for a length, difference and speed where both other
orders of the distance's two products give 377); a backward crossing of the
line; a missed split; three laps, whose last total is the previous total
plus the lap; a highest lap passing the count without equalling it, which
finishes nothing; a time of 0 and the empty time printing alike, a lap timer
of exactly 0 on the go step of a race started at 3986, and a best lap of 0
left to the HUD's dashes; a restart mid-race
timing exactly as a fresh race and a restart after the finish; a quit; the
wrong way and a reset zone ending nothing; the clock limit; three racers
finishing in order with the unfinished last and an opponent's lap not the
fastest; the HUD's countdown, race time, running and held lap timer and best
lap; every refusal of a configuration, a route and a step's inputs leaving
the state untouched; the step clock over the fake timing service - one step
a 16667 us frame for 600 frames, a 100 ms stall handing out 4 and dropping
2, a clock going backwards, 600 steps in 1200 frames of 8333 us, step 60 at
exactly 1 s; and a synthetic circle driven through `nfsu_race_route` end to
end, forward (finished at step 2726, the laps adding up to the race time,
and the times pinned from this implementation's own run: 87040, 84680 and
171720 ticks) and backward (never finished, the wrong way).

`tests/race_timing_guards.cmake` (ctest `nfsu_probe_race_timing_guards`)
drives the command: the usage refusals (no replay among them), three
missing files, the forward, backward and both skipping drives over the
race-route fixture pinned by SHA-256, a missing event, another track's
bundle, a skip outside the path, a path file round a square, a malformed
path file and a missing one.

### Deliberate breaks

Twenty-five mutants of `src/race_timing.cpp`, each applied alone in this
worker's worktree - the first round's twenty, and five for review comment
3941: its N5 survivors 14, 20 and 22 and the two halves of its F2. For each
only `nfsu_race_timing_tests` and `nfsu_probe` were rebuilt, the unit binary ran under a 60-second limit and `ctest -R
race_timing_guards` after it, and the source was then restored byte for
byte and touched. A run is classified by what it printed: *killed* when the
unit binary printed a `test failure:` line or the guards a `CMake Error`,
*passed* when it exited 0 printing its success line, *unknown* otherwise.

| # | Mutant | Unit tests | Guards |
| ---: | --- | --- | --- |
| 01 | go does not reset the highest checkpoint | killed | killed |
| 02 | rounding half up made half down | killed | passed |
| 03 | rounding without the ceiling of 0x00438800 | killed | killed |
| 04 | countdown 3 while `since <= T` | killed | passed |
| 05 | go shown while `since < T` | killed | passed |
| 06 | 3T computed as T + T + T | passed | passed |
| 07 | frame units not rounded | killed | passed |
| 08 | backdate at a speed of 1 | killed | passed |
| 09 | backdate difference not wrapped | killed | passed |
| 10 | crossing not dated back | killed | killed |
| 11 | lap total without the previous lap | killed | killed |
| 12 | a crossing that raises no checkpoint is timed | killed | killed |
| 13 | every finisher placed first | killed | passed |
| 14 | an opponent's lap is the fastest lap | killed | passed |
| 15 | restart keeps the world timer | killed | passed |
| 16 | the race time not rounded | killed | killed |
| 17 | the finish on the lap before the count | killed | killed |
| 18 | the step clock does not hand out step 0 | killed | passed |
| 19 | the step clock replays a stall | killed | passed |
| 20 | the HUD holds a lap without its time | killed | killed |
| 21 | backdate distance as `L * (past * 0.01)` (review 14) | killed | passed |
| 22 | lap total adds the previous lap (review 20) | killed | passed |
| 23 | finish when the highest lap reaches or passes the count (review 22) | killed | passed |
| 24 | the empty time prints dashes | killed | killed |
| 25 | the empty best lap is not formatted | killed | killed |

Totals: 25 mutants, 24 killed, 1 equivalent, 0 unknown; the table is the
fix round's sweep over the final source, whose guard pins it ran against. Mutant 06 is equivalent for
every T: `T + T` is an exact doubling, so `(T + T) + T` and `T * 3.0` both
round the same exact value once. Mutant 07 survived the first sweep, when
every rate tested landed just above a whole number of units; the checks at
13 and 17 frames a second were added for it, and a rerun of it alone then
killed it. The review's mutant 25, the lap timer's hold while under rather
than at most 3 s, is equivalent: no whole tick count converts to exactly
3.0 (11999 ticks are 2.99975 s and 12000 are 3.0000002). Each of
race-timing's four path conversions in `src/main.cpp`, reverted alone to
`fs::path(argv[k])`, is refused by `nfsu_path_arguments`.

### The console

The Switch tree builds the library and includes its object in issue #175's
whole-tree census (`nfsu_switch_fma_census`, [FLOATING_POINT.md](FLOATING_POINT.md)).
Measured on this branch with devkitA64: `race_timing.o` holds 18 `fmul` and
no fused instruction; the same source compiled by the same command less
`-ffp-contract=off -fno-fast-math` holds 14 `fmul` and 4 fused
multiply-adds, and the census fails it. The console cannot run the replay
here, so what is asserted there is the precondition the desktop's pins rest
on.

## Approximations, labelled

- **The first move.** The lap update sees a released car move first on the
  step after go. The traced order rules out the go step's own lap update
  seeing a move the release caused; where the car's position is integrated
  in the frame, and what the player's driver's hold does, were not traced.
  One step later or earlier gives the event 1 times of the table under
  **Stages**: a hundredth or two on each lap and on the race time.
- **The HUD's moment.** `race_timing_hud` reads the world time of the step
  it follows; retail's HUD reads the timer itself at a point in the frame
  that was not traced. At most a frame on the running lap timer and on the
  3 s hold; display only.
- **The format** prints digits only, `M:SS.hh`, not 0x00586090's widths,
  language separators or leading space.
- **The step.** A step is one 60 Hz frame and adds 67 ticks, the retail
  arithmetic for a frame of exactly the display period. The retail frame is
  measured, and a frame that runs long or short adds other counts.
- **The start of the countdown.** The countdown starts on the first step
  after the race is built, at world time 4000; retail spends frames loading
  and in the flyby and prerace first, so its race start, and with it the
  phase of every rounding, is later by a number of frames this model does not
  know. Every time is rounded relative to the world timer, so another start
  frame can move a time by a hundredth.
- **When the countdown's start is taken.** `set_race_stage` takes the world
  time of the next step, as if the coordinator set the stage before the race
  status update of the same frame.
- **0x0078A368** is taken as clear: T = 0.9.
- **The speed** of a replayed car is its step's length over 1/60 s, as
  `race-route`'s wrong-way test takes it; a driven car brings its own.
- **Empty times.** Retail marks an unset time with 0 or 0x7FFFFFFF; this
  model keeps a flag beside each, which differs only for a time that is
  exactly 0, which a world timer counting from 4000 never is.
- **The point-to-point rule** at go (0x00434C37..0x00434C42) is not
  modelled: such a race is refused, as its route is.
- **The backdate's saturation** at 2^29 ticks, where retail's `_ftol` would
  give 0x80000000 for a distance no retail path has.
- **The HUD's lap count**: the lap counter, never below 0; which count the
  retail HUD shows was not traced.
- **The step clock** is this project's: retail varies the frame's length
  rather than the number of fixed steps.
- **Float.** As in [`RACE_ROUTE.md`](RACE_ROUTE.md): IEEE single arithmetic
  in retail's order stands for the x87 at 24-bit precision.

## Unknown

- How many frames retail spends between building the race and starting the
  countdown, with and without the flyby.
- Where in the frame a car's position is integrated; the player's driver
  class, and what its hold does to the controls.
- When the HUD's timers run relative to the world timer's advance.
- What `0x0078A368`, `0x0073465C` and the race type `0x0078A30C` are by name.
- The finish statuses 2 to 9 and the paths that give them.
- What the results screen (`PostRaceResults.fng`) shows beyond the times and
  places the race status holds.
- Which lap number the HUD's lap counter shows.

## Read-only

The retail installation was read only. A manifest of every file's path,
size and modification time (`find . -type f -printf '%P\t%s\t%T@\n' |
LC_ALL=C sort`, 476 files) was taken before the first read and after the
last, and the two are identical under `cmp`.
