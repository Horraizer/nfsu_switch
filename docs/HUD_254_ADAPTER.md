# Conditional lap HUD presentation adapter

Executable plumbing for [#254](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254),
assigned by [comment8229](https://git.accad.pro/murzintsevav/nfsu-switch/issues/254#issuecomment-8229).
The bounded delta extends reviewed PR290 at
`8d17b4e2b5ffe9ef31645365cd9b0771c36118b0`, including PR282's typed consumer.
Its main-target diff also contains that inherited substrate; this adapter does
not author or re-accept the inherited renderer changes.

`race_lap_hud_style` maps explicitly known selected language IDs from the
recovered setter to formatter styles. The language overload of
`build_race_lap_hud_panel` consumes the existing timing snapshot. Unknown,
unselected and unproved languages remain unknown. The production caller still
has no established live language/profile and continues using unknown style.

`copy_lap_hud_permission` copies only the lap permission from an explicitly
known profile byte. Exactly one enables the bit; other values clear it. Other
profile options are separate producer inputs, and unrelated permission bits
are preserved. The view record is a value copy of these settings. The toggle
updates consistent known race/view lap bits and returns the profile byte;
unknown or inconsistent input is rejected without mutation. `lap_hud_visibility`
requires explicit knowledge of the non-permission mask predicates, including
lifecycle. It returns only the lap component, not a fabricated complete mask.
Neither initialization nor a profile default establishes event0 visibility.

`decode_lap_hud_fntf` decodes supplied glyph records with signed bearings and
advances, using the declared header+0x14 glyph-table offset. The entire table
must fit the supplied buffer and lie outside the header. Kerning storage may
precede a relocated glyph table but must not overlap it; truncated or invalid
offsets clear the font. Its texture binding and effective line metrics must be supplied
after font loading/override resolution. `build_lap_hud_text_geometry` takes
that font, actual texture dimensions, ASCII text, known Sj/Sl/Sw, local SA,
effective parent transform/color and known visibility. This bounded layout
supports single lines with Sl/Sw zero, baseline zero and line spacing one.
Other layout modes and Sj bits outside the recovered alignment nibble are
refused. Center Sj bits take precedence over the
right/bottom bits. Missing glyphs are refused except an absent padding space,
which the original LCD numeric fixture skips without advance or a quad.
The original routine treats caret/dollar as control markers; they are refused.
The FNTF per-glyph pair count/index table is decoded using `0x6387F0`'s
record layout. A selected pair with nonzero adjustment is refused: this
bounded adapter does not silently draw unkerned text or claim general kerning
support. Missing pair bytes invalidate the font. The existing numeric and
label fixtures do not require nonzero adjustments. No font registry, fallback,
runtime replacement or localization is invented.

`build_lap_hud_backing_geometry` uses the same explicit SA/parent/texture and
visibility inputs and the supplied sprite SA UV bounds. It constructs the
centered unit quad, applies the native next-power-of-two UV ratio and never
substitutes an opaque rectangle or chooses a backing resource. Local SA here
supports scale and translation; an explicitly supplied SA quaternion must
establish that local rotation is absent. Other quaternions are refused.
Effective parent supports affine 2D, with
perspective and 3D inputs rejected. The parent transform precedes the native
logical canvas center. Logical quad vertices retain the recovered z=1;
the queue bucket adds binary32 SA and parent depth at the original x87
64-bit significand precision, then truncates to a signed integer and clamps.
Wider long-double targets explicitly round the sum to that precision with
nearest-even ties. Effective sums outside [-2147483648,2147483648) are refused
instead of treating the native indefinite conversion result as a high bucket.

Vertices expose logical positions, atlas UV, separately inset half-texel UV,
and packed submission ARGB after parent channel multiplication and halving.
The color is not a measured pixel opacity. NDC/raster conversion, actual
shader/material behavior, viewport and safe area are outside this adapter.
The stable descending bucket order preserves explicit caller traversal for
ties; it does not recover package traversal. Unknown inputs clear outputs and
return failure; known hidden objects have known empty geometry. Both panel and
geometry keep `presentation_ready` false, and no production draw call uses
these outputs.

## Validation and reproduction

The compiled `nfsu_race_lap_hud_presentation_tests` checks supported synthetic
inputs, stale-output clearing, unsupported/unknown inputs and immutable
permission failures. `tools/hud254_adapter_test.py` validates a fresh original
report with the existing independent presentation tests before comparing the
compiled adapter against every original glyph/backing vertex, packed color,
UV/inset, language setter/formatter, lap permission/toggle and depth queue
fixture. It feeds full FNTF records read from pinned English.bin rather than
the report's filtered glyph metadata. `hud254_adapter_regressions.py` also
executes original queue arithmetic/append/drain for parent-depth boundaries,
rounding ties, cancellation and signed-domain refusals. It relocates real FNTF
tables in synthetic memory, poisons obsolete records, and compares every
resulting vertex to original glyph lookup/layout. Invalid supplied-buffer
offsets and overlapping tables are adapter refusal tests, not a claim that
the original bounds-checks a buffer size it does not receive. Full retail and
reference manifests bracket these extra executions. Expected measurements live in tests.

From the actual worktree, using fresh ignored output on each run:

```text
python -B tools/hud254_presentation.py --root <retail-root> --reference <reference-root> --source <full-head> --out out/254-adapter-fresh
python -B tools/hud254_adapter_test.py --root <retail-root> --reference <reference-root> --report out/254-adapter-fresh --source <full-head> --probe build/nfsu_race_lap_hud_presentation_tests.exe
python -B tools/hud254_adapter_test.py --root <retail-root> --reference <reference-root> --report out/254-adapter-fresh --source <full-head> --probe build-release/nfsu_race_lap_hud_presentation_tests.exe
```

Derive reference/workspace locations from runtime path APIs. Dependency
versions and full input manifests follow [the reviewed contract](HUD_254_PRESENTATION.md).
The worker handoff records default/Release retail CTest and Switch gates for
the combined candidate. These probes execute original instructions with
declared stubs and synthetic runtime inputs; they are not original footage,
live event0 lifecycle, final material/pixel acceptance or a console run.

Production still requires known event0 profile/lifecycle, effective font and
hierarchy, viewport/safe area, shader/material and package traversal plus
independent original/runtime and hardware comparison. #253, #266 and #269
retain gauge, map and results ownership respectively. This work does not
activate drawing, close #254, merge or deploy.
