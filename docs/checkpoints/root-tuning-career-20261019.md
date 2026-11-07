# Career tuning implementation

Owner assignment: implement tuning sequentially. First establish original
catalog, category/price/unlock rules for the campaign through opening the
first tournament, then purchases/persistence, live garage and race consumers.
Role: worker, root owner integration, solo per explicit owner instruction.
Worktree: build/day20261008-root-combined relative to main checkout.
Branch: codex/draw-distance-original.
Head: ea4d472c9c399265370fa10be94736b1121771ad.
Existing staged GLM merge and integration changes preserved. Main checkout
was clean at task start. No delegate, checkout, commit, push or merge.

Baseline: sealed campaign-flow-r17, not sent. Owner explicitly deferred its
console check until tuning. Its confirmed stale-selection fix is preserved;
complete hardware post-finish acceptance remains open.

Current phase: original read-only research. CarPartDatabase has original
part records and installed selection; vehicle_tuning has physical levels;
runtime skins and visual assembly exist. CareerProfile has no installed
parts, purchased inventory or performance setup yet. Garage is presentation,
not an implemented purchase loop. Existing documents include historical
limits; verify against current code rather than assume those remain exact.

Research evidence lives under build/tuning-catalog-r1. Original PC1.4 USA
speed.exe identity is checked before disassembly. Candidate string xrefs
are navigation only, not confirmed semantics or function boundaries.
Next: trace actual garage price and availability callers, native execution
where feasible, and reproduce the decoded catalog in production.
Validation: no new implementation or build verdict at this checkpoint.

## Catalog and transaction checkpoint

Production: new career_tuning header/source, attached to CareerCatalog.
Original price/category/event/style witnesses and actual-record ownership
keys agree with C++. Catalog SDK compile PASS in switch-catalog-build-r1.log.
PC purchase-test-r2.log PASS includes lock, funds, compatibility, stock/free
reinstall, earned-style retention, schema1/2 migration, schema3 save/reload
and earlier campaign progression. Original oracle outputs are under
native-final-r5 and owned-native-r3. First oracle failures were bounded
instruction-range declarations; promotion first refused a compressed-file
hash because the known identity is the decoded image. Failures remain in
separate logs; final bounded originals ran without external call hooks.

New save writes are schema3. No SD profile has been changed. GUI/race use of
installed state is not implemented yet. Normal vinyl families are decoded;
special whole-car vinyl policy, complete garage, live preview and per-car
visual reputation remain open. Existing r17 finish-flow acceptance remains
open. No console upload or broader optional test sweep.

Next: inspect authentic garage FNG/category transitions and owner reference,
then connect the existing purchase backend to the live garage.
## Garage integration checkpoint

Catalog/purchase SDK build completed in switch-purchases-build-r2.log.
New career_ride and career_garage sources added. Shared selection resolves
stock/default rendering paint separately from purchased paint, installed
parts, generic runtime skin, sponsors, performance and visual reputation.
Original5A05B0 agrees with career_tuning_reputation_native.inc; its bounded
oracle uses actual database levels with supplied RideInfo slots and no hooks.

Garage core, FNG resource bindings and resident component-bank API are in
progress. Smoke-r5 passes body navigation/purchase/preview and draw-update
compatibility on PC. Its garage-body.ppm/png was visually inspected: authored
LockedIcon was incorrectly shown on an available selection; this still needs
correction. Native root/body/performance/accessories order researched; some
leaf menu flows (rims, paint, vinyl, decals) remain a functional first pass,
not verified native parity. Do not claim full tuning or send this tree yet.

Switch/race_main.cpp is now wired to garage input and a bounded preview bank,
and to owned physical/visual setup at race entry. intro_grid supports the
owned human dress while preserving authored UG0. These newest Switch edits
have NOT compiled yet. A broad string replacement briefly inserted the owned
human block into traffic too; this was removed before the next successful PC
build. No traffic semantics change is intended.

Open: correct conditional garage icons/stars and actual leaf presentation;
check all initial-slice categories and preview cancellation; inspect memory
and strip unused CPU variant payload if needed; recover style multiplier and
earned-unlock notification consumer; verify race consumption and SDK; seal a
combined NRO only after those checks. Sealed r17 stays unchanged and unsent.
No console save/upload, commit, push or merge. Main checkout must stay clean.

## Materials, effects and leaf-flow checkpoint

