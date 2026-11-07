# Track textures in GPU format (#171)

The opt-in GPU path keeps each DXT1/DXT3 top level as BC1/BC2 blocks and
uploads it directly through deko3d. It does not allocate decoded RGBA for
those textures. A8R8G8B8 still becomes RGBA; P8 remains unusable as before.
The default desktop preparation and every existing render pin are unchanged.

Reproduce with a readonly installation (replace X:/Games/NFSU):

```text
nfsu_probe track-stream-path X:/Games/NFSU/Tracks/STREAML1RA.BUN X:/Games/NFSU/Tracks/TRACKB1003.lzc 1003 --hand-path 1003 --cost prepared --gpu-textures --list none --texture X:/Games/NFSU/Tracks/TEX1003TRACK.BIN --texture X:/Games/NFSU/Global/INGAMEB.BUN --texture X:/Games/NFSU/Global/GLOBALB.BUN
```

Each `gpu-budget step` reports retained CPU streaming payload, preparation
peak, a GPU peak bound and the largest section. A failing row makes the
command return 3. `--frame-every` is refused with `--gpu-textures`: the CPU
renderer requires decoded texels. Without the new flag the old output and
pins are unchanged.

For the 1003 hand path at 60 steps/s, retail policy, all 6903 steps fit.
The peak is reached at step 76; 78 sections stay resident through the lap.
The prepared currency reports zero over-budget and zero unplaced steps.

| Quantity | Bytes | Switch plan ceiling |
| --- | ---: | ---: |
| Texture payload, previously RGBA8 | 100564480 | - |
| Texture payload, native DXT plus remaining RGBA8 | 14096320 | - |
| Geometry and instances | 13674390 | - |
| Retained CPU streaming payload | 27770710 | 50331648 |
| CPU preparation peak including duplicate texture set | 41867030 | 50331648 |
| Largest prepared section | 12086944 | 16777216 |
| GPU peak bound, including staging and executor reserve | 135045120 | 268435456 |

The prepared run's sequence SHA-256 is
`7A57E9ED1B5695E3723F72DE65195179037247223720C0EB587A0C237548AA3E`;
its cost SHA-256 is
`BFB45D2F0ED56DD5BF7C04A73EA163C36BBE3EA53B545C20A6CA7465D4B0689D`.
With the default table currency, the same 6903-step resident payload report
fits, and the existing retail sequence remains
`F64185A1E34258C03126E86EC80DC021716D3F0FE509933F6A8B0BD4D18CB643`.
That currency retains the original one startup over-budget/unplaced step;
it is not a prepared-payload budget failure.

## Ownership and release

This implements the issue's native-format alternative. Prepared sections
retain compressed CPU blocks until section eviction, rather than retaining
RGBA copies. The temporary texture set is released after preparation copies
its wanted textures into the scene; its overlap is included in the CPU peak.
When a section is evicted, its prepared CPU payload can be destroyed after
its bytes have been copied into upload staging. GPU staging survives until
the upload fence completes; the image survives until both that fence and
its last drawing fence complete. `UploadLedger` enforces those GPU lifetimes.
The native-section synthetic test uploads, retires staging, evicts, waits
for last use and checks all allocations returned. A GPU arena one byte too
small refuses the whole section and rolls back buffers, images and staging.

## Scope of the figures

These are payload-plan counters, as in TRACK_BUDGET.md, not a process heap
measurement. The desktop diagnostic also holds input file bytes, parsed
reports, strings, vector capacity and cached resident-set measurements.
Those host inspection allocations are not claimed to occupy the Switch
streaming arena. #54 must bound loader workspace and measure actual console
memory separately. The largest section fits one region; the complete set
uses the three-region arena, not one 16 MiB region.

The GPU figure places each image using `bounded_image_layout`, rounds buffer,
image and staging extents to 4096 bytes, includes tight and padded staging
copies simultaneously, and reserves 64 MiB for the bounded executor's
commands, uniforms, descriptors, shaders and 1280x720 presentation/depth
images. This reserve applies to the bounded executor (8192 resources, 2047 draw uniform slots for #54);
the whole resident set's all-visible draw list is not a promised executable
frame. #54 selects a bounded visible slice. The real backend queries deko3d
image sizes and checks every memory block against 256 MiB, refusing before
an over-budget allocation. These placeholders and bounds are not measured
hardware residency or a claim of native-format visual equivalence.
