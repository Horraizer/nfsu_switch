# Retail audio formats (issue #205)

This describes the PC retail installation examined for the first-lap work.
All offsets below are file offsets, hexadecimal unless explicitly decimal;
multi-byte container integers are little endian. The named files are relative
to the retail root. No retail bytes or decoded audio are committed.

## Inventory and reproducibility

The examined `Sound`, `SNDSTREAMS`, and `SpeechStreams` trees contain 125
files: 106 ABK banks, 7 CSI files, 7 A1C files, and 5 AST streams. The bank
parser finds 563 embedded samples and the AST scanner finds 759 segments.
The decoder covers all 563 samples and all 759 segments. The fixture
`tests/audio_retail_expected.txt` pins one line per ABK or AST (111 lines):

```text
relative_path bank_count entry_count sample_count segment_count total_frames SHA256_of_concatenated_PCM16LE
```

Each count is local to that file. Samples and segments are concatenated in
table order. `nfsu_audio_retail` (label `retail`, registered when the build is
configured with `-DNFSU_RETAIL_ROOT=<installation>`) checks that manifest and
hashes every one of the 125 input files before and after the run. The decoded
output is 16-bit little-endian interleaved PCM, at each header's own rate and
channel count. No resampling, loop expansion, gain, or engine mixing is
applied. The manifest therefore pins the decoder output, not the exact in-game
playback mix.

## ABK modules and BNK sample headers

`Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk` supplies a concrete example:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| `0x0000` | `41 42 4B 43` | `ABKC` container signature |
| `0x000A` | `01 00` | One module |
| `0x001C` | `5C 00 00 00` | Module table starts at `0x5C` |
| `0x0020` | `00 25 00 00` | Embedded `BNKl` at `0x2500` |
| `0x0080` | `04` | Four players in this module (`0x5C + 0x24`) |
| `0x0088` | `4C 06 00 00` | Player data base `0x64C` (`0x5C + 0x2C`) |
| `0x0CC4` | `04 00 00 00` | Four 12-byte sample/stream entries |
| `0x0CC8` | `00 FF 00 00 01 00 00 00 00 00 00 00` | Entry type 0, sample index 1, loop value 0 |
| `0x2500` | `42 4E 4B 6C 05 00 05 00` | `BNKl` version 5, five index slots (slot 0 empty) |
| `0x2518` | `10 00 00 00` | Slot 1 header is 16 bytes after this slot, at `0x2528` |
| `0x2528` | `50 54 00 00` | Sample parameter header (`PT`) |
| `0x2533` | `80 01 02` | Version tag `0x80` is 2 |
| `0x2536` | `85 03 00 82 1E` | `0x85`: 33,310 frames |
| `0x253B` | `A0 01 08` | `0xA0`: codec 8, PCM16LE |
| `0x253E` | `86 00 87 03 00 82 1D` | Loop start 0, inclusive loop end 33,309 |
| `0x254B` | `88 04 00 00 00 E0` | Data offset `0xE0` relative to `BNKl`, at `0x25E0` |
| `0x25E0` | `2B FF E8 10 59 2A` | First three little-endian PCM samples |

The module contains player records; each player points to a table whose
4-byte count precedes 12-byte entries. The 12-byte entry layout observed in
the retail banks is type at +0, value at +4, and loop value at +8. Type 0
refers to a BNK sample index. Types 1 and 2 refer to stream offsets in a
companion AST. A stream-only ABK has zero at container offset `0x20` and no
embedded BNK. Multiple players may refer to the same table; it is counted
once. The parser validates every type-0 reference against the BNK indices.

BNK sample parameters are length-prefixed tags in a `PT 00 00` record.
Relevant tags observed are `0x80` version, `0x82` channels, `0x84` rate,
`0x85` frame count, `0x86` loop start, `0x87` inclusive loop end,
`0x88` BNK-relative data offset, and `0xA0` codec. An absent rate uses
22,050 Hz; an absent channel tag means mono; an absent codec tag means
EA-XA code 10. Code 8 is PCM16LE; code 10 is EA-XA ADPCM. The API exposes
loop end as exclusive (`inclusive + 1`) so a full-sample loop is `0:frames`.
The retail samples are mono. The sample codec totals are 456 PCM16LE and
107 EA-XA version 2.

## AST stream segments and blocks

