# UG0 traffic and prop completion checkpoint

Owner requests one combined NRO: traffic behavior/contact/shadow/lights,
small-object contact and measured optimization. Solo implementation; no reviewers
or new agents. Targeted checks per owner. Local only; publication authority remains
unresolved. Base: 68de396. Owned tree: build/day20261008-root-combined.

## Deferred scope

Restore original realtime car selection without intervening loading screens.
This needs resource residency and transition logic, not hiding loading text.

## Confirmed omissions

Traffic shadow assets are not loaded and add_rival explicitly skips their draw.
Lamp glow anchors are consumed only for the player. Impacted traffic permanently
brakes in the selected-car physics adapter and stops updating wheel animation.
Prop definition/placement/lifecycle research exists but no gameplay manager does.

## Acceptance still open

Implement and validate combined changes, PC captures and actual contact scenarios,
performance evidence, SDK build/package and one exact NRO. Switch runtime pending.
Do not claim full original traffic scheduler/contact/lifecycle equivalence from
ported primitives or synthetic checks. Preserve original data read-only.

## Implemented local candidate (2026-10-13)

Original data: read-only PC 1.4 USA. Asset census and disassembly under
build/ug0-traffic-props-r1. Correction to the initial shadow omission: among the
15 traffic assets only AMBULANCE contains SHADOWIG. Original missing-resource
behavior skips that shadow. No fabricated substitute is installed.

- Traffic and rival original lamp sprites use authored markers. Traffic markers
  attach to the body part; traffic does not have separate lamp geometry slots.
- Traffic projected headlights share the existing world draw pass. Six maximum,
  camera-distance admission and per-fragment depth/UV rejection. GPU cost still
  needs portable Switch measurement; no claim of sustained 60 fps.
- Recycled traffic clears physics caches. Impact wheel animation continues.
  Actual impulses, not SAT overlaps alone, activate collision simulation.
  Traffic pairs now exchange impulses, with conservative AABB rejection.
- Loaded authored 178 prop placements, 11 types, real mass/inertia/pivot and
  attachment threshold. Original 597980 callback fraction limits the first
  impulse; 46C5E7 repeats with the released body's mass/inertia. The prior WIP
  omitted this retry and sent the light street sign hundreds of metres. Failed
  r6 and corrected r7 measurements remain in the evidence directory.
- Moving objects update their existing GPU placements in a small fenced two-slot
  uniform buffer. Meshes/textures are shared. Reflection matrices follow motion.
  Stationary props skip integration; broad phase avoids distant exact contacts.

Verification commands (Release native, PATH includes C:/Strawberry/c/bin):
- cmake --build build --target nfsu_intro_grid_viewer nfsu_car_contact_tests nfsu_gpu_upload_tests -j 4
- build/nfsu_car_contact_tests.exe: PASS, existing pinned original car responses.
- build/nfsu_gpu_upload_tests.exe: PASS, extended headlight uniform ABI.
- nfsu_intro_grid_viewer <retail> <prefix> 0 props-check: PASS all 11 types.
- nfsu_intro_grid_viewer <retail> <prefix> 1800 profile: PASS actual RaceRun and
  GPU package/prefix/resource stability; captures include traffic lamp sprites.
- Real SDK nfsu_switch_race_nro built, including changed fragment shaders.
- CTest nfsu_switch_race_(nro|embedded_shaders): 2/2 PASS.
Raw command outputs: build/ug0-traffic-props-r1/*-final.log, sdk-*.log.

Limits retained: SAT witness and vertex/terrain ground response are port adapters,
not original GJK/material/cache equivalence. No prop-to-prop contact or child
replacement (the actual loaded types have no children). Impacted traffic still
uses the selected-car brake adapter until pool retirement; original crash action
recovery remains open. No new traffic audio. CPU captures verify sprites and
geometry, not GPU-only projected multi-car headlights. Existing road culling
metadata describes initial placements; moved props also have a separate selection.
New lighting/physics on portable Switch and frame-time cost are unverified.
Realtime car switching stays deferred. No publication, merge or deploy performed.

Owner explicitly requested solo work and reduced targeted checks. No agents,
review assignments, repetitive full native matrices or board writes were used.
Final NRO identity is recorded in the ignored build evidence/package receipt.
