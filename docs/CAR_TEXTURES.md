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
| `0x0056CA60..0x0056CAC3` | Reads four two-bit damage levels, indexes the window target tables at `0x006F1B48` and `0x006F1B58`, and writes the substitution targets. Level zero selects `WINDOW` (`0x834B8017`); the alternate table selects `WINDOW2` (`0xECBB8329`). |
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
then `--yaw -45` and `--yaw 145`. All outputs below are 1280x720 TGA files,
3686418 bytes each. Retail reads were bracketed by recursive filename,
size, and SHA-256 manifests: 476 files, zero differences.

| Yaw | Before SHA-256 | After SHA-256 |
| ---: | --- | --- |
| 35 | `8A5932A302A22AA3C4C01776009B5348629D8D37B3B61E6E0AA303235B1CF48F` | `05203E9CCDEB15B8ECF478B8501777577620DAB94C3FBA6A34F428BA772AF6E0` |
| -45 | `52F496BA4BBB7F520973152A0ECA454B10BA9047C629AE74892B9429DE97B8D7` | `17D783062F318FE0AC1B8D42D41665CE810A7FE39ADC9733612C199C18030E03` |
| 145 | `127C235F5E468E12F453AA9E85ABF809C95C62235D5BDFC1284DB86C6D9B9303` | `D3A4D72629888283C24D2788F81E865427248E01DFDCC3C0107C4073D5218D4B` |

The default painted frame changed from 233 fallback triangles and 33795
fallback pixels to zero of each. It records 74 owning draw commands, with
59 resolved material groups and 20 invalid empty slots. The six remaining
unresolved hashes are the disabled dummy decals. Window checker patterns,
magenta strips, and the blank sponsor patterns are gone in all three views.
The dark wheel backing visible in the viewer's default material mode is
unchanged. In `--material-state retail`, its alpha-test state removes the
square at yaw 145. Whether the Switch hardware output has the same result
needs the requested console capture; the wheel rim selection limit above
also remains for that review.

The retail draw-command check compares CPU reference draws with the deko3d
command recorder, verifies placements, zero painted fallback pixels, and
both pinned default frames. It proves identical prepared material selection;
pixel agreement on the console still needs a console capture.
