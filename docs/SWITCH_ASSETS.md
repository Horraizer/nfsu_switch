# Retail assets on the Switch

Which retail files the port reads, where they go on the Switch's SD card, how
a small application proves they are the right files before anything tries to
load them, and what is not known yet. This is issue #47, under the Switch
goal #44, for the first-lap target of #159: one timed lap of race 1 of
Underground mode - event 1, a circuit on track 1003 in stream region `L1RA`
([`RACE_EVENTS.md`](RACE_EVENTS.md)) - in the stock 240SX.

No game data is in this repository and none is packaged into an application:
the owner copies the files from their own installation. This note holds file
names, sizes and SHA-256 hashes only.

The answer first:

- **The first lap needs 12 retail files, 108907646 bytes**, listed under
  "The manifest" below: the six the stock-car application already reads,
  and six more for the track and the race. The owner's SD card holds the
  first six today; the other six are `FrontEnd/FrontB.lzc`,
  `Global/INGAMEB.BUN`, `Tracks/STREAML1RA.BUN`,
  `Tracks/STREAML1RA_1003.BUN`, `Tracks/TEX1003TRACK.BIN` and
  `Tracks/TRACKB1003.lzc`.
- **They go under `sdmc:/nfsu/`, in the folders the game uses**, exactly as
  issue #141's application already reads its six. A small ASCII file,
  `sdmc:/config/nfsu_switch/assets.cfg`, can move that root.
- **`nfsu_switch_assets.nro` checks the card and shows the answer on
  screen**: one line per file, a verdict - `COMPLETE`, `MISSING FILES`,
  `PARTIAL`, `WRONG VERSION` or `UNREADABLE` - and the list of files still to
  copy. Nothing goes over the network.
- **`nfsu_probe asset-check <root>` runs the same check on the desktop**,
  against the game's folder or against the SD card in a card reader. Over
  the owner's retail installation it reports all 12 files `ok` and exits 0.
- **Names are matched without regard to ASCII case**, sizes are compared
  before anything is read, and a file is hashed only when its size is right,
  in chunks of at most 1 MiB. The check writes nothing.

## Preparing the SD card for the first lap

1. Open the game's installation folder, the one that holds `speed.exe`.
2. Copy these files to the SD card, keeping their folders, under `nfsu/`
   at the card's top level. A file already there with the same size need
   not be copied again; the application says which ones are wrong.

   | Copy from the game's folder | To the SD card | Bytes |
   | --- | --- | ---: |
   | `Cars/240SX/GEOMETRY.BIN` | `sdmc:/nfsu/Cars/240SX/GEOMETRY.BIN` | 4780816 |
   | `Cars/240SX/TEXTURES.BIN` | `sdmc:/nfsu/Cars/240SX/TEXTURES.BIN` | 697340 |
   | `Cars/BRAKES/GEOMETRY.BIN` | `sdmc:/nfsu/Cars/BRAKES/GEOMETRY.BIN` | 69408 |
   | `Cars/PLATES/GEOMETRY.BIN` | `sdmc:/nfsu/Cars/PLATES/GEOMETRY.BIN` | 1184 |
   | `Cars/TEXTURES.BIN` | `sdmc:/nfsu/Cars/TEXTURES.BIN` | 1230808 |
   | `FrontEnd/FrontB.lzc` | `sdmc:/nfsu/FrontEnd/FrontB.lzc` | 4182578 |
   | `Global/GLOBALB.BUN` | `sdmc:/nfsu/Global/GLOBALB.BUN` | 2361176 |
   | `Global/INGAMEB.BUN` | `sdmc:/nfsu/Global/INGAMEB.BUN` | 1670080 |
   | `Tracks/STREAML1RA.BUN` | `sdmc:/nfsu/Tracks/STREAML1RA.BUN` | 92394688 |
   | `Tracks/STREAML1RA_1003.BUN` | `sdmc:/nfsu/Tracks/STREAML1RA_1003.BUN` | 0 |
   | `Tracks/TEX1003TRACK.BIN` | `sdmc:/nfsu/Tracks/TEX1003TRACK.BIN` | 1086216 |
   | `Tracks/TRACKB1003.lzc` | `sdmc:/nfsu/Tracks/TRACKB1003.lzc` | 433352 |

   `Tracks/STREAML1RA_1003.BUN` is empty in the retail game and is copied
   anyway: the game names it as the track's pack file, and the check
   requires it, so that the card holds every file the game names for this
   track. Do not copy
   `Global/GlobalB.lzc` or `Global/GLOBALA.BUN` in their place or beside
   them: the port reads neither, and the owner's copies of both are 2025
   modifications (see "Two modified files" below).
