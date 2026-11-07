# Garage lighting pixel contract

The original PC fx/car.efx compiles vs_1_1/ps_1_1. Its vertex outputs use
COLOR0 for diffuse RGB/alpha and COLOR1 for specular RGB/environment power.
Microsoft specifies saturated0..1 inputs for those pixel color registers:
[ps_1_x registers](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-1-x),
[HLSL shader inputs](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-writing-shaders-9).

The FE CPU rasterizer and Switch fragment shader enforce that input range
before the texture/specular product. Their precombined diffuse/alpha already
include car.efx's final factor2, so those lanes saturate in0..2. Specular RGB
and environment power use0..1. The actual-raster case in nfsu_frontend_tests
checks negative diffuse/specular and out-of-range highlights. Raw native
vertex polynomial witnesses remain unclamped, as they precede this boundary.

This prevents a negative light color from subtracting a highlight. It changes
no garage exposure, base texture, paint color, lamp strength or race shader.
It does not establish full original lighting parity: the bounded original FE
startup assumptions described in FRONTEND_66.md remain unresolved.

## Light rig and camera

Original542E40 overrides its fallback basis with scene+0x40 (the active
primary CView), inverses it and transforms the FE directional light table.
The previous port retained directions recovered only for the main entry
camera. It therefore kept lighting fixed while garage part cameras moved.
FrontendGpuScene now supplies its actual camera every frame. The intensity
table, SH accumulator/normalization and road-light inputs are unchanged.

nfsu_frontend_tests compares all SH lanes against independently executed
original542E40/544390 for the native entry camera and synthetic identity and
quarter-turn CViews. tests/frontend_camera_lighting_native.inc contains the
witness values. A fixed startup record is the negative control. The final
appearance on the Switch GPU still requires hardware acceptance.
