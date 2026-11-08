# Tuned lamp emitter recovery

Scope: owner report of partially working tuned headlights and taillights,
including Projector. Base: b5118ce29b996b855cc3a4242794868f3fba14d8.

## Original evidence

PC 1.4 USA speed.exe SHA-256:
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.

Ghidra56CC40 scans all thirty model solids and every 80-byte marker record.
For each recognized lamp it allocates a light node, copies its position and
normal and appends it to CarRenderInfo+0x500. There is no first-per-name test.
56D8D0 visits this complete list;5447C0 consumes each light, creating two
headlamp layers or one tail layer. Resting ordinary-owner tail gain is .7;
service brake above .05 adds16*brake, clamped to1. These gain/primitive rules
were already implemented and are retained.

The port instead kept only the first of each of five marker names. Additional
lamps authored with the same LEFT_HEADLIGHT, RIGHT_HEADLIGHT or brakelight
name were discarded. Civic Projector and several rear kits expose this
without relying on subjective brightness comparisons.

00580B00 and004BBD70 confirm installed BINNAME texture resolution. The full
variant check also verifies selected lamp material groups resolve textures.
The initial texture-alias hypothesis did not reproduce a missing binding;
this change does not rewrite the texture alias rules.

## Change

Keep every emitter in a per-name list. Prepare player and actor sprite/draw
storage from the fitted marker count before renderer opening. Runtime updates
reuse that storage for both ordinary driving and braking. The legacy debug
four-sprite approximation and two-emitter nitrous purge intentionally keep
using one primary point per side. Frontend rendering already visits all
markers and is unchanged.

No fitted-variant cap replaces the former two-headlight assumption. Missing
name groups keep transparent placeholder slots for stable package topology.
No per-frame heap allocation is added by the original lamp-placement overload.

## Reproduction and verification

The test nfsu_lamp_variants_retail prepares every front/rear kit on every
playable car through the production skin/geometry/texture paths. It checks
selected solids, texture bindings, every repeated marker position/normal,
visible placement and stable sprite storage when braking.

The optional native instruction witness runs the same input matrix against
unaltered original56CC40 with only its allocator supplied:

```text
python -B tools/lamp_variants_native.py --retail-root <ascii-retail-root> --probe build-publish-r2-default/nfsu_lamp_variants_check.exe
```

It compares emitter multiplicities, positions and normals, including repeated
names. This is not a claim of full damage, light occlusion or final GPU pixel
parity. Console appearance and frame time remain hardware acceptance criteria.

Full Default/Release and affected Switch gates, independent review and the
final NRO identity are recorded in the worker handoff after they run.
