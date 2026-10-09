# Race lap/time UI implementation

- Owner assignment: pause optimization/effects; implement timer first, minimap
  later, sequentially in this chat. Original behavior requested. No reviewers.
- Base: c1570e0107612aae74ccc7d5d4f0f466c48e7e5f.
- Own tree: build/day20261008-root-combined; local work only, no push/merge.
- Timer uses committed RaceTimingState. No physics, input, time advancement,
  lap completion or pause code changes. Existing pure research API remains
  available without a bound presentation atlas.
- English FNTF glyph metrics and texture payloads are loaded read-only.
  The three lap fonts, authored backings and bindings feed the existing HUD.
  Glyph subsets fit unused atlas space, without increasing its dimensions.
- Explicit English presentation uses the native fallback time formatter.
  The pre-start localized placeholder is English.bin hash 0x61C9B7A2.
- The no-total circuit branch is at 0x4A1874..0x4A192D: mode 1, configured
  laps zero, predicate 0x78A368 zero. It hides total, positions LapText ten
  logical units after LapNumber's right edge, and caps the displayed lap at
  99. The port's circuit free drive maps to that presentation branch only;
  it does not change route state or claim a full original event setup.
- Native evidence uses speed.exe SHA256
  C0FAD450912952F53809A54690D235AA8B4E25B9F46650693C7CBEE1B4C074B7.
  Existing lap_native confirms bounded two-lap text output. Its initial
  zero-lap attempt hit an unimplemented UI binding and is NOT a native pass;
  the no-total branch was instead inspected in the authenticated disassembly.
- The root SA hierarchy, text/backing geometry, font bindings and world.efx
  linear/inset contract are documented in HUD_254_RUNTIME_PRODUCERS.md and
  HUD_254_PRESENTATION.md. English labels use authored default strings.
  Upper-left viewport scale follows the supplied Deck reference, matching the
  existing gauge scale. This is not proof of every widescreen patch or live
  original font override. No map is drawn by this change.
- Timer quads use a separate linear sampler and half-texel inset; the gauge
  and menu retain their existing sampling. CPU reference applies the same
  timer sampling. Runtime geometry stays fixed-capacity and allocation-free.

## Validation and remaining acceptance

Evidence is local under build-artifacts/draw-distance-20261008-root.

- Native affected targets built, including nfsu_race.
- Targeted CTest: race timing, lap HUD and lap presentation passed (3/3).
  Existing session tests cover repeated reads, lap hold/release, finish and
  retry/reset. Added live-panel reads leave canonical state unchanged; the
  no-total counter behavior and display-only cap are checked separately.
- nfsu_race_hud_tests retains two manual-gear fixture failures. Recompiling
  the parent HUD implementation with the unchanged current vehicle code
  reproduces exactly those two failures. See ui-timer-r1-parent-hud-check.json.
  This is a controlled HUD comparison, not a full parent-tree validation.
- Initial timing CTest was not run because its executable was absent; the
  target was then built and the actual test passed. No skipped pass claimed.
- Actual retail PC loads and rendered frames succeeded for events 1 and 8,
  using the current skyline/car-cube capture helper rebuilt with current HUD.
  These CPU captures establish loading/UI, not complete GPU postprocess parity.
- Real Switch SDK NRO build passed; NRO, embedded shaders and frontend profile
  contracts passed (3/3). No full unrelated matrix per owner's minimal-test
  direction. New UI changes invalidate old full-frame image expectations;
  none have been blindly refreshed.
- Pending: actual Switch launch, original-reference visual acceptance and a
  driven lap/pause/re-entry check. NRO tag ui-timer-r1. No upload performed.
- Next: owner checks PC frame; send sealed NRO on fresh Netloader readiness.
  Resume minimap only after the timer step is accepted.