Native neon color and reputation oracles promoted, production fixtures pass
in garage smoke-r13. Exact counts live in their fixtures and oracle outputs.
Neon reuses shadow ground taps; preview strips unused source/parser/skin copies.
Special rim/exhaust/caliper/spoiler materials now bind actual selected paints.
Latest colors/material changes compiled, smoke-r13 passed and body UI inspected.
No unexpected lock overlay remained in that frame.

Rim manufacturer/style/size navigation now follows WHEEL_MANUFACTURERS and
original family ownership. PC_ColorPicker has real palette families and grid
input. Decal structural layout selections are now represented separately from
sponsor texture selections; stock window/door/quarter/hood slot counts traced.
Original30240 wide-body metadata found at decoded FrontB B6BC80; its later
wide-body consumer remains outside initial stock-car slice acceptance.
Sponsor baseline whitelist traced51BEB0; other brand selections use earned
kind2 flags. This corrected an earlier free-all-sponsor first pass.

Post-race integer style multiplier and earned reward popup integration added.
Right-stick garage orbit uses original projection with documented port input
rates, not a native-rate claim. Latest full category smoke extension and SDK
are pending. PC build-categories-root-r15.log is the active build; no current
console readiness claim. Last SDK success predates neon/palette/material edits.
Main checkout still clean. Sealed r17 unchanged/unsent; no SD save, upload,
commit, push, checkout or merge. Next: run category/paint/vinyl/decal/resource
smoke, inspect color UI, run shader/shadow gates and latest SDK, then seal an
honestly scoped combined NRO. Do not send until ready and authorized.

## Initial tuning slice ready for hardware acceptance

Latest production includes palette/sponsor selection fixes and right-stick
orbit. The preview bank signature includes decal geometry as well as the
selected logo, so changing a layout cannot reuse the wrong resident bank.
Vinyl color entries now follow each installed record's NUMCOLOURS. Current
color preview and conditional color-tier locks bind the authored PC picker.

Actual workspace validation:
- cmake --build build --target nfsu_career_tests -j6: PASS in
  build/tuning-garage-r1/build-final-root-r20.log.
- build/nfsu_career_tests.exe <readonly-retail> <fresh-output> garage:
  PASS in garage-final-root-r20.log. This includes actual owned career grid
  preparation, combined bumper/rim/base paint/rim paint/vinyl/window sponsor/
  neon, cached and reference GPU frames, command validation, resource bounds,
  live preview and original FNG/color rendering. Combined CPU preview inspected.
- build/nfsu_career_tests.exe <readonly-retail>: PASS in
  career-final-root-r19.log, including new save3 purchases, free reinstall,
  insufficient/locked/foreign refusals, first-five win progression, old save
  migration and damaged-save recovery. No SD profile touched.
- ctest --test-dir build --output-on-failure -R
  '^(nfsu_shader_manifest_tests|nfsu_shader_source_guards|nfsu_car_shadow_projection_tests)$':
  PASS in shader-shadow-root-r15.log.
- bash build/tuning-garage-r1/package-switch-root-r19.sh: PASS in
  switch-final-root-r20.log. Configured career-tuning-r18, built NRO and ran
  the NRO, embedded shader, GPU sample, FE sampler and FE preflight checks.
- bash build/surface-shadow-r1/scan-surface-shadow-r2.sh: tracked ASCII/mode/
  whitespace scan PASS in ascii-final-root-r20.log. An earlier pass found a
  new trailing blank line in frontend_scene.cpp; it was corrected and the
  affected PC/SDK builds were rerun. Source seal also scans untracked files,
  new paths and every scratch automation byte before copying the NRO.

No full Default/Release matrices or independent review claimed. The owner
requested sequential solo work and minimal checks. This is a concrete first-
five tuning slice for console acceptance, not a complete whole-game garage.
Known remaining scope is in CAREER_TUNING.md: Trade In, future award selectors,
magazines, whole-car vinyl awards, later performance tuner/brand screens,
wide-body-specific decal layouts and native generic purchase dialog. Native
FE camera projection is used; orbit input rates are explicit port choices.

Next action after sealing: owner hardware check of garage navigation, preview,
purchase, return to garage, save/reload and the owned car in an event. Observe
preview bank rebuild latency and SD memory headroom. Also check the previously
reported result-screen bypass using included CAREER_FLOW boundary journals.
Do not declare hardware completion before this evidence exists. No upload,
commit, push, publication or merge yet. Previous sealed r17 preserved. Main
checkout clean at the last boundary. Integration work stays in this worktree.