3. Optionally, with the card still in a card reader on the PC:
   `nfsu_probe asset-check <card drive>:/nfsu` prints the same report.
4. Put `build-switch/switch/nfsu_switch_assets.nro` on the card under
   `sdmc:/switch/`, or send it with `nxlink -a <console address>` from the
   homebrew menu's netloader, and start it. Photograph the screen.

## What nfsu_switch_assets shows

It draws its report with the project's own font into a 1280x720 framebuffer,
at the text scale of issue #141's application: 106 columns and 40 lines.
While the check runs, the bottom line names the file being checked and, for
a large one, how far through it the hash is. Then the screen holds, from the
top:

```text
nfsu_switch_assets: the retail files the first lap needs, on this SD card (issue #47)
config: sdmc:/config/nfsu_switch/assets.cfg not found; root sdmc:/nfsu (the default)
set first-lap: one timed lap of event 1 in the stock 240SX (issue #159), 12 files, 108907646 bytes
<12 lines, one per file>
install: <VERDICT> - <n> of 12 files ok: <n> missing, <n> wrong size, <n> wrong version, <n> unreadable
copy these <n> files from the game's installation folder, keeping the folders:
  <path> -> sdmc:/nfsu/<path>
read <bytes> bytes in <n> reads and <n> directory listings
checked in <seconds> s
Press + to exit.
```

A complete card says `install: COMPLETE - all 12 files match by name, size
and SHA-256` in green and `nothing to copy`. Each file line starts with its
status - `ok` in green, `MISSING`, `WRONG SIZE`, `WRONG VERSION`,
`UNREADABLE` or `AMBIGUOUS` in red - then the file's path and what was found:

```text
ok            Cars/240SX/GEOMETRY.BIN  4780816 bytes, sha256 matches
MISSING       Tracks/STREAML1RA.BUN  no Tracks in the root
WRONG SIZE    Tracks/STREAML1RA.BUN  52428800 bytes, expected 92394688: shorter than the retail file: an interrupted copy?
WRONG VERSION Global/GLOBALB.BUN  2361176 bytes, sha256 0123456789ABCDEF... expected 39DF38FAC2BE7CA0...
```

and a file found under another case adds `(found as <path on the card>)`.

