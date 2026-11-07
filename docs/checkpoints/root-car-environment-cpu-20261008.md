# Race car environment CPU checkpoint

Owner authorized body reflections after the headlights CPU image. Sequential
root work, no workers or reviewers. Own local tree is
build/day20261008-root-combined at b009de72 with prior dirty work preserved.

Opt-in RenderOptions::car_cube replaces the static procedural environment
with six borrowed scene captures. Reflection direction follows car.efx local
normal/view and WorldView mapping. Existing common cube lookup interpolates
face-local texels. Texture alpha and material alpha mask the sampled term.
The existing approximate diffuse/specular harmonic path remains unchanged;
this is not full original material or lighting parity.

Original capture setup uses primary camera face bases, unit focal scale,
128 square faces, .5/64 clip planes and body origin plus world vertical .95F.
The actual read-only speed.exe trace at 5707B0 and constant 704AD0 is saved.
The diagnostic captures the loaded track and sky. Original scene-category
selection, live player selector, sprites, traffic and update schedule remain
limits. CPU track renderer now honors its supplied explicit capture camera.

Validation: fresh targeted build passed; nfsu_track_render_tests 1/1 passed
with six-direction cube sampling, diffuse/material alpha masks and camera
setup checks. Same-scene capture changed only the vehicle screen bounds.
Artifacts/source hashes and limitations are in runtime-relative
build-artifacts/combined-20261008-root/car-environment-r1/checkpoint.json.
The exact capture driver is preserved there; its original execution location
is this worktree's build directory. before.png and after.png are actual CPU
renders, not generated artwork.

Still local, uncommitted and unpublished. No new Switch NRO or GPU integration.
Full Default/Release/SDK/package/hardware gates were not run. Prior shadow
shader interface failure remains. Next is GPU capture/consumer integration
and frame timing; the current image is a CPU prototype only.
