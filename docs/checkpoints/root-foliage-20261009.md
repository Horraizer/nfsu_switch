# Foliage original comparison and AA selection

role: worker
agent: Codex root, sequential owner-led work
phase: investigating
worktree: build/day20261008-root-combined
base: 7c42c30e0a6dc314f3d4eef50d7058fc27468c08
saved: local only, no push/merge/reviewer

Owner rejects MSAA8 as too slow and retains MSAA2/FXAA for consideration.
At7c42c30, msaa8-r1 hardware driving:1155 GPU samples mean16.077317ms,
max18.561406ms; presentation1154 intervals mean28.148745ms, P9934.25ms.
No invalid/dropped GPU sample; normal return to menu and exit. This is
a historical diagnostic-build result, not visual acceptance. Exact log
identity and owner verdict in msaa8-r1-results.json under root artifacts.

Owner asks why grass/foliage shimmer, compare original before another fix.
Read-only retail world.efx SHA matches documented provenance. Its sampler
and fixed technique both specify LINEAR min/mag/mip. Raw original STREAM
pack metadata validates ORG_GREENLEAFYTREE64x64/4levels,
ORG_BUSHGOLDGRASS64x64/4levels, ORG_PALMFRONDTOPBUSHIER128x128/5levels,
TRN_GRASS128x128/5levels and TRN_GRASSC256x256/6levels.
Current texture_decode.cpp discards lower levels for compressed GPU
textures; Deko layouts default to one mip and uploads only level0.
Samplers leave mipFilter at None. This confirms a missing source feature.
Main ordinary world mask0 also keeps legacy nearest policy; wet mask0x40
is explicitly linear. A specific visible foliage draw binding has not
yet been captured; do not claim every leaf uses the same material.

Missing minification filtering plausibly causes the reported shimmer.
Its visual contribution is not yet measured. Original shader declaration
does not prove every PC runtime quality override. Preserve original cutout
alpha and authored mips, rather than inventing blur or alpha thresholds.
Next proposed implementation: retain original world mip chains end to end
and use original linear mip/min/mag on applicable world materials, followed
by same-camera motion comparison. No renderer changes made in this step.
Evidence: foliage-root-original-findings.json and foliage-root-stream.txt
in build-artifacts/draw-distance-20261008-root. Earlier textures command
was the wrong parser for raw packs and failed; pack-set completed instead.
