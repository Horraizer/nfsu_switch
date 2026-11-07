# Original PC recorder

`tools/original_recorder.py` is a standalone Windows GUI recorder. It requires
64-bit Python 3.10 or newer with Tkinter; it uses only the standard library.
The owner's installed desktop shortcut is named `NFSU Original Recorder`.

## Recording

1. Open the recorder and click **Start recording**.
2. Launch the original PC game, or return to an already running original.
3. Play a race. A countdown, full race and results provide useful coverage.
   For AI comparisons, keep the selected difficulty, car and upgrades in the
   optional session note. Capture different situations in separate sessions.
4. Click **Stop and save ZIP**, or close the game. Wait for **Saved**.
5. Click **Open recordings** and provide the resulting ZIP for analysis.

The recorder can wait for the game before it starts. Alt-Tab to stop it after
playing; the recorder does not install global hotkeys or control the game.
Closing the recorder during capture requests a stop and waits for packaging.
The default output is `NFSU-Recordings` in the Windows Documents folder.
There is no automatic upload. Existing recordings are never overwritten.

Manual launch from the repository root:

```text
python tools/original_recorder.py
```

To install a user copy with its documentation and desktop shortcut, run
`python tools/original_recorder.py --install`. Installation uses the same
Python runtime, does not require administrator access and changes no game files.

An optional `--output <folder>` selects a different output directory. Keep new
directory names ASCII. The parent user/workspace folder may already contain
non-ASCII characters. The program discovers Windows folders through the OS.

## Supported original

Only the authenticated original PC 1.4 USA executable is accepted:

```text
speed.exe SHA-256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7
image base 0x00400000
```

The recorder pins the process ID, creation FILETIME, executable hash and image
base. It refuses an unsupported executable or multiple `speed.exe` processes.
It opens the process with `PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION`.
It never writes game memory/files, injects code, attaches a debugger, suspends
the process, sends game input or changes saves. Launch both programs as the
same Windows user; an elevated game can refuse access to a normal recorder.

## Output and coverage

Each session has an independently named directory and verified ZIP:

| File | Meaning |
| --- | --- |
| `metadata.json` | Recorder/source identity, process identity, note, limitations |
| `samples.jsonl` | Polled frames, clocks, race stage, actors, controls, paths |
| `events.jsonl` | Attach, context snapshots, errors, stop reason |
| `memory.bin` | Raw blocks with elapsed time and original address |
| `summary.json` | Motion, clock changes, failed reads, frame crossings, overhead |
| `manifest.json` | SHA-256 and byte length of every other session file |

ZIP members are read back and hashed before the final `.zip` is published.
Raw files remain if packaging fails. No executable, textures or music are
bundled. Memory captures can include player/profile information; inspect before
sharing publicly. A ZIP contains sampled memory, not a video recording.

Sampling requests 60 polls per second. Unchanged states are saved once per
second, while the summary counts every poll. Selected raw blocks are sampled
twice per second, with a larger globals/context snapshot at attachment, race
stage changes and every 30 seconds. The recorder stops and packages at its
1 GiB session data ceiling. Start another session for a longer recording.

`cross_frame` flags samples whose clock/frame changed during reads. Missing
fields are `null`, not invented zero values. Link cycles and unreadable actors
are reported. The process keeps running during reads: this is not an atomic
snapshot and cannot guarantee capture of every physics step or collision.
`read_ms` measures the polling reads/decoding, not GPU time or the entire
recording cost; `overruns` records missed polling deadlines, including raw
context and output costs. The recorder consumes CPU, memory bandwidth and disk.

Coverage distinguishes human movement from menu/paused captures. It does not
claim a complete lap automatically. A capture supports comparing original and
port trajectories and constructing focused replay fixtures; it is **not** a
complete deterministic replay or proof of matching behavior in unseen cases.
Original PC capture timing cannot establish Switch CPU/GPU performance.

## Decoded fields and raw evidence

The fixed layout is restricted to the executable above. The current decoding
follows the original readers used in `controller293_native.py`,
`controller293_planner.py`, `handling_modes_native.py`,
`human_engine_native.py`, `drag_world_rng_native.py` and `minimap266_native.py`.

- Frame: `0x7364DC`; original clock ticks: `0x73AD34`.
- Actor manager pointer: `0x7361F8`; shared world RNG seed: manager `+0x2D0`.
- Race status pointer: `0x73619C`; stage is its first word.
- Actor list ends at the manager sentinel; traversal is bounded and cycle checked.
- Actor `+0x60` is XYZ position, `+0x70` is XYZ **velocity**, not heading.
- Actor `+0x3D4` is published speed in m/s; `+0x3D8` is the raw gear slot;
  `+0x3E0` is RPM; `+0x3EC` is torque. The gear slot is preserved without UI
  gear-label assumptions.
- Actor `+0x14` points to configuration: role `+4`, car type `+0x10`, signed
  AI context `+0x735`. Role 1 is human, 2 rival; other roles remain numeric.
- Actor `+0x30` points to the driver. Steering word `+0x1D0`, gear command
  `+0x1D4`, throttle `+0x1DC`, handbrake `+0x1E0`, service brake `+0x1E4`.
- Driver `+0x10` contains the bounded planned path, road IDs and point indices.

Raw actor/configuration/driver and related pointer blocks retain additional state for later
analysis. Context snapshots retain BSS `0x730000..0x790000`, race flags and
career event/installed-part data. Unknown fields remain undecoded. Raw records
repeat little-endian `<dII` (elapsed seconds, address, byte count), then exactly
that many bytes. There is no zero-padding for failed reads.

Version 1.1 reads the `0x1E8`-byte common driver prefix at polling frequency,
instead of requiring an `0x800`-byte allocation for every driver. The larger
AI driver tail is optional slow raw evidence. The first live UG0 recording
showed that the larger read failed for the human driver while its actor state
and all rival drivers remained readable. That recording still contains the
full driven race, but not the player's throttle, brake or steering inputs;
the absent bytes cannot be recovered from that recording.

## Focused validation

```text
python tests/original_recorder_tests.py
```

Checks cover field decoding, invalid/missing reads, path bounds, linked-list
termination, frame crossings, paused versus moving coverage, ZIP hash integrity,
and read-only Windows process access using a cooperating Python child. That
child is explicitly rejected by the production executable authenticator.
Testing does not require manipulating the owner's game UI.
