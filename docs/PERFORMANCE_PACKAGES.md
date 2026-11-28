# Original performance package selection

## Evidence and reproduction

The owner baseline is bdda1226701879858e1fee3083de8c9874289d57. A failing
normal CareerGarage input sequence opened Performance, Engine / Exhaust,
Street Package, then immediately set confirming=true on the level screen.
There was no brand screen. The regression is now
`nfsu_performance_packages_retail`, with CLI `nfsu_career_tests <retail-root>
--performance-packages`. It uses the real catalog, menu, renderer recording,
ride resolver and save adapter, rather than a parallel shop implementation.

Read-only Ghidra analysis used PC1.4 USA speed.exe, SHA-256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Original executable and decompressed FrontB identities are authenticated by
`tools/performance_packages_native.py`. Its native lookup and purchase
witnesses are pinned in `tests/performance_packages_native.inc`. That fixture
is the record of category/tier counts, exact variant order and ordered firms;
this document does not duplicate those pins.

## UI and data

Ghidra functions 50A5D0/50ADA0 construct and update the level browser.
50B2D0 accepts a level. 50B6E0 then constructs
MU_GaragePerformanceBrand.fng, at decompressed FrontB offset B82D8C.
50B9E0 formats Package plus the selected number and appends each BRAND_n
language label in record order. The level page already supplies the
category/level's generic mechanical contents. The brand page supplies its
firms. It does not assign individual firms to individual mechanical parts.

| Source | Original consumer | Meaning |
| --- | --- | --- |
| FrontB chunk 34A09 | 5A0270, 50B6E0 | Field followed by per-tier variant counts |
| FrontB chunk 34A0A | 5A02C0, 5A0220 | Field, level and package number as little-endian u16, then u8 firm count and ordered u8 firm IDs |
| English.bin BRAND_n | 50B9E0 | Display labels for package firms |
| GENERIC_LEVELn | 50B6E0 | Package-number thumbnails, independent of mechanical level |

Both tables start at the next aligned address after their headers. 5A0220
matches all three identity fields, so collapsing records by field and level
loses actual choices. The port now parses both tables and opens this original
page from the level picker. Left/right clamps at the first/last package, as
50B8F0 does. Back restores the previous page and its selection. Reopening the
brand picker starts at its first package, as the original constructor does.

## Purchase and durable state

50AA90/50AB90 check the campaign gate and money before entering brand choice.
50AAC0 requires the next installed level for a new purchase. Owned levels
bypass that progression requirement and can be reinstalled free. 50B2D0 gives
an already-installed dialog before brand choice if that level is installed.
Stock uses a direct confirmation. Unique items remain separate flags.

50BC10's paid-confirmation path invokes 5A25A0 to debit the category/level
price and store tier ownership. 5047D0 stores the installed level. Its
50BCA2..50BD48 slice ORs the selected firms' flags in profile+1260 and sets
the turbo sound identity at customization+0C when choosing Turbo. The owned
installation path at 50BE63 updates the installed level and turbo identity
without granting additional package firms. Both paths close the brand picker.

There is no durable package number or per-component brand assignment in
these paths. Engine and brake appearance resolve from their level, not their
firms. Generic mechanical levels and their physics/ratings are identical
across same-tier packages. The port retains that distinction: sponsor flags
are profile-wide, turbo identity is on the ride, and package numbers remain
menu state. Sponsor availability consults the recovered alias table and the
permanent purchase flags. Removing a level does not revoke its firms.

Original save writer4AB120 and reader4AB200 round-trip the complete Quick
Ride and Underground customization blocks, including the purchase sponsor
region. The native witness checks every byte of those copied regions with
synthetic data. It neither installs a PC profile nor claims PC startup.

Browsing and confirmation operate on a preview. B or the Cancel button
discards the pending transaction. A confirmed transaction alone reaches the
normal save path. Cost and the selected firms are visible before confirmation.

## Compatibility and other garages

Port save schema13 appends PERFORMANCE_BRANDS, the turbo identity and sponsor
flags after the existing fields. Schemas1 through12 remain readable. They
keep their levels, ownership, parts and money; their absent brand provenance
is not guessed. The alternating save adapter retains a byte-exact pre-v13
slot for rollback before publishing a migrated slot.

Quick Ride uses the same brand page and retains its ride through the existing
in-memory car selection path. The port's multiplayer lobby still controls the
mechanical tier. Its garage opens only that tier's brand variants and cannot
change tier or unique items; a stock lobby has no such variants. This is a
port lobby rule, not a claim that the original PC game offered split screen.
Turbo identity travels with both local and LAN rides, including the sound
loader. The LAN configuration identifier changes so incompatible older
participants are rejected instead of silently losing that field.

## Validation boundary

`nfsu_performance_packages_native_retail` executes original table loaders,
the full lookup and the bounded purchase flag/turbo slice, with no external
call hooks. Every emulated record is compared with its complete retail bytes
before using its result. It also executes the original new-tier predicate.
This is original-code evidence, not a live original-game UI replay.

`nfsu_performance_packages_retail` covers all fixture variants through actual
menu input, native order/contents, same-tier rating equality, previews,
confirmation cancellation, cost, sponsor grants, free reinstall, normal ride
assembly, fixed lobby selection and old/new save round trips. Its optional
third CLI argument writes a fresh desktop garage image. This task neither
publishes a build nor uses the Switch. Console input, rendering and audio
remain for independent hardware acceptance.
