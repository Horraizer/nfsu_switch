# Stock 240SX texture bindings (issue #223)

The retail `speed.exe` uses texture substitution entries in `CarRenderInfo`.
The stock 240SX mesh stores hashes for individual windows, while the
unbroken car resolves them to the shared `WINDOW` texture in
`Global/GLOBALB.BUN`. The unselected sponsor slots use `DEFAULTALPHA` and
the car draw skips them. The CPU viewer, Switch stock frame, and deko3d draw
commands now use the same prepared bindings.

## Retail trace

Addresses below are VAs in the retail executable. The findings were checked
with Capstone disassembly of local retail bytes; no retail bytes are tracked.

| VA | Observed operation |
| --- | --- |
| `0x0056A780..0x0056A7B2` | Initializes the window source hashes in `CarRenderInfo`. |
| `0x0056CA60..0x0056CAC3` | Reads six two-bit damage levels (ECX = 0x10..0x1A, stride 2; one per window), indexes the window target tables at `0x006F1B48` and `0x006F1B58`, and writes the substitution targets. Level zero selects `WINDOW` (`0x834B8017`); the alternate table selects `WINDOW2` (`0xECBB8329`). |
| `0x0053F540..0x0053F5CD` | Compares the mesh texture hash with substitution entries and replaces the texture pointer. Lookup uses `0x005461C0`. |
| `0x0056C3D0..0x0056C4B7` | Initializes the empty decal target from `DEFAULTALPHA` (`0x66994F4A`). |
| `0x0056C4C3..0x0056C6E0` | Hashes `DUMMY_DECAL1..8` and initializes their substitution entries. |
| `0x0056C810..0x0056CA2D` | Replaces a dummy decal with a selected sponsor texture when customization supplies one. |
| `0x0040CA03..0x0040CA0C` | Skips drawing the `DEFAULTALPHA` material. |

The six unbroken stock window source hashes are `WINDOW_FRONT`
`0x7B220DDF`, `WINDOW_REAR` `0x1B0763A0`, `WINDOW_LEFT_FRONT`
`0xE7E4EF49`, `WINDOW_LEFT_REAR` `0x4CDEBFCA`, `WINDOW_RIGHT_FRONT`
`0x60F8B13C`, and `WINDOW_RIGHT_REAR` `0x0AB88F5D`. The retail 240SX LOD A
uses five of these. Damage variants remain unimplemented: the present stock
profile always represents damage level zero. `DUMMY_DECAL1..8` are
`0x910E6654..0x910E665B`; the retail 240SX LOD A uses the first six.

The actual `WINDOW` record is pack 0, record 39 in `GLOBALB.BUN`, 128x128
`A8R8G8B8`. The mesh window hashes have no direct record in the surveyed
car and global packs. `WINDOW_DAMAGE0..2` and `WINDOW2_DAMAGE0..2` are
present in `GLOBALB.BUN` for later damage support. A blank stock decal is
still reported as an unresolved source hash, but its draw is disabled as
the retail empty-slot path requires.

The stock race path and the viewer use the same stock frame preparation.
After the aliases, the complete painted unresolved set is the six disabled
hashes `0x910E6654..0x910E6659`. The unpainted path additionally has
`240SX_SKIN1` (`0x77A20245`), because the caller has not composed its
runtime body skin. The painted race and viewer paths compose that skin.
No submitted painted material uses a fallback binding.

The wheel mesh's three groups all resolve `240SX_TIRE` (`0x03A09493`),
car `TEXTURES.BIN` record 4. Its alpha-test mode is 1, so the retail
material state cuts the transparent corners from its inner backing quad.
The stock brake's two groups resolve `BRAKE_GLOBAL` (`0x17F9F794`) and
`BRAKE_STYLE00` (`0xC6578694`) from `GLOBALB.BUN`, pack 0 records 8 and
58. The brake draw at `0x0057049B`, `0x00570569`, `0x0057062B`, and
`0x00570702` points its left and right models at different substitution
tables. `0x0056A76B` initializes the `BRAKE_GLOBAL` source; at
`0x0056A7FE..0x0056A832` the left table maps it to
`BRAKE_GLOBAL_LEFT` (`0x85E9C79E`, pack 0 record 40) and the right table
leaves it unchanged. The stock scene now binds this per-side target for
left brake copies 0 and 3; CPU rasterization and deko3d command recording
consume the same texture handle. The wheel's rim material key
`0x22719FA9` is conditionally rebound from `CarRenderInfo + 0x4D8` at
`0x0056FDA3..0x0056FDDC`;
the selected stock pointer is not yet proven. This is an explicit limit
on the claim that the wheel face matches retail.

