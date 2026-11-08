# NOS filter render recovery

The original PC 1.4 USA executable is authenticated by SHA-256
`c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7`.
The read-only input `fx/filter.efx` has SHA-256
`fd731c437b4107c00fde2923d69d42d444190335bab7a8f20f2ee4c9b7263988`.
The original loader at 40E180 selects this effect as entry 6 and decodes
its bytes with XOR AA. No retail shader source is distributed here.

## Verified path

- 409580 flushes world geometry, calls 40CD40, then submits actors and cars.
  The Switch applies the filter after its static world list and before cars.
- 40CB80 allocates two half-resolution A8R8G8B8 textures. 40D370 reduces
  the scene using the four half-source-texel offsets at 7024B8.
- 40D090 generates clip-corner vertices, each with four UV pairs. UVs are
  corner coordinates minus tap index times that corner's blur offset.
  The second radial pass multiplies the offsets by four.
- The authenticated effect passes clip positions and UVs through unchanged,
  samples four linear/clamped textures and sums their supplied weights.
  The blur caller supplies equal weights. 40D6E0 composites the result using
  the byte-valued constant blend factor and its inverse.
- The original unadjusted clip-corner geometry uses D3D9 integer pixel
  centers. Maxwell fragment coordinates are half-integer. The Switch shaders
  subtract 0.5 before normalizing each pass; the CPU preview does the same.
  Previously the implementation normalized gl_FragCoord directly and shifted
  every pass relative to the original sampling lattice.

The raster convention is documented by Microsoft:
https://learn.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels

## Reproduction and limits

From the actual checkout, using standalone Python with Unicorn installed:

```text
python tools/speed_blur_render_native.py --retail-root "E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground" --output build/blur-render-fresh.json
```

The output must not exist. The witness executes original radial vertex
arithmetic from 40D0C5 to 40D1AF, outside graphics calls, checks the original
effect contract and the Switch pixel-origin conversion, and reauthenticates
its inputs. Expected vertex geometry lives in this executable witness.
`nfsu_speed_blur_tests` checks the preview's first-pixel sampling regression.
`tools/speed_blur_original.py` separately compares original envelope, offset
and alpha arithmetic to the portable oracle.

These checks do not execute the original GPU or Switch GPU. They identify a
concrete sampling mismatch; they do not establish that every visible NOS
complaint is fixed. Hardware image parity remains unverified. The race renderer uses the original ambient high-speed contribution and NOS
release tail after the owner enabled them on 2026-11-08. Pause holds the
envelope; restart and leaving active racing clear it. The optional NOS-only
adapter remains available to diagnostics, not the race call site.