On the owner's card as it is today - the six stock-car files and nothing
else - the code predicts the six stock-car lines `ok` (if those copies are
the retail files, which issue #141's `EQUAL` frame makes likely), then
`MISSING` for `FrontEnd/FrontB.lzc` (`no FrontEnd in the root`),
`Global/INGAMEB.BUN` (`no INGAMEB.BUN in Global`) and the four track files
(`no Tracks in the root`), the verdict `install: MISSING FILES - 6 of 12
files ok: 6 missing, 0 wrong size, 0 wrong version, 0 unreadable`, and six
copy lines. That is what the code and the desktop tests say; it has not been
seen on a console.

`+` exits, and so does a request from the system to close. The application
opens every file for reading only and writes nothing, on the card or
anywhere else.

## The manifest

`nfsu::retail_asset_manifest` (`include/nfsu/assets.h`,
`src/assets.cpp`) holds three sets. `first-lap` is the default and is the
other two together; `stock-car` is exactly the six files
`stock_frame_file_path` names for the stock-car application, and
`event-1` is what the game loads for event 1.

| Path | Bytes | SHA-256 | Sets | What reads it |
| --- | ---: | --- | --- | --- |
| `Cars/240SX/GEOMETRY.BIN` | 4780816 | `002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619` | first-lap, stock-car | the 240SX's meshes |
| `Cars/240SX/TEXTURES.BIN` | 697340 | `725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA` | first-lap, stock-car | the 240SX's textures and its doorline skin layer |
| `Cars/BRAKES/GEOMETRY.BIN` | 69408 | `FEC4CBF4F9C3E7FB1CD883F0B874F920248652A2E63EE4A341446563F0B9B1A5` | first-lap, stock-car | the brake meshes the stock car places |
| `Cars/PLATES/GEOMETRY.BIN` | 1184 | `0EBCFD14DD303613F5DA3FF5D7A6DF57BE3CDE915F458BA243FE5D089B64BD8A` | first-lap, stock-car | the licence plate mesh |
| `Cars/TEXTURES.BIN` | 1230808 | `999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58` | first-lap, stock-car | the shared car textures, with the skin target `DUMMY_SKIN1` |
| `FrontEnd/FrontB.lzc` | 4182578 | `76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90` | first-lap, event-1 | the career event table: event 1's track, laps and direction ([`RACE_EVENTS.md`](RACE_EVENTS.md)) |
| `Global/GLOBALB.BUN` | 2361176 | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` | first-lap, stock-car, event-1 | the car part database, the car and track tables, shared textures |
| `Global/INGAMEB.BUN` | 1670080 | `044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299` | first-lap, event-1 | the in-race textures the track draws with ([`TRACK_WHOLE.md`](TRACK_WHOLE.md)) |
| `Tracks/STREAML1RA.BUN` | 92394688 | `2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E` | first-lap, event-1 | the `L1RA` stream: every section of track 1003 ([`TRACK_WHOLE.md`](TRACK_WHOLE.md), [`TRACK_STREAMING.md`](TRACK_STREAMING.md)) |
| `Tracks/STREAML1RA_1003.BUN` | 0 | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` | first-lap, event-1 | track 1003's stream pack: empty in the retail game, which names it |
| `Tracks/TEX1003TRACK.BIN` | 1086216 | `081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5` | first-lap, event-1 | track 1003's sky and track map textures |
| `Tracks/TRACKB1003.lzc` | 433352 | `ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA` | first-lap, event-1 | track 1003's streaming table, zones, route and collision ([`TRACK_COLLISION.md`](TRACK_COLLISION.md)) |

`stock-car` is 9140732 bytes and `event-1` 102128090; `GLOBALB.BUN` is in
both, so `first-lap` is 12 files, not 13.

**Where the numbers come from.** Each size and hash was read from the
owner's retail installation, strictly read-only (`stat -c %s` and
`sha256sum`, with the installation's file list, sizes and modification
times recorded before the first read and after the last and found
identical). All twelve files are dated 2003 there. The hashes agree with
the ones the notes already pin for the same files - `RACE_EVENTS.md`,
`TRACK_WHOLE.md`, `TRACK_STREAMING.md`, `FORMATS.md`, `RENDERER.md`,
`BODY_SKIN.md` - except `BRAKES/GEOMETRY.BIN` and `PLATES/GEOMETRY.BIN`,
which no note hashed before. `tests/asset_tests.cpp` pins this table entry
by entry; a change to one has to be made to the other.

**Which spelling.** Each path is spelled as the installation spells it on
disk, which is what the owner sees when copying: `Tracks/TRACKB1003.lzc`
and `FrontEnd/FrontB.lzc`, although the game formats `TRACKB%d.LZC` and
`FRONTEND\FRONTB.LZC`. The naming rule below is what makes either spelling
find the file.

