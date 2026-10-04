# Original introduction playback

Issue #320 supplies native EA MAD playback before the frontend in issue #66.
The player reads the owner's installation on the SD card. It writes no retail
file, extracts no movie, and needs no conversion step or external codec at run
 time. `play_original_intro` is linked into the Switch frontend through
`nfsu_intro_movie`; the menu worker owns the application entry and window.

## Authenticated startup sequence

The installed North American `speed.exe` bootflow constructor at `0x4DE1E0`
uses the six-entry string-pointer table at `0x6FA1A4`. The order is disc check,
EA logo, PSA, introduction, splash, new/load profile. The EA logo constructor
at `0x4DEAA0` selects `na_boot` in its North American branch; the PSA
constructor at `0x4DEBF0` selects `PSA`. The decoded `FrontB.lzc` contains
`LS_IntroFMV.fng` in the nested JDLZ block at `0xBB4E8C`. Its Movie object
is `e3_title` (object name at package offset `0x304`), and its original source
reference is `e3_title.avi`. The executable movie table maps this name to 19.
The implemented order is therefore `na_boot.mad`, `PSA.mad`, `e3_title.mad`.
The other regional bootflow table uses THX and a disclaimer instead of PSA;
that regional sequence is not the identity of this installation.

Read-only reproduction, from the workspace with standalone Python:

```text
python tools/intro_media_trace.py <retail-root>
```

The executable and package offsets above are tied to these inputs:

| Input | Bytes | SHA-256 |
| --- | --- | --- |
| speed.exe | 3178496 | c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7 |
| FrontEnd/FrontB.lzc | 4182578 | 76f368d5e5e30f96a8182bf457dba4491395e075c3f336d86a32012b56e16f90 |
| Movies/na_boot.mad | 2397904 | 8342f3bcd2ecb708858fd1bffa2a6f2ea3d3d398522a29f8fed5473e316ab61c |
| Movies/PSA.mad | 6291068 | cbb41e6acb311a9ed06ff10ff2ed26e828d690078ea21470405cdb5bcd1b3fcd |
| Movies/e3_title.mad | 26059320 | 65991efa4970a4a5d48c8937a1165889bb1e2077db718e1c30516e7fa9e9c615 |

## Original display geometry

Encoded dimensions are 512x256 for `na_boot` and `PSA`, and 512x512 for
`e3_title`. These samples are not square pixels in the original frontend.
All three original Movie objects have Size 640x480. The nested FNG packages
are `LS_EAlogo` at `0xBB380C`, `LS_PSAMovie` at `0xBB3A8C`, and
`LS_IntroFMV` at `0xBB4E8C`; their Size float pairs are at package offsets
`0x25C`, `0x248`, and `0x3E8`, respectively. The trace tool checks each pair.

Native Movie type7 dispatch at `0x4F2D2D` calls renderer `0x4F1D80`.
It centers the frontend at (320,240) and calls object geometry `0x4F1810`.
That geometry multiplies SizeX and SizeY at `0x4F1AE2` and `0x4F1B26`.
Encoded texture width/height are read later at `0x4F1E95` and `0x4F1E9B`
for UV extent within the backing texture; they do not set quad proportions.
The trace checks the dispatch, Size multipliers and center instructions.

The original player supplies display geometry separately from decoded sample
width/height. `blit_movie` fits that 4:3 rectangle into the destination and
samples the complete encoded picture into it. A widescreen Switch therefore
uses side bars. No picture is cropped or fitted using raw sample proportions.
The Switch Screen uses a linear RGBA framebuffer and passes its byte stride
unchanged to this helper. The padded-stride regression checks geometry and
source mapping for both original sample aspects, odd sizes and tall viewports.

Owner testing of integrated source `42f96af6` reported working intro audio
and rejected image scaling. That build fitted encoded dimensions and is not
accepted for movie presentation. The geometry correction preserves its audio
path. Corrected hardware image acceptance remains pending owner retest.

## Player contract

`include/nfsu/intro_movie.h` exposes the player and a stride-aware RGBA
letterbox helper. The caller services applet/pad input in `IntroPoll`, and
paints the borrowed frame in `IntroPresent`. A/B skip the complete startup
sequence; Plus or application shutdown returns `quit`. Completion and skip
return with the audio thread joined and the audio device closed, so the menu
can immediately acquire the output. Missing, corrupt or unsupported media,
output failure and presentation exceptions return `error` with a reason;
the frontend can show that reason and continue into the menu.

Video presentation uses the actual device played sample count from
`waveOutGetPosition` or `audrvVoiceGetPlayedSampleCount`. Prediction frames
are decoded even when their presentation deadline has passed. Late frames
are omitted from presentation while audio continues on its original clock.
After the final picture the player drains the original audio tail. A stalled
device clock fails instead of hanging the startup flow indefinitely.

Two independent file handles stream the audio and video chunks. Chunk sizes,
file sizes, dimensions, durations, coefficient runs, bit reads, channel
offsets, prediction references and decoded audio totals are bounded and
checked before access. Storage grows with the maximum chunk and picture,
not the length of the introduction. Stereo EA-XA R2 is decoded at its native
rate and linearly resampled to the platform's 48 kHz output when required.
The installed movie formats are supported directly: MADk, MADm and MADe
YUV420 frames with stereo R2 audio at 44.1 or 48 kHz.

## Verification

`nfsu_intro_movie_retail` decodes the complete three movies and checks video
frames, audio frames, native rates, full YUV and PCM hashes. The pins are in
`tests/intro_movie_tests.cpp`. It participates in the retail read-only fixture.
`nfsu_intro_player_tests` uses synthetic media and a deterministic audio
device to check decoder failures, PCM reset frames, resampling, clocked
sequence completion, skip/quit/error paths, output teardown and blit bounds.

Independent full-image checks used FFmpeg 8.1.1 without saving decoded assets:

```text
ffmpeg -v error -i <movie.mad> -map 0:v -fps_mode passthrough -pix_fmt yuv420p -f hash -hash sha256 -
ffmpeg -v error -i <movie.mad> -map 0:a -c:a pcm_s16le -f hash -hash sha256 -
nfsu_intro_movie_tests --inspect <movie.mad>
```

Every full YUV digest and every complete 28-sample audio-block digest agrees
with that reference. FFmpeg truncates the last partial audio block; the
native decoder retains the header's final sample count. The test prints a
separate reference-compatible PCM digest, as well as the full native digest.
This distinction prevents a reference's truncated tail from becoming an
incorrect playback duration.

The EA transform and MPEG VLC tables are adapted from FFmpeg n7.1
[`eamad.c`](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/eamad.c),
[`eaidct.c`](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/eaidct.c),
[`mpeg12data.c`](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/mpeg12data.c)
and [`aandcttab.c`](https://github.com/FFmpeg/FFmpeg/blob/n7.1/libavcodec/aandcttab.c).
Their attribution is retained in the source and their LGPL 2.1 license is in
`third_party/ffmpeg-LGPL-2.1.txt`. License form-feed pagination is normalized
to newlines for this repository's allowed ASCII whitespace.

Hardware acceptance still requires running the integrated Switch frontend
and checking the original pictures, audible stereo playback, lip/action
synchronization, skip, HOME/Plus handling and transition into the menu.
Desktop decode and a packaged NRO do not establish console timing or sound.
