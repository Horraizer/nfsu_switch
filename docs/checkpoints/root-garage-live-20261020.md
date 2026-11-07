# Garage live preview and skid follow-up, 2026-10-20

Role: worker, Codex root owner integration. Direct owner scope: rims slow
motion, all visual part/neon/color previews and purchases, missing Unique
vinyl, and excessive tyre marks. Local-only dirty integration on
codex/draw-distance-original in build/day20261008-root-combined.
No commit, push, merge or publication. Preserve the sealed r20 and r21.

Evidence from r21 capture: completed multi-variant jobs took several
seconds; a larger color request was replaced before it could publish.
The prepare path reloads/decompresses/decodes the whole room per variant.
An immutable FrontendSceneEnvironment is now shared across jobs. Alternate
cars prepare only vehicle/shadow/neon/glow commands, using that room.
The main thread alone owns GPU work. Room leases preserve upload pointers
across old-owner retirement. Six fixed-pose PC garage frames match r21
byte-for-byte; two asynchronous images have different supplied elapsed time
and are not a differential parity claim. Runtime caching tests follow.

Original50CD34/39 copies6B6B30 to746DE0: rim browser target1 km/h.
Generic505C29/39 copies6B6B2C:20 km/h. Existing4AE1F0 acceleration is kept.
The actual menu sets the target; leaving garage restores ordinary menu
motion. Source disassembly is saved under root-road-*.

Found a real skid input bug: wheel+168 is normalized force demand/limit
published by587830, not the velocity-slip norm. The previous adapter fed
m/s into168*158>8 and could trigger marks on nonsliding wheels. The force
limiter now exposes only its diagnostic usage; no force arithmetic changes.
Original587830 through5879AD ran on supplied stock240SX inputs without
hooks. New wheel_skid_usage_native fixture pins that output. Full original
587E90 gate witness remains separate; marks must still break on contact
loss/reset/material gaps. No original full world/ribbon manager claim.

Actual data: vinyl type15 is Unique/CUSTOM; type16 is manufacturer. Their
labels were reversed. Unowned level4 Unique records were also hidden even
when the style award existed. Original4E3F90 through4E4043 calls4AA9D0
kind2 with the exact part hash for these types. Authentic4AA970 flags,
threshold equality and the special selector were executed without hooks.
The catalog now follows that branch, leaves unearned entries locked and
keeps unrelated level4 award selectors closed. Full special whole-car body
vinyl policy is not inferred from this narrow correction.

Production CareerGarage preview_signature and prepare_previews are shared
with the new live navigation diagnostic. The diagnostic uses real menu
entries, background room-reusing jobs, actual GPU geometry/texture bindings,
confirmation, cash/ownership mutation and installed-base restoration.
Current phase: building/running this diagnostic and skid/player regressions.
No complete r22 validation or hardware result yet. Next: correct concrete
failures, inspect images, run required Default/Release/SDK/ASCII matrix,
seal and return to owner hardware verification. Other r20 acceptance and
race music balance remain open.

Second boundary:
- Shared-room loading passed six fixed-pose r21 frame comparisons. Car input
  files are now leased once per bank and dropped before publication; variant
  preparation no longer rereads those files either.
- Native skid utilization witness passed, including contact, lock, slide,
  zero capacity and driven-wheel boost. New skid/player targeted runs pass.
- Native generic/rim target setter and actual special-vinyl branch/flags ran
  without call hooks. Unique category mapping and earned availability pass.
- Live diagnostic traversed bumper, rim style/size, neon, paint and Unique,
  including preview and installed-base restoration. Its initial comparison
  failure was a debug-name race-car prefix, not changed geometry; subsequent
  checks compare actual payloads. Failed logs are retained.
- Found the missing FE-neon fragment consumer: generic textured shaders did
  not implement alpha marker7. Racing uses a separate projected shader. The
  FE branch now consumes packed color like that shader. The CPU diagnostic
  uses the current explicit view for this draw. Fixed-pose alpha-off pixel
  comparison and an inspected red-neon image confirm visible contribution.
- Adding a second neon color case, final focused rerun and live skid probe
  precedes the one required full source/SDK/ASCII matrix. No r22 NRO sent.

Final focused boundary:
- Native motion, Unique/manufacturer style and wheel-friction-usage tools
  reproduce their checked-in fixtures without write-fixture mode.
- The complete live menu diagnostic passes, including preview, buy and parent
  restoration for bumper, rim style/size, two neon colors, paint and earned
  Unique. Unearned Unique remains visible/locked. The car file cache reduces
  duplicate per-variant reads; raw source leases are dropped before publication.
- Corrected FE-neon CPU reference to use ordinary-world RGB2x/BGR expansion,
  matching the newly added GPU consumer rather than saturating tint early.
Next: final focused rerun and production skid probe, then immutable source
and one required Default/Release/SDK/ASCII matrix. No hardware or merge claim.

Pre-gate freeze:
- Final focused live menu run passed with both neon colors and actual light
  pixels. Three native tools passed fixture-reproduction mode. Actual900-tick
  race probe retained visible skids and normalized telemetry; only sliding
  wheel samples emitted in that recording. No driving-force changes claimed.
- Car-only assembly preserves the original race-car debug-name prefix as
  well as payloads. The final matrix rebuild will repeat the actual live
  menu diagnostic on the exact frozen sources before running all CTest gates.
- All remaining source changes are frozen for required host/SDK validation.
  Hardware garage responsiveness, all visual previews and skid appearance
  remain unaccepted until the owner checks this exact NRO.
