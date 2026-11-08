# Fractional start actors and the KR vinyl

## Start actors

StartCrowd previously selected an integer baked mesh pose. Half-speed starter
clips therefore retained one pose across multiple presentation frames.
The current path decodes the authored skeletal samples at load, interpolates
local poses at fractional phase and skins one stable vertex array per shared
clip/rate. The existing streamed_vertices backend copies it into fenced frame
slots. Runtime updates allocate no new mesh arrays. Scene placement, source
playback rate, camera timing, crowd retirement and restart ownership are retained.

DeltaQ uses normalized component interpolation without shortest-sign rewriting;
scalar pose fields interpolate linearly. The implementation shares its arithmetic
with the already recovered animation-mover sampler. nfsu_world_motion_tests
checks the authenticated original fractional sample witnesses. The source
constructor579750, including the half-speed branch5799AE, was inspected through
Ghidra. This does not claim a complete new recovery of every NIS controller.

nfsu_start_crowd_check checks fractional pose changes, repeated paused updates,
stable CPU pointers, retirement and restart. nfsu_kr_vinyl_retail covers the
separate cosmetic feature. PC gates and Switch packing do not certify hardware
frame pacing; that remains a console acceptance item.

## KR selection

In the garage open Visual, Vinyl, select a layer, then Unique, KR. It is a free
owner-requested whole-car design available without campaign unlocks. It uses
the existing Unique group and its mutually exclusive layer dialogs. Removing
it uses the normal installed-vinyl dialog. It occupies no existing retail part
or texture identity and gives no extra visual reputation.

Its stable installed hash is saved through the existing installed-part schema,
which also feeds career, practice and LAN car setup. Every peer that displays
KR needs a build containing this extension. No retail file is overwritten.
Retail part parsing itself remains unchanged; the cosmetic catalog appends a
separate descriptor, and a customized frame opts into the same extra identity.

The supplied RGBA logo is stored losslessly as ASCII palette/RLE/base85 data.
Original PNG SHA-256:
55c788f6ee341eb839340b3344cb228c97096cacd3b96e8c158526f820cf9f67

The compositor projects it onto the selected body's door surfaces and painted
hood through each car's actual mesh UVs. Each side fits its complete square
inside a connected painted panel, with an inset from the panel boundary;
whole-body height is not used as available door area. Glass, wheels and materials that do
not bind the painted body target are excluded. A carbon hood with its own
material therefore receives no painted-hood projection. RGB is blended while
the body's paint/gloss alpha remains intact. Color preview applies the same
overlay to the recomposed skin. There is no additional race draw or shader pass.

nfsu_kr_vinyl_retail installs it on all playable cars, checks actual body-target
changes and preservation of gloss, exercises live repaint, and saves/reloads a
real campaign profile. Optional PC captures are produced with:

```text
nfsu_career_tests <retail-root> --kr-vinyl <fresh-output-directory>
```

The atlas projection is a port-owned feature, not an original-game mechanism.
Large body-kit changes and each LOD should be inspected visually when adding a
new car or mesh family; availability on a roster is not a guarantee of identical
sticker proportions on every modified panel.
