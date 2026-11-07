# Garage menu correction

This records the owner's2026-10-23 garage report. It is not a hardware
acceptance or a claim of complete original front-end fidelity.

## Original evidence

The read-only PC1.4 USA executable identity is recorded by
`tools/career_palette_native.py`. Ghidra analysis used a separate executable
copy. Native4E3320 maps displayed vinyl layers to slots34..31 and opens the
color picker through its secondary color action.4E40D0/4E4C40 construct
separate group and family browsers.4DA1A0 reads NUMREMAPCOLOURS; NUMCOLOURS
is the texture palette size and cannot determine the number of paint channels.

4D9A40 and4DBBB0/4DBBF0/4DBC30 populate separate tier ranges of the authored
PC_ColorPicker.fng.4DBF90 positions Cursor at its selected cell. The four
Colorpick_Arrow sprites surround the fixed CurrentColor sample.
4DC280/4DC230/4DBAD0 clamp axes and retry a missing cell once before restoring
the previous position. The executable witness and its checked fixture cover
full and sparse palettes. Execution stops before FEng rendering; there are
no external call hooks. The same witness executes68D1D0's vinyl icon table
initializer for4E24A0.

4D7B70 lists earned paint finishes in career.4E2520 restricts body vinyls to
the current car's BODY_1..5 hashes.4E3FD1 maps their availability to the
shared ALLCARS_BODY_n style flags. This is separate from normal family-level
and Unique/manufacturer-part gates.

## Corrected paths

- AccessoryCategoryHeading receives the highlighted accessory name.
- Category highlight keeps the whole-car camera. Entering a part browser
  selects its existing original camera preset and interpolation.
- Color entries retain tier-specific cell positions. Cursor moves; the
  CurrentColor sample arrows stay fixed. Locked cells retain the preceding
  valid car preview. L/R cycles earned finishes; browsing previews without A.
- Vinyl selection follows layer, group, family where applicable, then design.
  Fully unavailable groups/families are omitted in career. X on an installed
  layer opens its remappable color channels. A advances channels and confirms
  their purchase together; B cancels drafts without changing the saved ride.
- Body vinyls use current-car compatibility and the shared original style
  flag. Group/family/layer thumbnails use original sprites and native order.
- Compact atlas packing reuses earlier shelves' gaps. No source image is
  resized and the previous atlas width/height cap remains in force.

## Validation entry points

`nfsu_career_retail` exercises menu/camera timing, palette preview/cancellation,
original navigation and icon witnesses, authored Cursor GPU vertices, finish
unlocks and vinyl channels. `nfsu_career_tests <retail> <fresh-output> garage-live`
checks actual geometry/skin publication, previews and restoration. The
`garage-pages` mode exports authored page frames for inspection. Full required
Default/Release, SDK packaging and ASCII results belong to the local build
receipt, with exact commands and source/archive hashes.

Source, artifacts and checkpoints remain local under the owner's Git hold.
Hardware input, resource budgets on the Switch, and live original UI timing
must be reported separately from these bounded code/data checks.

## Palette revisits and vinyl layer focus

Original4DAFA0 stores palette row/column pairs before changing the finish or
vinyl color channel. Returning restores that pair, including a locked cursor
cell; it does not reset the browser to the first swatch. The port now keeps
separate cursors for the three body/spoiler finishes and each vinyl channel.
Backing out of a later channel retains its current draft color until the
whole editor is cancelled. Only confirmed transactions change the saved ride.
An early-career locked swatch retains its cursor while the last available
draft color supplies the preview after returning to that channel.

Original4E3320 exchanges adjacent vinyl and color slots without changing
735DEC, the highlighted layer. Reordering in the port now keeps that focus.
`verify_original_garage` in `nfsu_career_retail` covers the finish/channel
revisits, cancellation, draft preview and layer focus. The additional slices
in `tools/career_palette_native.py` execute the original channel cursor
save/restore instructions before FEng rendering, with no call hooks.

## Installation navigation and vinyl conflicts

The original group/family distinction is intentional, not a platform extra.
4E40D0 routes Side/Hood through4E4C40's family selector, while Manufacturer,
Body and Unique open4E5700's design browser directly.4E61F0 opens the install
confirmation and reports an already installed design. Remapping colors is
4E3320's secondary action on an installed layer, not another mandatory
browser before choosing a design.50C9E0/50D420/50E340 establish the original
rim manufacturer, style and size flow.

4E3320's occupied-layer Replace/Remove choices are now a modal over the layer
selector. Moving the dialog choice preserves the underlying selected layer;
B cancels without a profile write. The separate port-only action page was
removed. The original GenericDialog art supplies garage confirmations.
The Switch maps dialog dismissal to B rather than adding a browser page.

4E4610 requests confirmation before clearing layers for Unique, replacing a
Unique with normal vinyls, or restoring a stock hood for hood vinyls.
4E4840 applies the acknowledged conflict action and leaves the group selector
active. Unused color slots remain until a layer's explicit Remove action,
which clears its design and remap colors.4E56B0 removes a matching design
from other layers when installing it; it preserves their unused colors.
The ordinary part transaction enforces this duplicate-design rule too.

The group/conflict/cancellation and duplicate cases run in
`verify_original_garage` under `nfsu_career_retail`. The `garage-live` mode
submits the layer modal and captures the Unique warning as well as purchase,
color and restoration flows. Ghidra decompilation and a read-only instruction
probe of4E4610/4E56B0 are preserved in the local r7 receipt under the owner's
Git/publication hold. That probe captures FEng dialog and RideInfo writes;
it does not hook the decisions or emulate an entire original garage.