**Which files the game loads that the set leaves out.** The game loads
`GLOBAL\GLOBALB.LZC` and `GLOBAL\INGAMEB.LZC`, JDLZ blobs; the port reads
`GLOBALB.BUN` and `INGAMEB.BUN`, the retail images the installation carries
beside them, which the notes above show to be byte for byte their
decompressions. It also loads `GLOBAL\GLOBALA.BUN` and `GLOBAL\INGAMEA.BUN`
(empty), which nothing in the port reads, and each car's `VINYLS.BIN`, which
a stock car does not need ([`SWITCH_BUILD.md`](SWITCH_BUILD.md)). Sound,
the HUD, the front end and opponents are later work (#56) and not in any set
yet.

### Two modified files

The owner's installation carries two 2025 modifications in `Global/`, with
their 2003 originals kept beside them under a `.bacc` suffix:

| File | Bytes | SHA-256 | Dated |
| --- | ---: | --- | --- |
| `Global/GlobalB.lzc` (modified, stored uncompressed) | 2610196 | `B4E06F7EDC933C3DCAD2E607E04184B45B010281C9BE71C5E98CA4198F31756B` | 2025 |
| `Global/GLOBALB.LZC.bacc` (the 2003 original, JDLZ) | 972201 | `DCFEA6677B0893B40937C886CF2FD953FBDD12CC878FB8FF9DD228FF3A9F9236` | 2003 |
| `Global/GLOBALA.BUN` (modified) | 147844 | `1502D9305F37E51503D4C4E57CAE32890059497AC450FEEE6D72B50323568953` | 2025 |
| `Global/GLOBALA.BUN.bacc` (the 2003 original) | 147456 | `592FD858C0757733D59CADB59FBFE52FF0F1AEFD142274A5AF54A4C85659C69B` | 2003 |

The port needs neither file. Its global data is `Global/GLOBALB.BUN`, the
2003 retail image of `GLOBALB.LZC` - the decompression of the `.bacc`,
byte for byte ([`RACE_EVENTS.md`](RACE_EVENTS.md)) - and nothing in the port
reads `GLOBALA.BUN`. The check does not look at either, and a card holding
them is not reported for it.

## The naming rule

Two names match when they are the same length and equal byte for byte once
`A` to `Z` are folded to `a` to `z`. No other byte is folded, so a name
holding anything outside ASCII matches only itself. For ASCII names it is
the rule Windows gives the game, which formats its names in upper case and finds them in
whatever case the installer wrote them, and the check applies it itself on
every platform, one path component at a time from the root, so that the
desktop - whose file system may be case-sensitive - and the console's SD
card find the same files. The check reports where it found each file when
the spelling differs, as `(found as tracks/trackb1003.LZC)`.

Two names in one directory that both match - possible only on a
case-sensitive file system - are not guessed between: the file is
`AMBIGUOUS` and neither is read. The config file is found by the same rule.

## The check

`nfsu::check_asset_install` takes a `FileSystemService`, a root and a
manifest, and for each file in manifest order:

1. finds it by the naming rule, listing each directory at most once per
   check, and refusing a directory that holds more than 4096 names rather
   than searching part of it;
2. sizes it, and if the size is not the manifest's stops there: `WRONG
   SIZE`, with whether the file is shorter - an interrupted copy - or
   longer. A file of the wrong size is never read;
3. reads it from its first byte to its last in chunks of at most 1 MiB into
   one buffer allocated once, hashing as it goes, and then asks for one
   byte past the end, which must not be there;
4. compares the SHA-256: `ok` or `WRONG VERSION`.

A file that cannot be sized or read, is a directory, or is shorter or longer
than it was sized at while it is read is `UNREADABLE`, with the service's
reason. So the most a file ever costs is its expected size and one byte, and
the check reads nothing a manifest does not name. It returns the per-file
report and a verdict for the install, the first of these that applies:

| Verdict | When |
| --- | --- |
| `complete` | every file `ok` |
| `missing files` | any file missing |
| `partial` | none missing, any of the wrong size |
| `wrong version` | none missing or of the wrong size, any with another SHA-256 |
| `unreadable` | the rest: any unreadable or ambiguous |

A root that cannot be listed makes every file missing and says why once. A
manifest the check cannot run - no name, no files or more than 64, a path
that is not relative printable ASCII with `/` separators, a size over 256
MiB, a hash that is not 64 upper-case hexadecimal digits, two paths that
name one file - is refused before anything is listed.

## The config file

`sdmc:/config/nfsu_switch/assets.cfg`, if it exists, moves the root. Without
it the root is `sdmc:/nfsu`. The format, all of it:

```text
# Where the Need for Speed: Underground files are on this SD card.
root = sdmc:/games/nfsu
```

- At most 4096 bytes, read in one bounded read. Every byte is printable
  ASCII (`0x20` to `0x7E`), a tab, or a line end. A leading UTF-8 byte order
  mark is refused as such.
- Lines end with LF or CR LF. A blank line, or one whose first character
  other than spaces and tabs is `#`, is ignored.
- Every other line is `key = value`, spaces and tabs around either ignored.
  The only key is `root`, and it is given exactly once.
- The root is absolute on the SD card: `sdmc:/` and then folder names
  separated by `/`, none empty, `.` or `..`, and none holding `"`, `*`, `:`,
  `<`, `>`, `?`, `|` or a backslash; at most 200 bytes. A trailing `/` is
  dropped. `sdmc:/` alone, the card's top level, is allowed.

A config file that is present and breaks any of these, is larger, cannot be
read, or has two spellings in its folder, is **refused, not ignored**: the
application shows `config: <file> REFUSED: <what is wrong, with the line
number>`, says to fix or delete it, checks nothing, and lists the files the
set needs under the root. A root that parses and does not exist is applied,
and the check then says `root <root> cannot be listed: it does not exist`,
with every file missing.

The config file is read by `nfsu_switch_assets`. Issue #141's
`nfsu_switch_car` still takes its root as an argument, as before; the lap
application of #159 is where the config file is meant to be read next.

## On the desktop: nfsu_probe asset-check

```text
nfsu_probe asset-check <root> [--set <first-lap|stock-car|event-1>]
```

It runs the same check through the desktop file system service and prints
`asset-check: root <root>` followed by the report lines above, with every
hash in full. It exits 0 when every file matches, 2 when the root cannot be
listed, 3 for any other install, and 1 for a usage error: a missing root, a
second positional, an option that is not `--set` or a set that is not one.
The root goes through the path-argument conversion of
[`PATH_ARGUMENTS.md`](PATH_ARGUMENTS.md), so a root under a Cyrillic folder
reads as any other.

Over the owner's retail installation, read-only, the command exits 0 and
prints:

```text
asset-check: root <the installation>
set first-lap: one timed lap of event 1 in the stock 240SX (issue #159), 12 files, 108907646 bytes
ok            Cars/240SX/GEOMETRY.BIN  4780816 bytes, sha256 002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619
ok            Cars/240SX/TEXTURES.BIN  697340 bytes, sha256 725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA
ok            Cars/BRAKES/GEOMETRY.BIN  69408 bytes, sha256 FEC4CBF4F9C3E7FB1CD883F0B874F920248652A2E63EE4A341446563F0B9B1A5
ok            Cars/PLATES/GEOMETRY.BIN  1184 bytes, sha256 0EBCFD14DD303613F5DA3FF5D7A6DF57BE3CDE915F458BA243FE5D089B64BD8A
ok            Cars/TEXTURES.BIN  1230808 bytes, sha256 999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58
ok            FrontEnd/FrontB.lzc  4182578 bytes, sha256 76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90
ok            Global/GLOBALB.BUN  2361176 bytes, sha256 39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9
ok            Global/INGAMEB.BUN  1670080 bytes, sha256 044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299
ok            Tracks/STREAML1RA.BUN  92394688 bytes, sha256 2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E
ok            Tracks/STREAML1RA_1003.BUN  0 bytes, sha256 E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855
ok            Tracks/TEX1003TRACK.BIN  1086216 bytes, sha256 081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5
ok            Tracks/TRACKB1003.lzc  433352 bytes, sha256 ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA
install: COMPLETE - all 12 files match by name, size and SHA-256
nothing to copy
read 108907646 bytes in 123 reads and 8 directory listings
```

Everything after the first line - 16 lines, 1720 bytes with CR LF folded to
LF - has SHA-256
`8978B84F37FF895C3E699CB796CEAAC0C325D6CEB23BCD8BC2AF1BC2993B77F4`, from the
default build and from the Release build alike. `--set stock-car` exits 0 with 10 such lines
(954 bytes, `3BEFD53897776432CD19E43F19815E2D199E9696BD2B2D88AFEEC352511C5FDB`,
9140732 bytes in 19 reads and 6 listings) and `--set event-1` with 11 (1100
bytes, `D3B8D10578C26D9C0AC842E733C306BD01F82014C585D9CFE4686575ADEF62A2`,
102128090 bytes in 108 reads and 4 listings). The 123 reads are 111 chunks
of at most 1 MiB and one probe past the end of each of the 12 files; the 8
listings are the root, `Cars`, `Cars/240SX`, `Cars/BRAKES`, `Cars/PLATES`,
`FrontEnd`, `Global` and `Tracks`.

## The file system service

The check reaches files only through three read-only calls
`include/nfsu/platform.h` added to `FileSystemService` for this issue -
`list_directory`, bounded by a name count; `file_size`; and `read_file`, a
bounded read of one byte range that opens the file for reading and closes
it again. [`PLATFORM_SERVICES.md`](PLATFORM_SERVICES.md) states the
contract. Three implementations answer them:

| Service | Where | How |
| --- | --- | --- |
| desktop | `desktop_file_system()`, `src/arg_path.cpp` | `std::filesystem` and `std::ifstream` over the wide paths the argument conversion builds |
| console | `posix_file_system()`, `src/posix_file_system.cpp` | newlib's `opendir`, `readdir`, `closedir` and `stat`, which libnx routes to the SD card, and `std::ifstream`, which issue #141's application already reads the card with |
| fake | `FakeFileSystem`, `src/platform_fakes.cpp` | an in-memory tree that records every listing, sizing and read |

`nfsu_assets` itself is neutral - the neutrality gate reads it - because it
reaches no file but through the service it is handed.
`nfsu_posix_file_system` is platform code on purpose. It is also built on
the desktop, where MinGW supplies the same calls, so that the tests run it
over real directories; there it is only given ASCII relative paths.

The parsers still open their inputs with `std::ifstream` themselves, as
[`PLATFORM_SERVICES.md`](PLATFORM_SERVICES.md) records. This issue adds the
calls a loader on the console needs - a bounded range read is what the
section streamer of issue #157 asks of `STREAML1RA.BUN` - and uses them for
the check; moving the loaders onto them is the lap application's work.

## Tests

- `nfsu_asset_tests`: the three sets against the pinned table, against the
  six files `stock_frame_file_path` names and against the names
  `nfsu_race_events` formats for track 1003 (`TRACKB1003.LZC` found as
  `Tracks/TRACKB1003.lzc`); the naming rule; 17 manifest refusals; over the
  fake, a complete install with every read's offset and size, a missing
  file and a missing folder, a file cut short and one too long, one bit
  changed in a 2.5 MiB file, every name in another case, two spellings of
  one file and of one folder, a folder that will not list, a read that fails
  part way, a folder in a file's place, a file that grows and one that
  shrinks while it is read, an absent root, a root that is a file, 4096 and
  4097 names in a folder, and the verdict order; 5 config files accepted and
  22 refused by line and reason; the config file absent, applied, found in
  another case, refused for its bytes, for being 4097 bytes (and unread),
  for being a folder, unreadable, changed while read or spelled twice; the
  console's whole report for a complete card, a moved root, a root that is
  not there, a refused config, and the first-lap set on an empty card; and
  over real directories under the build tree, six installs through the
  desktop and the console services, which must agree, plus each of the
  three calls directly.
- `nfsu_probe_asset_check_guards`: the command's usage errors, a root that
  is not there, a file as the root, an empty root, a mixed first-lap install
  written by the script - one file right, two of the right size and the
  wrong bytes, one cut short, the rest missing, names in other cases - with
  the root given absolute and relative, and a stock-car install of the right
  sizes and the wrong bytes, then with one file cut short and one replaced
  by a folder; every exit status, and that the command wrote nothing. No
  case reaches exit 0, which needs every file of a set to be the retail
  file; the retail report above is that case.
- `nfsu_path_arguments` drives a Cyrillic path through the command's root
  argument.
- `nfsu_switch_assets_nro`, in the Switch tree: the NRO and its NACP.

Each test was made to fail before it was trusted: removing the case fold,
checking a file whose size is wrong, dropping the probe past the end,
letting the config parser accept any byte but NUL, exiting 0 for every
verdict, listing `.` and `..` on the console and dropping the desktop
listing bound each failed `nfsu_asset_tests` or the guards, and the tree
passed again once each was restored.

## What is not known

- **Nothing here has run on a console.** `nfsu_switch_assets` is built with
  no warning and checked as a file; what it shows is what the code and the
  desktop tests say. How long the check takes there - 104 MiB read from the
  card and hashed by the CPU - is unmeasured, and so is the SD card's speed.
- **How the console's calls behave on the card itself.** What was checked
  is the linked code: the NRO's ELF holds libnx's SD card device
  (`fsdevMountSdmc`, `fsdev_diropen`, `fsdev_dirnext`, `fsdev_stat`) and
  newlib's `opendir`, `readdir`, `closedir` and `stat`, and the disassembly
  of its `readdir_r` turns the device's `ENOENT` at the end of a listing
  into a null entry with `errno` set back to 0 - the POSIX end, which the
  console service reads as the end (it would read `ENOENT` as the end too).
  None of it has been run against a card.
- **The SD card's case.** FAT32 and exFAT keep a name's case and ignore it
  when opening, so the naming rule should find nothing the card would not;
  that is not observed either.
- **The set will grow.** It is what the port reads today for the first lap:
  the car, the track, its textures and collision, and the event table. The
  vehicle model's handling data is in `GLOBALB.BUN` already; sound, the HUD
  and opponents (#56) will add files.
- **`STREAML1RA_1003.BUN`** is empty and required. The game formats its
  name as the track's pack file (0x0044972C) and its streaming table
  declares it 0 bytes; whether the port's loader will open it at all is
  that loader's decision ([`TRACK_WHOLE.md`](TRACK_WHOLE.md)).
- **Nothing checks a file at load time.** A loader trusts what is on the
  card; this check is run before, by hand.
