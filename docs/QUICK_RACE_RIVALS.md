# Quick Race rival construction

## Original and port boundaries

The PC1.4 USA Quick Race producer separates model choice, driver skill,
physical upgrades and appearance. Model selection alone is not a completed
rival. The original4B4040 calls4B3C60 for a stock candidate,4B4590 for a driver
context, then4B4380 applies57F700 and57D860 to the completed opponent list.

The port previously stopped at model/context selection. Its empty performance
selection became stock physics; its empty preset became the blue stock skin.
Quick Race now supplies the adaptive performance result and a generated
installed-part selection to the same scene/runtime consumers as career cars.

The port has an explicit Quick Race seed. It does not recreate the original
global RNG history before entering the race, including racer-name selection
and preliminary paint selection. The same integer seed is therefore not a
promise of an identical complete original-game roster. The appearance and
context functions are compared at their explicit input boundaries.

## Performance and skill

57F700 matches the rival to the actual player's performance rating, retaining
the chosen car. This is not an unconditional copy of identical part levels:
different base cars can need different upgrades to reach the same rating.
The producer copies eligible player settings, searches the original level
masks and adjusts its original tolerance when the discrete upgrades cannot
hit the target. Normal runtime aero is rebuilt through57F2B0.

Difficulty selects a cumulative distribution of original driver contexts.
4B4590 includes equality at each threshold; the native captures include zero
and rounded boundary samples. Context data reaches the existing road-speed,
throttle, steering and drag-shift controllers. Quick Race keeps its own
original catch-up switch and difficulty-specific drag clutch behavior; it
must not borrow an arbitrary career event's global throttle ceiling.

## Appearance

57D860 uses the final performance levels, with distinct level sources for
bodywork, spoiler, exhaust and wheels. Its original probability table chooses
whether and how far to upgrade each visual category. Thus a stock rival can
have visual modifications, and upgraded rivals need not share identical kits.

Selection includes bumpers, skirts, hood, roof, spoiler, widebody, lamps,
exhaust, wheel style and dimensions, tint, neon, vinyl layers, sponsor parts
and paint classes. The native loader57C8B0 defines paint/vinyl pools by class
and level. Database order and RNG consumption are preserved within this
producer.57EA80 compatibility supplies the final enabled slots and widebody
decal bindings. Resolved paint and vinyl colors are composed into the runtime
skin, and sponsor bindings are installed before the scene is uploaded.

The generator runs during loading. Drawing does not resample it. An installed
Quick Race neon also retains its texture/color asset through grid upload into
RaceGpuScene; selecting slot116 alone would not make it visible. Its existing
controller reuses the vehicle shadow's terrain placement.

## Reproduction and acceptance

Input identities are authenticated by tools/quick_rival_native.py. It executes
the original x86 loader and appearance code on the retail data. Explicit
synthetic boundaries are the database list head, QueryPerformanceCounter and
the observed CRT formatting calls. No part selection, compatibility or RNG
result is substituted by a hook. Retail files remain read-only.

Run from the repository root with an installed Python and Unicorn:

```text
python tools/quick_rival_native.py <retail-root> <fresh-output.json> --all-cars
nfsu_quick_rival_probe <retail-root> --verify
ctest --test-dir build -R nfsu_quick -V
```

The native witnesses live in tests/quick_visual_native.inc and
tests/quick_context_native.inc. The retail test nfsu_quick_rival_visual_retail
checks installed records, enabled flags and final RNG against those witnesses.
nfsu_quick_race_retail exercises production grid loading, player performance
presets, difficulty contexts, the composed rival skins, scene admission and
live race steps. These checks do not replace a console drive for visual and
handling acceptance.
