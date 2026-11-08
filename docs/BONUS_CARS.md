# Earned bonus cars in Quick Race

The active Underground profile's saved `style_high_water` gates the original
kind3 style rewards from GLOBALB34A08. The sandbox's unrestricted normal cars,
parts and tracks do not grant these earned presets. Penalties do not remove an
earned car. Selecting another profile rechecks access against that profile.

## Original evidence

PC1.4 USA speed.exe SHA-256:
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
GLOBALB.BUN SHA-256:
`39df38fac2be7ca0f0d0a92e0113caaeda5b1c60e14436c612b3d0874afc8db9`.
FrontB.lzc SHA-256:
`76f368d5e5e30f96a8182bf457dba4491395e075c3f336d86a32012b56e16f90`.

Original004AA970 sets persistent earned flags and does not clear them when
points fall.004AC220 creates descriptors for kind3 rewards in table order,
retaining the preset hash, style index and mode mask255.004BC170 admits earned
descriptors into the Quick Race car list.004C3240 separately builds ordinary
Underground trade-ins; earning a bonus does not replace the career car.

004AC0F0 applies004B26F0's complete ordered preset after0057D160's cleared
ride initialization.004BC830 has car-type-specific performance overrides;
bonus cars do not copy the human career upgrades or the sandbox's chosen tier.
`tools/bonus_cars_native.py` executes the original preset consumer via the
existing bounded native witness, and the original performance stores, to
regenerate `tests/bonus_cars_native.inc`. Its constructor, formatter and
performance-recalculation boundaries are explicit. It is not a live-game or
whole-engine emulation claim. Retail files remain read-only.

The retail test `nfsu_bonus_cars_retail` is the record for the exact preset
identities, point thresholds, installed slots and performance stores. It also
checks saved access after a point penalty, profile isolation, every authored
preview, each selected car's production Quick Race setup and driving after GO.
No current test pins are duplicated here.

## Selection and ride lifecycle

Quick Race appends earned preset identities after the ordinary car list. The
display names come from the original English award messages; the internal
`STYLE_SPONSOR_CAR_n` descriptor hashes are not language-table keys.

The selected `bonus_preset` is transient and never added to the career save
schema. The complete resolved part selection retains empty slots as well as
body, paint, vinyl, decals, rims and other authored equipment. In particular,
the native null hood-decal slot is not inherited from a stock ride, and an
empty vinyl key cannot select the custom KR part's absent BINNAME attribute.

Bonus rides expose the normal course, traffic, opponents and race options.
Their authored appearance and performance are fixed, so the ordinary sandbox
customization controls are hidden. Selecting an ordinary model restores its
own sandbox ride. Bonus choices never overwrite that model's saved sandbox
customization. Preview signatures include the preset identity, including when
cycling between two presets of the same base model.

The existing post-race style reward flow supplies unlock messages. The next
Quick Race entry reads the active career save again. A fresh profile has no
earned bonus presets. Existing saves need no migration.

## Acceptance limits

Host tests exercise original data, menu draw commands, actual car preparation
and production race startup. SDK compilation and NRO checks validate console
packaging. Final controller navigation, live GPU appearance and performance on
Switch require a console run and must be reported separately.