`SNDSTREAMS/STRM_Music_UG0_MB.ast` starts as follows:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| `0x0000` | `53 43 48 6C 24 00 00 00` | `SCHl`, 36-byte stream header |
| `0x0008` | `50 54 00 00` | Parameter header |
| `0x0016` | `80 01 02` | Version 2 |
| `0x0019` | `85 03 0A 14 ED` | 660,717 frames in segment 0 |
| `0x001E` | `82 01 02` | Two channels; rate defaults to 22,050 Hz |
| `0x0024` | `53 43 43 6C 0C 00 00 00 C2 01 00 00` | `SCCl`, 450 data blocks |
| `0x0030` | `53 43 44 6C 60 07 00 00` | First `SCDl`, 1,888 bytes |
| `0x0038` | `CC 05 00 00` | 1,484 frames in this block |
| `0x003C` | `00 00 00 00 A6 03 00 00` | Channel payload starts at +0 and +934 from the block data base (`0x44`) |
| `0x0044` | `EE FE FE FE E7` | Version 2 PCM reset frame: marker and two history samples |
| `0x0AF3F0` | `53 43 45 6C 08 00 00 00` | `SCEl` closes segment 0 |
| `0x0AF400` | `53 43 48 6C 24 00 00 00` | Next segment, after zero padding |

Every chunk has a four-byte ASCII tag and a four-byte little-endian size that
includes its eight-byte header. A segment contains `SCHl`, `SCCl`, one or
more `SCDl` chunks, and `SCEl`. `SCHl` carries the same parameter tags as a
BNK header. `SCCl` holds the expected block count at +8. An `SCDl` holds
its frame count at +8 and one 4-byte per-channel offset beginning at +12.
Those offsets are relative to the first payload byte after the offset table.
The sum of block frames and the block count must match `SCHl` and `SCCl` at
`SCEl`. Segments are padded to 128-byte boundaries with zeroes.

The music file has one version 1 segment. At `0x04979800`, `SCHl` has
`80 01 01` (version 1), `85 04 00 CF 0D 80` (13,569,408 frames),
`82 01 02` (stereo), and `84 03 00 AC 44` (44,100 Hz). Its first
`SCDl` starts at `0x04979834`: size `6C 0C 00 00`, frame count
`7C 0B 00 00` (2,940), channel offsets 0 and `2C 06 00 00`.
Each channel payload begins with four history bytes before the ADPCM frames.
The remaining 758 AST segments use version 2. The `0xA0` tag is absent in
these stream headers, so code 10 is the observed default.

