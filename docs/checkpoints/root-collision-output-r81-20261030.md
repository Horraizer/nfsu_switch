assignment: Owner interface/car/object/sound audit, collision output category
phase: implementing
ownership: Codex root, active
base: validated and archived r80; source freeze released
branch: codex/draw-distance-original; Git/index/ref/board HOLD
head: ea4d472c9c399265370fa10be94736b1121771ad
saved: local only; no independent review, hardware or console send

Actual531350 ->530740 multiplies spatial attenuation and category7 before
the global output gain and bank curve. Current world impact/scrape output
bypasses category7; smokeable graph also receives a fixed unprofiled gain.
The source gain order matters: applying a bus after the bank curve changes
integer rounding. Recover the category curve and supply it before the curve,
while keeping still-unrestored spatial/global/admission producers explicit.

Research root-collision-category-r2 executes actual530740 with supplied unity
spatial gain, listener0 and original profile initialization. r1 without the
existing fixture's explicit zero-page slice refused; no pass is claimed for
that setup. Do not mistake the zero initial settings3E1C field for a genuine
runtime gain:5365E0 later writes it from AudioMain+8's separate input source.
Its value depends on the original settings producer, still under research.
Do not invent a global gain from a target listening loudness.

Collision spatial profile6F47A0 is type4, near10 and quadrant far80/30/30/30.
Base52E3F0 sets attenuation32767. Existing audio312_contacts.spatial_witness
executes the actual spatial/profile/category dispatch; its listener/actor
and output boundary are explicit. The physical contact point already exists
in VehicleContactObservation; the current RaceAudioSnapshot drops it.
Restoring it and full spatial/runtime/admission is a connected next stage,
not evidence that the category correction alone completes audio fidelity.

Next: extend race_audio_mix_native category7 publication/consumer fixture,
connect pre-bank gain, compare production output and mandatory full gates.
Retail read-only; no original launch/capture/UI/agents/review/Git writes.
Updated UTC: 2026-10-30T01:47:37.786525+00:00.


Implemented continuation: category7 follows the original profile curve and
is supplied before the bank gain product for world impacts/scrapes and
smokeable input. race_audio_mix_native now observes actual530740 followed
by bank35, alongside consecutive original category publication. The scalar
bank product also retains its binary32 store before nearest-even conversion.

The6F47A0 collision spatial profile now reaches the scheduler and Mixer.
VehicleContactObservation already retained the physical point; race snapshots
now copy it, including car pairs. Pair episodes retain that supplied point
for stale scrape output. CollisionAudioPosition uses actor distance and
camera-direction pan independently. Whole native531350 output is compared
with supplied listener/record states; the compiled test follows spatial to
configured gain to actual bank35 and emitted pan. No body-center fallback or
new physical-state mutation was introduced. Props still lack this separate
spatial producer and keep their explicit port output policy.

Fresh preferences are now traced: audio_position_native.prepared supplies
AudioMain+8 from the actual4AA21E preference initializer.5365E0 writes the
impact-global publication. The standard default is consumed by impacts and
smokeable input before the bank;531080 scrape does not consume that stage.
The earlier zero3E1C observation was only an incomplete settings fixture.
Special override flags and nondefault settings remain separate/open.

Native position r1/r2 used explicit unity global input; r3 includes that
and the fresh impact-global input. These are declared finite contact/actor/
listener states with final dispatch observed, not natural record admission
or Player/PCM equivalence. The native corpus is kept in its compiled fixture,
not copied into this document. Target-r3 passed the chosen unit, retail,
category, fresh-preference and position checks with before/after manifests.

Next: freeze this connected output group and run every mandatory Default/
Release/SDK/ASCII gate before archiving. Actual52FB60/531FF0 admission,
5311D0/536C80 transients, natural body-pair identity/lifetime, full traffic,
remaining effects/HUD/magazine and final portable acceptance remain open.

Updated UTC: 2026-10-30T02:56:18.006706+00:00.

## Validated local r81 boundary; full owner scope remains open

Updated UTC: 2026-10-30T03:28:24.352041+00:00.
Default and Release passed with no skipped retail tests. SDK packaging and
shader checks passed. The frozen source identity is root-code-freeze-r1.json,
not the unchanged HEAD. Exact results/logs are in fidelity-r81 receipts.

Command: `cmake -S . -B build -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

Command: `cmake --build build -j 5`

Result: exit 0.

Command: `ctest --test-dir build --output-on-failure -j 5`

Result: exit 0.

Command: `cmake -S . -B build-release -G Ninja -DNFSU_RETAIL_ROOT=E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground -DCMAKE_BUILD_TYPE=Release`

Result: exit 0.

Command: `cmake --build build-release -j 5`

Result: exit 0.

Command: `ctest --test-dir build-release --output-on-failure -j 5`

Result: exit 0.

Command: `/usr/bin/cmake -S . -B build-switch-fidelity-r81 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/c/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -DNFSU_RACE_BUILD_TAG=fidelity-r81 -DNFSU_RACE_FXAA=ON -DNFSU_RACE_MSAA2=OFF -DNFSU_RACE_MSAA8=OFF -DNFSU_RACE_FPS_OVERLAY=OFF -DNFSU_SWITCH_SPEED_BLUR_PROBE=OFF -DNFSU_SWITCH_DEKO_DEBUG=OFF -DNFSU_SWITCH_FRONTEND_PREFLIGHT=OFF`

Result: exit 0.

Command: `/usr/bin/cmake --build build-switch-fidelity-r81 --target nfsu_switch_race_nro -j 2`

Result: exit 0.

Command: `/usr/bin/ctest --test-dir build-switch-fidelity-r81 --output-on-failure -R "^nfsu_switch_(race_nro|race_embedded_shaders|shader_[a-z_]+)$"`

Result: exit 0.

Command: `C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -B tools/collision_position_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

Command: `C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -B tools/race_audio_mix_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

Command: `C:/Users/horra/AppData/Local/Programs/Python/Python312/python.exe -B tools/audio_position_native.py E:/Games/nfs/Need For Speed - Underground/Need For Speed Underground`

Result: exit 0.

No independent review, original launch, hardware acceptance, console send or
Git/index/ref/board mutation occurred. NRO is UNSENT. Code freeze is released
after archive/identity checks; later edits invalidate these gates. Actual
52FB60/531FF0 record admission, transient commands, nondefault settings and
the other FIDELITY_STATUS criteria remain open.
