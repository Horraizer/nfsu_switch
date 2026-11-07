# Flat lighting and uam's derivative constant (issue #147)

The console failure recorded on [PR #191, comment 4116](https://git.accad.pro/murzintsevav/nfsu-switch/pulls/191#issuecomment-4116)
is consistent with missing flat lighting: 819880/921600 pixels within +/-2,
28.029007 dB RGB PSNR, maximum channel error 160, and a matching CPU pin.
The amplified difference described in comment 4119 covers the car's surfaces.

## Cause

The GLSL and `src/render.cpp` use the same eye-facing geometric normal,
normalized light `(0.35, 0.85, 0.40)`, ambient/diffuse factors `0.35/0.65`,
and integer shade rounding. The vertex shader places the same positions;
the fragment shader derives the normal from their screen derivatives.

The installed uam compiler emits this for `dFdy(inWorld)`:

```text
DCL CONST[0][0]
15: MUL TEMP[11], CONST[0][0].xxxx, IN[0].xyzz
16: DDY TEMP[12].xyz, TEMP[11]
```

The compiler-generated constant buffer is 16 zero bytes in both original
fragment DKSH files. This is TGSI CONST[0], not the explicitly bound Frame
UBO (TGSI CONST[1]). `dFdy(0 * inWorld)` is zero, the cross product is zero,
`has_normal` is false, and `shade` stays 256. Both fallback and textured
surfaces therefore bypass lighting. Neither changing Frame.light nor
changing the light formula could repair the absent normal.

Upstream source inspection at
[uam 5a5afc2](https://github.com/devkitPro/uam/tree/5a5afc2bae8b55409ab36ba45be63fcb73f68993)
traces the multiplier to `STATE_FB_WPOS_Y_TRANSFORM` in
`mesa-imported/state_tracker/st_glsl_to_tgsi.cpp` (the dFdy lowering).
`glsl_program_get_constant_buffer` in `source/glsl_frontend.cpp` exports
the parameter values without initializing that Mesa state. `source/dksh.h`
defines the compiler constbuf offset/size used by the correction.

A desktop reproduction at the earlier wheel-placement baseline renders the
painted stock scene with flat shading and again with shading disabled,
changing nothing else. The CPU TGA pin for that historical comparison was
`52B9303A822F82242717B3A2F363BE6584E4A24B533B69B6AA78EED6DFBED994`.
The comparison is 817808 exact pixels, 819954 within +/-2, maximum 158,
PSNR 27.99147884 dB. This closely reproduces the console's failure; it is
an isolation of the lighting error, not a hardware rasterization emulation.

## Correction and guards

`tools/uam_derivatives.py` initializes the compiler multiplier to `1.0`
after uam compiles and before the manifest check or RomFS packaging. The
original `.dksh.raw` and TGSI remain beside the corrected DKSH for inspection.
No shader instruction, explicit uniform, CPU pixel, threshold or runtime
readback changes. A derivative sign reversal would also reverse the raw
normal and is canceled by the eye-facing orientation; a nonzero unit scale
restores the intended geometric plane on either framebuffer origin.

This is deliberately not a general DKSH patcher. It requires one bounded
program, the expected stage, the sole compiler-state declaration/read,
the exact multiplier-to-DDY sequence, a 16-byte compiler constbuf aligned to 256,
zero unused lanes and a multiplier of zero or one. Unknown layouts fail
the build. The vertex stage must have no compiler constants and is copied
unchanged. A future compiler that changes this lowering requires revisiting
the workaround, rather than silently guessing another offset.

Every manifest build stamp verifies the corrected constants; Switch CTest
also verifies all three packaged-stage inputs independently. Host tests
exercise the correction, byte preservation, idempotence, malformed ranges,
unexpected state uses, zero-multiplier rejection and the command-line tool.
The existing RomFS tests establish that the NRO contains those exact shaders.
Python 3.8 or newer is a host build/test dependency, with no target runtime
dependency. No game data enters the correction or its tests.

## Hardware acceptance

The corrected NRO must still be run on the owner's console. The required
result remains CPU pin MATCH, at least 99.0 percent within +/-2 on RGBA, and
at least 35 dB RGB PSNR over the whole frame. Desktop lighting isolation and
compiled-data checks alone do not establish those hardware thresholds.
