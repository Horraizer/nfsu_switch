# Car shadow terrain heights on Switch

## Original behavior

The PC 1.4 USA speed.exe identified by CAR_SHADOW.md constructs a four by
four terrain grid at 56EB33..56EFD9. Each point retains its own 440A40 road
height or wheel-plane fallback, plus the original 0.04 lift. Inner points
5, 6, 9 and 10 receive an additional 0.08 before validity checks. The
three strip rejection counts and quantized height/alignment alpha are
separate from those vertex heights. There is no countdown or first-wheel-
contact visibility delay. CAR_SHADOW_FIDELITY.md records the original
instruction evidence; its descriptions of the former planar port are
historical, not the current projected path.

The projection is checked by nfsu_car_shadow_projection_tests, including
original executable ground-cache and transformed-grid witnesses. The
selected stock solid bounds were compared to the original constructor in
the surface/shadow investigation. Whole-frame visual parity is not implied.

## Compiler failure and correction

A runtime-indexed uniform vec4 component in car_shadow.vert.glsl was
lowered by the installed UAM compiler to a constant X swizzle. For example,
fallback[i] for i in 0..3 became CONST[3][0].xxxx in TGSI: the address
arithmetic shifted i*4 right by four and never selected Y, Z or W. Each
row consequently became flat across its four columns. This also erased
the selected inner lifts. On a longitudinal slope the shadow can penetrate
the road on one side and float above it on the other. This affects rivals,
the player and projected neon because they share the shader.

Select the complete row first, then select X/Y/Z/W with explicit scalar
branches. Preserve the original per-point heights, footprint, strip mask,
alpha, sampler and depth state. No extra draw, texture, framebuffer,
per-frame upload or artificial lift is introduced. Animated player start
poses also use the projected path; a planar placement supplied no terrain
height payload to this shader.

## Regression and limits

nfsu_switch_shadow_height_contract evaluates the actual generated UAM
TGSI with distinct, sloped and lifted terrain heights. All vertex IDs are
executed through projection to POSITION. It rejects missing output,
uninitialized reads, unsupported instructions, and a negative control
that replaces column selection with X. This is a bounded compiler-IR
check, not a general TGSI implementation or Maxwell hardware execution.
The pre-correction compiled shader fails the same height comparisons.

The career production scene check verifies every rival's initial shader
packet against its projected terrain heights and visibility fields. The
optional diagnostic frame indexes rivals by roster position, including
multiple cars of the same type, rather than selecting the first type match.

A Switch appearance check remains required: inspect shadows during the
low start-camera cuts, countdown, launch, and changing road slope. Stock
and tuned rival geometry must keep their own authored shadow textures and
footprints. Do not claim hardware acceptance from a successful NRO build.
