# Upstream lap HUD presentation producers

Research for [issue #254](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254),
following [CHECKPOINT7368](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254#issuecomment-7368).
The assigned development base is `798bd8e6570b8e84043a3c4577b01da34b057bff`
from [PR282](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/282).
This extends [HUD_254.md](HUD_254.md) and the
[gated consumer](HUD_254_IMPLEMENTATION.md). It changes no C++ or gameplay
state and does not make `presentation_ready` true.

## Evidence boundary

`tools/hud254_presentation.py` executes pinned original x86 bytes in Unicorn
and decodes named instruction ranges with Capstone. It never launches the
game. Real package fields, font records and texture metadata feed explicitly
constructed runtime objects. Allocation, texture resolution, localization,
render-state calls and COM calls are bounded host stubs, enumerated in the
report. The font loader, font lookup, text layout, glyph geometry, transforms,
quad submission and NDC conversion themselves execute original instructions.

The report records input SHA-256s, source commit, script hashes, instruction
range hashes, native submissions and complete before/after manifests of every
file and directory under retail and reference. Failed probes still write the
after manifests. Output must be fresh, ASCII-named and outside both inputs.
No image pixels, retail files or generated binary output enter the repository.

The existing executable, INGAMEB and English identities remain pinned by
`hud254_trace.py`. Two additional input identities are required:

| Input | SHA-256 |
| --- | --- |
| `Global/GLOBALB.BUN` | `39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9` |
| `Languages/LanguageTextures.bin` | `6A92604D9766557115DEE8F8CE18F392557AF1A8B0369E69B474F36DF2691615` |

These identify the inspected installation files, not an assertion that every
file in that installation is stock. Texture metadata records are separately
identified by source file, offset and hash. Expected measurements belong in
`hud254_presentation_test.py`, not in this document.

## Language and time formatting

`0x6EE6F0` is the selected language, not an independent time-format preference.
The table at `0x6EE650` contains ID, language name, file and handler records.
The registry-read context `0x40132B..0x40139B` matches the `Language` string
against a table and writes `0x70430C`; a failed query selects English. The
bootstrap context `0x448794..0x4487A9` substitutes English for the unselected
sentinel before calling the setter `0x59FA20..0x59FB30`. The setter writes
`0x6EE6F0` at `0x59FAC5` and triggers language resource and frontend updates.

The native case runs that setter with an initially unselected language and no
frontend object. Resource loading and notification are explicit stubs. It then
calls the existing native formatter with the resulting global. The language
matrix in `test_language_producer_selects_formatter` pins the distinct
separators. The earlier forced Dutch selector produced a valid string but did
not establish which language event0 actually selected. An eventual adapter
must receive a known selected language and preserve an unknown state otherwise.
This research does not read or infer the owner's live registry or profile.

## Permission initialization, copy and updates

`0x432890..0x432902` initializes one view-settings record, including its state
byte and permission flags. Race-settings initialization at `0x431DF0` calls
it for the view records. The player-zero profile copy at
`0x4B47A0..0x4B48E2` sets or clears permission `0x100` according to whether
profile byte `0x7588DA` is exactly one. Other profile options populate their
own permission bits. The career builder's context `0x4B602B..0x4B604F`
calls this copy with player zero.

The view constructor context `0x42B1C2..0x42B245` selects the player's settings
record and calls `0x42BAC0..0x42BB5E`. That routine copies the settings into
`view+0x20`, placing permissions at `view+0x30`. The native cases execute
initializer, profile copy and view copy, then pass the copied state into the
existing conditional mask oracle. The latter retains all of its explicitly
declared predicate stubs. An initialized permissive bit is not proof of the
value after the active profile is copied.

The in-race option-handler slice `0x4BFD90..0x4BFE1B` toggles the lap bit in
both view and race settings and writes it back to the selected player's profile.
The synthetic test selects player zero, executes this slice twice, and stops
before the unrelated menu refresh. It verifies the round trip and preservation
of the other flags. This is a bounded handler test, not a running menu test.
The per-view context `0x430DBC..0x430DF2` recomputes the mask via `0x42E620`,
compares the prior mask, updates visibility via `0x42ED40` when changed, and
dispatches component updates via `0x4A9440`.

Still missing: the actual new-career/loaded-profile value of `0x7588DA`, the
complete event0 path that reaches these view records, and movie, pause, retry
and finish lifecycle captures. The prior stage matrix alone cannot close them.

## Font loading and Sj

The package's OP/RsRq resource requests resolve to the English font records.
`0x4F44C0..0x4F46AA` initializes the runtime font from its FNTF chunk, records
the texture hash, derives line height, and applies a hash-keyed override table
at `0x6F9498`. The report exports that table. The exercised English fonts do
not hit those overrides. The optional FNTF callback table is explicitly null
in these cases; texture lookup returns the corresponding synthetic texture
object populated from the real metadata.

`0x4F43D0..0x4F441C` searches the supplied font registry before trying the
single fallback mapping at `0x6F9490`. The probe registers the loaded English
font and executes the real search; it does not replace font lookup with a
constant pointer. This proves the direct matching path, not the fallback path.
Text submission at `0x4F25F0` also contains a language-dependent scale branch
for Korean/Chinese and the large Conduit request. Those languages are outside
the English geometry cases. Later runtime font replacement has not been ruled
out, and default package bindings remain conditional for a production adapter.

`0x501AC0..0x501B77` transfers Sj, Sl and Sw into the runtime object. The
layout at `0x4F48B0` uses Sj mask `0x01` for horizontal centering, otherwise
`0x02` for right alignment; `0x04` vertically centers, otherwise `0x08`
bottom-aligns.
The first bit takes precedence when both bits of a pair are present. Width
and height come from original routines `0x4F4D00` and `0x4F4F10`.

The numeric tests independently calculate advances and signed bearings from
the parsed FNTF records, then compare every native glyph corner and UV. They
exercise both axes and overlapping Sj flags. Original `0x4F4790` applies the
runtime baseline, a minimum glyph width and the atlas border; the submitted
UV then receives the separate half-texel adjustment described below. General
kerning, wrapping, arbitrary Unicode and all font override branches are not
established by these single-line numeric fixtures. Static-label strings in the
geometry cases are supplied inputs, not newly proved localization results.

## Transform, backing and submission order

`0x4F1810..0x4F1D76` combines local SA and an optional indexed parent matrix.
Parent color multiplication also occurs here. Text and sprite submission add
the logical canvas-center translation after the parent transform. The probe
uses identity parent unless a case explicitly supplies a scaled/translated
parent and color. The independent test distinguishes parent-before-center
from center-before-parent. No complete package hierarchy is fabricated.

`0x4F2000..0x4F2278` builds the backing sprites from a centered unit quad,
SA dimensions and UV bounds. The real backing texture requests resolve to
`3RDPERSON_BOXCORNER` in INGAMEB and `WHITE16X16` in GLOBALB. The similarly
named pure-white/no-alpha textures are different resources. Font texture
dimensions come from LanguageTextures, not the font chunk or a guessed atlas.

`0x514530..0x5146CA` transforms vertices and replaces their Z with the supplied
quad depth. `0x514B90..0x514BE7` submits the descriptor list. The regular path
through `0x40BEF0..0x40C2F4` converts logical X/Y to normalized coordinates,
swizzles the packed vertex color, and adjusts UV by half a texel using the
resolved dimensions. Native DrawPrimitiveUP arguments and every vertex are
captured. `test_final_vertices_material_and_half_texel_inset` checks these
relations and the triangle-fan submission contract, including every glyph.

Original `0x40F3D0..0x40F6DB` processes the real texture alpha-test/blend
metadata under a supplied shader-context type. Render-state calls are captured
stubs. This proves their requested factors and test enable state for that
context, not final shader texture-times-color behavior, pixel alpha, filtering
or an image match. The packed FEng colors have already been halved before
submission; treating those bytes as final pixel opacity would overclaim.

`0x4F2FD0..0x4F3024` truncates the SA depth plus optional parent depth to an
integer, clamps the bucket index and appends through `0x4A9900`.
`0x4F2EB0..0x4F2FC2` drains high buckets first, retaining insertion order within
one bucket. Native cases distinguish truncation, clamping and stable ties.
`0x4F5640..0x4F565C` gates enqueue on object visibility/callback state. Under
unchanged SA depths and a shared parent, the backing precedes the text. This
does not prove actual package traversal order between equal-depth backings or
all animation-driven depth/visibility changes.

## Next implementation boundary

The recovered contract supports explicit language-to-format mapping, profile
permission propagation, Sj alignment, conditional English font metrics and
logical quad construction. Each must retain its known/unknown distinction.
No always-visible lap HUD, screenshot-derived scale or opaque rectangle is
authorized by this evidence.

The next isolated adapter can take these explicit inputs and expose outputs
without opening the production drawing gate:

| Callable boundary | Inputs | Output and remaining condition |
| --- | --- | --- |
| `format_lap_hud_ticks` / `build_race_lap_hud_panel` | Known selected language and existing timing snapshot | English selects `native_fallback`; recovered IDs 1 through 6 select their matching `nativeN`. An unavailable selection remains `unknown`. |
| Permission adapter | Known player profile option, race/view state and established mask predicates | Copied lap permission and conditional component mask; the active event0 profile and lifecycle are still required. |
| Future logical geometry builder | Bound English font/FNTF, actual texture dimensions, text, Sj/Sl/Sw, SA, effective parent | Glyph/sprite quads, packed colors and ordered buckets, comparable to `presentation.json`; parent/font state must be supplied rather than assumed. |
| Future draw backend | Ordered logical geometry plus proven viewport, shader and material | Raster output; the last three inputs and original runtime comparison remain missing. |

Before enabling production presentation, recover the event0 profile/lifecycle,
effective runtime font/hierarchy state, viewport and safe-area setup, actual
shader/material behavior and package traversal. A real original clip/runtime
capture is still absent. Keep `presentation_ready` false until the remaining
producers are independently reviewed and the integrated renderer is validated.

## Reproduction

Use Python 3.8+ with Capstone 5.0.7 and Unicorn 2.1.4. From the actual worktree,
derive any non-ASCII workspace/reference location through runtime path APIs;
do not embed it into automation. Use new output directories on every run:

```text
python -B tools/hud254_presentation.py --root <retail-root> --reference <reference-root> --source <full-head> --out out/254-presentation-fresh
python -B tools/hud254_presentation_test.py --report out/254-presentation-fresh --source <full-head>
python -B tools/hud254_trace.py --root <retail-root> --reference <reference-root> --source <full-head> --native --out out/254-consumer-fresh
python -B tools/hud254_trace_test.py --report out/254-consumer-fresh
```

The new test runner requires a report and expected source SHA, rejects stale
tool hashes, and has no mode that skips native-report validation. These probes
are research validation, not C++/Release/retail CTest or Switch acceptance.