EA-XA ADPCM is decoded with integer arithmetic. A normal 15-byte frame has
a predictor/shift byte and 14 bytes of packed signed nibbles, producing up
to 28 samples. Predictors 0..3 use coefficient pairs `(0,0)`, `(240,0)`,
`(460,-208)`, `(392,-220)` with an eight-bit fixed-point shift and 16-bit
clamping. Version 1 adds 128 before the shift. Version 2 also has a 61-byte
`0xEE` frame with two big-endian history samples and 28 big-endian raw PCM
samples. State carries between frames and blocks. The implementation reads
one AST block at a time, emits interleaved PCM, and never loads a complete
AST into memory. These rules were checked against the retail data and the
[vgmstream EA-XA decoder](https://github.com/vgmstream/vgmstream/blob/master/src/coding/ea_xa_decoder.c),
[EA SCHl layout](https://github.com/vgmstream/vgmstream/blob/master/src/layout/blocked_ea_schl.c),
and [ABK metadata reader](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_schl_abk.c).
The version 1 `+128` rounding comes from the vgmstream decoder; the retail
stream alone does not independently establish that one-bit rounding choice.

## Stream bank links and event companions

`Sound/STRMBANK/STRM_Music_UG0_MB.abk` has no BNK: bytes at `0x20` are
`00 00 00 00`. Its entry table at `0x1E0` begins with
`1B 00 00 00` (27 entries). Entry 0 at `0x1E4` has type 1 and offset 0;
entry 1 at `0x1F0` has type 1 and offset `00 F4 0A 00` (`0xAF400`);
entry 2 at `0x1FC` has offset `00 8A 51 00` (`0x518A00`). These are
the first three `SCHl` offsets in the matching AST. At `0x3B8`, the bank
contains the ASCII label `MUSIC_STREAM`. All 27 entries link to segment
starts; the other four stream banks with a companion AST also link.
Seven stream-only ABKs in this installation have no companion AST: the
French, German, Italian, Japanese, and Spanish speech banks, plus the
`_PCD` stereo ambience and music banks. Their metadata parses, but their
missing media cannot be decoded from this installation.

CSI and A1C files are event metadata, not PCM containers. The role is
supported by their signatures and event names, but their full record graph
is not decoded here. For example, `Sound/EVT_SYS/COLLISION_EVTS.csi` has
`4D 4F 49 52` (`MOIR`) at `0x00`, count `04 00 00 00` at `0x0C`,
`A1cpIMPACT_WORLD` at `0x64`, `IMPACT_WORLD` at `0x75`, and
`SCRAPE_WORLD` at `0xA2`. The paired `.a1c` has `41 31 43 4C`
(`A1CL`) at `0x00`, `IMPACT_WORLD` at `0x64`, and
`A1cpIMPACT_WORLD` at `0x71`. `NFSU_Streams.csi` contains
`MUSIC_STREAM` at `0x182`; its A1C partner has
`A1cpMUSIC_STREAM` at `0x16D`. Those names show event categories and
the stream name; they do not identify a particular ABK sample index.

## First-race evidence map

The search so far has not established a `240SX -> CAR_0` lookup or an
event-to-sample selection table. In `speed.exe` (SHA-256
`C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7`),
`C0U0_M0_ENG_MB` is at file offset `0x2BEAD0`, and
`Car_0\Eng_UG_0\` is at `0x2BEE18`. Their pointer tables are at virtual
addresses `0x006F8000` and `0x006F7EB0`. The code at
`0x00521DA3..0x00521DC1` indexes those tables with four times the
car sound group plus the engine upgrade level. This proves the bank lookup
after those two numbers have been chosen; it does not identify which group
the stock 240SX chooses. `Global/GLOBALB.BUN` was also inspected through
the decoded CarTypeInfo layout: the 240SX is record 18 of chunk
`0x00034600` at file offset `1736752` (see `docs/VEHICLE.md`). The known
handling fields in that record do not establish an audio group. The search
has not traced the code that sets the two lookup inputs, nor decoded the
CSI/A1C event selection graph. The following inventory keeps those links
explicitly unresolved.

| Sound role | Retail evidence | Status |
| --- | --- | --- |
| Engine at two RPM ranges | `Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk` has four looping PCM samples, indices 1..4, 22,050 Hz mono; sample 1 is 33,310 frames and sample 4 is 33,627. | `CAR_0` as stock 240SX and indices 1/4 as low/high RPM are guesses from names and sample grouping. RPM breakpoints are unresolved. |
| Exhaust and shifts | Adjacent `C0U0_M0_EXH_MB.abk` has four samples; `SHIFTING_SML_MB.abk` is in the same directory. | Candidate bank grouping; exact triggered index unresolved. |
| Pavement skid | `Sound/SKIDS/SKID_PAV_MB.abk` has four looping PCM samples at 44,100 Hz; sample 1 is 43,156 frames. | File name supports the role; selection among four samples unresolved. |
| Impact and scrape | `Sound/IG_GLOBAL/IMPACT_MB.abk` has 42 EA-XA samples; `SCRAPE_MB.abk` is adjacent. CSI has `IMPACT_WORLD` and `SCRAPE_WORLD` names at the offsets above. | Event category supported; specific impact and scrape sample indices unresolved. |
| Countdown, lap, finish | `Sound/FE/FE_MB.abk` has 53 samples and `Sound/GLOBAL/FE_COMMON_MB.abk` is another UI bank; `SpeechStreams/STRM_English_Speech_MB.ast` contains 277 speech segments. | Candidate containers only. There is no decoded label or event link proving which index plays for these three cues. |
| Race music | `STRM_Music_UG0_MB.abk` links 27 entries, in bank table order, to the 27 segments of `STRM_Music_UG0_MB.ast`. Segment 0 is 660,717 stereo frames at 22,050 Hz (about 30 s); segment 1 is 4,260,282 frames (about 193 s); segment 16 is the 44,100 Hz version 1 exception. | Storage order is proven. Race playlist order, track titles, and whether segment 0 is an intro are unresolved. |

The tool prints every sample and segment with index, rate, channels, frame
count, loop and codec. These commands write the six listening candidates
under one output directory outside both the repository and retail tree:

```text
nfsu_audio_dump <retail-root> <outside-repo-output> --write --bank C0U0_M0_ENG_MB.abk --sample 1
nfsu_audio_dump <retail-root> <outside-repo-output> --write --bank C0U0_M0_ENG_MB.abk --sample 4
nfsu_audio_dump <retail-root> <outside-repo-output> --write --bank SKID_PAV_MB.abk --sample 1
nfsu_audio_dump <retail-root> <outside-repo-output> --write --bank IMPACT_MB.abk --sample 1
nfsu_audio_dump <retail-root> <outside-repo-output> --write --bank FE_MB.abk --sample 1
nfsu_audio_dump <retail-root> <outside-repo-output> --write --stream STRM_Music_UG0_MB.ast --segment 1
```

The resulting relative WAV paths are:

```text
Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk.sample001.wav
Sound/CAR_0/ENG_UG_0/C0U0_M0_ENG_MB.abk.sample004.wav
Sound/SKIDS/SKID_PAV_MB.abk.sample001.wav
Sound/IG_GLOBAL/IMPACT_MB.abk.sample001.wav
Sound/FE/FE_MB.abk.sample001.wav
SNDSTREAMS/STRM_Music_UG0_MB.ast.segment001.wav
```

`--manifest <path>` computes the full PCM hash manifest, and
`--verify tests/audio_retail_expected.txt` compares it with the pinned
retail baseline. `--write` is required for WAV output; filters select the
WAVs to write. The tool refuses WAV output inside a Git repository or
anywhere under the retail root. WAVs are standard 16-bit PCM and should be
reviewed by listening before any sound is assigned a gameplay label.
