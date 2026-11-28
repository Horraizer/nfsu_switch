# Samantha reward rim inspection

The campaign result for internal event47 offers Samantha's unique wheel
category3. It opens `MU_SelectUniquePart.fng` through
`CareerCatalog::event_finished` and `CareerGarage::reset_unique`, with physical
slot17 and the ordinary wheel camera preset. It is not the tournament48
result. `nfsu_reward_rims_motion_retail` checks the actual result routing and
the other campaign unique rewards.

## Original PC evidence

Input: PC Underground 1.4 USA `speed.exe`, 3178496 bytes, SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
Retail was read only. Ghidra analysis and bounded x86 execution agree:

- Unique visual constructor `4C7790` writes EDI=4 at `4C7EFF`, then calls
  `4B2480` at `4C7F3D`. The latter forwards EDI in EAX at `4B248F` to
  `4B14E0`. The nonzero mode writes the generic target20 to global `746DE0`.
- Ordinary rim constructor `50C9E0` overrides that target at `50CD34/39`
  with the inspection value1 from `6B6B30`.
- The unique constructor publishes the hypothetical ride through `4B0E50`,
  then focuses the selected physical slot through `4B0150` and camera table
  consumer `4AEA50`. Its wheel case uses slot17. Selection handler `4C85B0`
  changes the staged part, republishes it and refocuses the same slot.
- The introductory explanation handler `4C8810` hides the explanation and
  clears its navigation gate. It does not set the road target.
- Car-view constructor `4AF620` initially enables animation at owner+10C.
  Update `4B0340` handles car travel states; wheel render `4B1760` advances
  the phase only when that animation flag is enabled, using road displacement
  plus car travel. No unique-rim stop call was found in the inspected unique
  constructor/selection/explanation path. Calls to animation setter `4B2520`
  were found in the magazine flow, not this selector.

`tools/reward_rims_motion_native.py` authenticates the executable and executes
the mode assignment, forwarding call and both target setters with no hooks.
It excludes UI allocation/lookup, scheduler and rendering. This is bounded
instruction evidence, not a capture of a running original game. The existing
garage camera and frontend motion witnesses cover the shared camera preset,
road easing, phase arithmetic and wheel transform consumer separately.

## Deliberate port behavior

As a deliberate usability difference, a pending wheel-category reward reuses
the existing ordinary rim inspection target. The retail campaign has only
the Samantha event47 reward in this category. The original road
integrator eases into that target; wheel phase, camera and car travel
equations stay shared with the ordinary garage. This is not presented as an
original PC unique-screen speed restoration.

The category-derived override belongs to the ephemeral reward selector. Intro, next/previous,
confirmation and confirmation cancellation retain it. A confirmed award
releases it; ordinary menu motion and garage presets resume. Resetting for a
normal garage or another reward cannot retain it. No saved field, price,
installed part, other unique reward or race wheel path changes.

## Reproduction and validation

Run from the assigned checkout with its toolchain on PATH and the retail
root supplied as a command argument:

```text
python -B tools/reward_rims_motion_native.py --root <retail>
build/nfsu_career_tests <retail> --reward-rims-motion
build/nfsu_career_tests <retail> --reward-rims-motion <fresh-output>
```

The last form also renders the actual reward UI and car at the wheel preset.
The regression earns the award through production result handling, publishes
the real resident preview bank, locates all four submitted wheels from an
independently prepared car model, and measures their frame matrices. A generic
target on that same bank is the negative control against a global wheel cap.
It covers explanation/enter, next/previous, back, confirm, consumption without
a charge, leaving, revisiting the ordinary garage and normal rim pages, plus
the other campaign rewards. Values are pinned only in the tests.

The diagnostic CPU renders and submitted transform checks do not certify
Switch GPU output or controller scheduling. Switch execution is outside this
private assignment, and the console was not occupied.
