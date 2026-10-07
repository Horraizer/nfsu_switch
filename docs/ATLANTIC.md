# Atlantic circuit (issue #327)

`--event 8` selects the next standalone career circuit after event 1. The
read-only FRONTB race table has one circuit slot for event 8: track 1005,
three laps, forward. Event 6 is a tournament, so it is not the next standalone
circuit. GLOBALB names track 1005 `DT3 - Atlantic`, region `L1RA`, lap skip 0.
The FRONTB event record is at decoded-image offset 11747568, its slot at
11747688; the GLOBALB track record is at 2102648. `load_supported_race`
validates this selection before either backend loads the course.

## Run

```text
build-release/nfsu_race --root X:/Games/NFSU --event 8 --live
build-release/nfsu_race --root X:/Games/NFSU --event 8 --generate build-atlantic-input.txt --out build-atlantic-frames --frames 180,600,6000
nxlink -s --args="--event 8 --live" build-switch/switch/nfsu_switch_race.nro
```

Use a new output path outside the retail installation. The console command
is an owner-operated acceptance step, not a claim of a completed hardware
run. Atlantic uses live controller input on Switch; its menu disables the
event-1 replay shortcut. Desktop accepts a generated Atlantic input recording
with `--event 8 --input <recording>`. No retail files or textures are bundled.
Without `--event`, event 1 and its existing recording remain the defaults.

The `first-lap-1005` manifest authenticates the common car/global inputs and
these additional original files. The shared STREAML1RA input retains the
existing manifest identity.

| File | Stored bytes | SHA-256 |
| --- | ---: | --- |
| Tracks/TRACKB1005.lzc | 423196 | 635C53E038FA7CBE0F90B00F8089DD3D7FAD7135433AD678663EED2F4371E8A5 |
| Tracks/TEX1005TRACK.BIN | 1086216 | CD86FEDB15B5F4CBC3E2180CBE0DECD855F7F58AACF00454B13CC175404976E1 |
| Tracks/STREAML1RA_1005.BUN | 0 | E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 |

## Recovered inputs and bounds

The existing decoders read Atlantic's own geometry, instances, texture table,
collision, forward start marker, progress spline, split/lap boundaries and
visibility zones. Residency selects its own permanent set and verifies that
every route zone stays inside the uploaded set. Texture spillover uses the
same authenticated L1RA texture-only sections as the existing circuit.
Wet-road lamps are loaded from TRACKB1005; sky, object bounds and road material
flags come from its own streamed scenery. Common 240SX, car effects and global
environment inputs use the existing race path.

The GPU loader retains every mesh hash referenced by every placed object's
six selection slots, including LOD, reflection and fallback slots. It removes
only uninstanced library meshes. Two otherwise unresolved hashes belong to
uninstanced end-of-race figures and a cargo shed. After each section is parsed,
Atlantic releases source bytes and rereads selected texture ranges during
decode. This avoids retaining raw sections beside decoded GPU blocks. The
allocation-counting `nfsu_circuit1005_loader_peak_retail` test enforces the
existing 48 MiB ceiling including race, audio and first-frame preparation.

One placed power-tower material requests absent hash `0x828DCF0B`. Original
solid loading reads the `0x00134012` texture list into solid +0x2C
(`speed.exe` 0x540023), then 0x53F9C0 passes each hash to 0x5461C0 with fallback
enabled. Missing lookup returns global pointer 0x73551C at 0x54622F. Texture
loading sets that pointer for hash `0x66994F4A`, DEFAULTALPHA, at 0x545998.
The later pointer fixup also explicitly substitutes it at 0x53FADF and writes
the solid's texture pointer at 0x53FAF7. GLOBALB's DEFAULTALPHA is the original
32x32 DXT1 record (info offset 27792, data offset 317952), including additive
blend state. Atlantic binds this record to the one evidenced absent hash;
existing or unusable claims are never overwritten. It does not paint a
replacement texture or alter the original mesh hash list.

The authenticated executable SHA-256 is
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
With Capstone and Unicorn installed, reproduce the bounded miss/hit witness:

```text
python tools/atlantic_texture_evidence.py X:/Games/NFSU
```

The witness executes the original fixup bytes; only global lookup is stubbed
to return a miss or a hit. It also prints the original solid lookup and
DEFAULTALPHA assignment instructions and verifies the executable stayed
unchanged. It establishes pointer substitution, not whole-game rendering.

## Validation and remaining acceptance

`nfsu_circuit1005_retail` checks retail selection, own collision/start,
residency, a nonempty GPU frame, original DEFAULTALPHA payload/state and no
unresolved placed materials. `nfsu_circuit1005_drive_retail` drives all three
laps through controller input and asserts normal completion without resets.
The test driver uses authored obstacle offsets, an approximation for
repeatable testing; it changes no physics, pose or route state. Runtime and
frame pins belong to tests, not this document. Existing event-1 retail tests
continue to check its unchanged replay, rendering and memory ceiling.

This adds one circuit to the current bounded single-player race integration.
It does not establish original opponent AI, traffic, career rewards or full
game behavior. Independent review and an actual Switch live drive, including
start, road and collision continuity, scenery/materials/effects, lap advance
and finish, remain required before issue acceptance. A package build alone
does not satisfy those criteria.