The global P8 palette conversion in `src/texture_decode.cpp` uses
`(byte 2, byte 1, byte 0, byte 3)` for RGBA. This matches the retail
palette unpack at `0x00407E70..0x00407EA8` and the prior pack correction
in PR #210. No palette change is made in this issue.

## Reproduction and result

Use the painted command in `docs/SWITCH_BUILD.md` with the default yaw 35,
then `--yaw -45` and `--yaw 145`. `nfsu_draw_commands_retail` holds the
default and orbit frame hashes, while `include/nfsu/stock_frame.h` holds
the default pin shared with the Switch CPU check. Issue #232 changed the
default to retail material state. Retail reads were bracketed by recursive
filename, size and SHA-256 manifests, with zero differences.

The default painted frame has no fallback triangles or pixels, checked by
`nfsu_draw_commands_retail`. The viewer's `groups_invalid` statistic combines
empty material slots and deliberately suppressed dummy-decal groups; it does
not mean every non-drawable group has an empty index range. The command above
prints the current counts.

The remaining unresolved hashes belong to disabled dummy decals. Window
checker patterns,
magenta strips, and the blank sponsor patterns are gone in all three views.
The viewer now selects retail material state for `--stock-car` by default.
Its alpha test removes the tire backing square at the default camera and
yaw 145; explicit
`--material-state off` retains the diagnostic old view. Whether the Switch
hardware output has the same result needs the requested console capture;
the wheel rim selection limit above also remains for that review.

The retail draw-command check compares CPU reference draws with the deko3d
command recorder, verifies placements, zero painted fallback pixels, and
both pinned default frames. It proves identical prepared material selection;
pixel agreement on the console still needs a console capture.

## GPU work and memory

At `329e13b5`, compared with `97e3839` using `nfsu_draw_commands_retail`,
the stock car submitted 20 fewer draws and 99 fewer triangles, with two more
upload resources (painted and unpainted). The added GLOBALB records are
WINDOW (128x128 A8R8G8B8, 87296 file payload bytes) and BRAKE_GLOBAL_LEFT
(128x128 DXT3, 21824 file payload bytes). Those pack spans include mip levels;
they are not allocated GPU byte counts. The current race car path calls the
same RGBA stock preparation as the CPU reference, so both added top levels occupy 65536 bytes each: 131072 bytes
before deko3d alignment, derived from their dimensions and decoded format.
This is larger than the compressed file spans. Existing shaders do the same
per-fragment work.
At `bd8aff1`, the review's painted default-camera retail-mode viewer run
reported 4 alpha-tested and 16 blended groups, with 25952 blended fragments;
the pre-fix car at `97e3839` had 9 blended groups
([PR #225, review comment 5290](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/225#issuecomment-5290)).
Resolving window textures therefore adds blending work even though blank
decal suppression removes draws. The net GPU-time change is unknown; more
blend fragments can increase color-buffer traffic and overdraw. A 60 fps
result still requires the scene to fit the 16.67 ms frame budget in both
handheld and docked modes.
Actual GPU milliseconds and aligned allocation cost need the paired console
measurement, not a claim of improvement inferred from fewer draws.

PR #225 review fixes derive CPU alpha/blend and coefficient state after
per-instance texture substitution, matching the recorder. A synthetic test
uses deliberately different alpha/blend modes for the base and left texture
so equality does not rely on the retail pair's matching metadata. A brake
mesh without a drawable BRAKE_GLOBAL group gets no left-texture override
or unused upload. `nfsu_stock_scene_tests` covers both cases. Hash constants
`kBrakeGlobalHash` and `kBrakeGlobalLeftHash` are shared by preparation,
validation and both render paths.
