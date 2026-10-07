# Race audio service (issues #65 and #202)

The race reads its retail files under the supplied root. It never writes to
that tree and contains no bundled game audio. The desktop `nfsu_race --live`
path and the Switch race path create an audio service after the race session
starts. A missing bank or unavailable device is reported as `audio:` or
`AUDIO` while the race continues. Desktop replay and `--live-smoke` remain
silent so automated runs need no sound device.

## Signal flow and limits

After each 60 Hz race tick, `race_audio_snapshot()` copies engine RPM,
throttle, horizontal speed, maximum wheel slip, copied per-contact observations from both substeps,
the latest physics step, countdown, completed laps, and race finish state from
`RaceSession`. The race tick identifies duplicate audio frames; contact aging
uses the separately copied physics step because countdown advances only the
race clock. An unchanged physics clock holds collision graph controls.
The audio service has no mutable reference to
the session. The simulator never reads from the audio service. The output
thread mixes eleven fixed clip slots and one streamed stereo signal into
48 kHz signed 16-bit PCM. Clip phase and gain use Q32 and Q15 integers;
voices are summed in slot order with 64-bit accumulators. The race enables a stereo-linked peak limiter
before the final PCM16 clamp; raw overload and limited-frame counters remain
visible alongside actual clipping. This is port output policy; see
[Collision audio](COLLISION_AUDIO.md).
Both clip and AST source-rate conversion use linear interpolation. Gain
changes ramp over five milliseconds. Output fades on pause and resume;
successive impacts occupy two slots with short onset ramps.
The AST decoder produces
one block at a time on a producer thread, converts to 48 kHz stereo, and
waits for room in an 8192-frame ring. An empty ring emits silence and an
overfull nonblocking push drops excess frames; both have counters.

Windows output uses `waveOut` with three 1024-frame buffers. Switch output
uses `audren` through libnx `audrv` with three queued aligned 1024-frame
buffers. The render thread is pinned to its current core; the decoder and
native audio output thread use a different core, with output priority 0x28.
Both request 48 kHz stereo int16. The Switch diagnostic panel and result
screen report ring underruns and fill, queued output frames, late output
cycles, thread work time and mix clips. Direct mode also prints those lines
to nxlink. `AUDIO STATUS` reports startup and runtime errors.
Music startup uses stream bank offsets to read only each segment's header and
end marker. It does not scan or decode the entire AST. Loading checkpoints
poll HOME/+ while indexing and waiting for the stream ring.
Its persistent payload check includes decoded clip storage, the ring,
and output buffers. Pausing silences output and holds the stream in the bounded
ring. Finishing fades music and engine and leaves the finish cue audible.
No third-party audio library is used.

## Retail lookup evidence and current cue choices

All virtual addresses below refer to the examined PC `speed.exe` with
SHA-256 `C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`.
The file's image base is `0x00400000` and the cited `.text` and `.rdata`
file offsets equal virtual address minus that base.

At `0x00521C83`, the sound manager switches on event type. Type 12 at
`0x00521DA3..0x00521DC1` reads group from manager offset `+0x3C` and
upgrade from `+0x40`, computes `4 * group + upgrade`, then loads an engine
bank name from `0x006F8000` and a directory from `0x006F7EB0`. The name
`C0U0_M0_ENG_MB` is at file offset `0x2BEAD0`; directory
`Car_0\Eng_UG_0\` is at `0x2BEE18`. Type 13 at
`0x00521DC3..0x00521DE7` computes `5 * group + upgrade` for the exhaust
bank-name table at `0x006F8040` and uses the same directory table. At
`0x0051D2F0..0x0051D351`, 28-byte event records supply type, upgrade and
group at offsets `+0x1E8`, `+0x1EC` and `+0x1F0` relative to the owner
object for record zero. `0x00522020..0x00522044` transfers these values
to the sound manager and performs the bank lookup.

The later [issue #312 investigation](AUDIO_FIDELITY_312.md) connects the
stock 240SX descriptor through the event producer to sound group 2 and its
CAR2 engine/exhaust banks. It also executes the native engine ABK controls,
which differ from the port's adjacent RPM layers, and corrects the earlier
reversed event-field description above. Actual intro-event upgrades,
upstream audio RPM/load transitions and final output parity remain open.
`ENGINE_EVTSYS.csi` and its `.a1c` companion name `PLAYER_CAR`, `AI_CAR`,
and `DYNAMIC_LOAD` but do not by themselves map RPM to samples.
The `RPMQuantizer` string at `speed.exe` file offset `0x2CA378` is reached
from `0x00439F4B`; that reference did not establish an audio mapping.

The service therefore uses the following explicit approximations:

| Cue | Runtime choice | Unresolved retail link |
| --- | --- | --- |
| Engine | `Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk` samples 1..4 loop on slots 0..3. Adjacent layer centers are 800, 2500, 4500 and 7000 RPM with linear crossfade. Pitch is RPM divided by layer center, clamped to 0.5..2.0. Gain is `weight * (0.22 + 0.33 * throttle)`. | Known stock bank/control discrepancies in #312; actual tuned setup, live audio RPM/load and output parity remain open. |
| Tyres | `Sound/SKIDS/SKID_PAV_MB.abk` sample 1 loops when speed exceeds 2 m/s and slip exceeds 0.25; gain is clamped to 0..0.45. | Pavement event selection and slip/gain curve. |
| Collision | World impact module selects samples 30..42 through recovered subtype/variant counters and gain/pitch curves; world scrape uses its single carrier with recovered modulation. See [trace and adapter limits](COLLISION_AUDIO.md). | Retail pair lifetime, car/smokeable paths, voice admission, spatial settings and exact RNG/control scheduling. |
| Music | `Sound/STRMBANK/STRM_Music_UG0_MB.abk` links to `SNDSTREAMS/STRM_Music_UG0_MB.ast`. Linked segments after segment 0 stream in bank table order and repeat at gain 0.5, ducked to 0.25 during interface cues. | Segment 0's role, playlist order and track titles. Skipping segment 0 and table order are approximations. |
| Countdown | `Sound/FE/FE_MB.abk` sample 25 plays on each 3, 2, 1 transition. | Exact retail event to sample mapping. |
| Go | `Sound/FE/FE_MB.abk` sample 26 plays at the go transition. | Exact retail event to sample mapping. |
| Lap | `Sound/FE/FE_MB.abk` sample 29 plays when completed laps increase. | Exact retail event to sample mapping. |
| Finish | `Sound/FE/FE_MB.abk` sample 30 plays once at finish. It takes precedence over the final lap cue. | Exact retail event to sample mapping. |

The named WAV candidates in `AUDIO_FORMATS.md` were heard by the owner and
accepted as free of audible corruption. The four interface sample choices
remain provisional until the owner hears the FE montage and the Switch run.

## Verification

`nfsu_audio_playback_tests` uses synthetic PCM and AST bytes. It checks
gain, positive and negative clipping, loop boundaries, pitch, source-rate
conversion, stereo order, a derived sine-image threshold, bounded AST prefix
scanning, ring ordering and underrun/overrun accounting, producer
backpressure, two-block streaming, playlist order and repeat.
`nfsu_race_audio_tests` checks the crossfade, bank-boundary indexing,
playlist construction and missing-file fallback. `nfsu_race_audio_cue_tests`
checks countdown, go, lap and finish edges. `nfsu_race_run_tests` checks the
replay pins. `nfsu_audio_retail` checks the full ABK/AST decoded manifest
and hashes retail files before and after reading them. A Switch build proves
the `audren` code links; audible output still requires a console run.
