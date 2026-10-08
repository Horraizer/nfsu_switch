# Current visual candidate profiling preparation

Owner request: prepare CPU/GPU profiling build; transmit only on explicit command.
Parent: b0576b5 (distance-r2), with actual owner run recorded in
build-artifacts/draw-distance-20261008-root/hardware-run-r2-results.json.

Artifact: nfsu-profile-r1.nro. Launch with --profile-frame (without --live):
menu and authored free-drive Skyline remain the entry path. The existing flag
now reaches free driving instead of being discarded at its run() call.
Without this flag the build retains normal timing behavior.

Preserve all current rendering, effects, resolution policy and handling.
Use existing CPU stage timers and one asynchronous GPU timestamp pair covering
the complete submitted scene, effects, upscale and HUD. No new GPU pass barriers.
Add CPU road-reflection selection and race-visual preparation timers as children
of combine, not additional parent costs. The latter includes environment capture
selection and headlight setup. GPU subdivisions are not measured in this build.

After renderer.close drains pending GPU queries, print RUN_PROFILE distributions,
CPU/GPU worst tick identities, sample windows, invalid/dropped counts to nxlink.
RUN_STATS retains complete normal frame/presentation interval distributions and
measured clocks. Profile overhead means this run diagnoses cost rather than
replacing the normal-build frame pacing baseline. No GPU utilization claim.

Checks: SDK build and CTest 49/49 PASS (profile-switch-build.log); native statistics
and profile pages 2/2 PASS (profile-native-tests.log). Fresh dependent native
libraries built. Twelve runtime visual frames and two dense camera probes on both
tracks pass with profile timers, positive child durations and parent containment.
Their counts/far plane/uniform/reflection outputs match the saved nonprofile probes.
This count equality is not pixel equivalence or hardware timestamp validation.
Full native matrices were not rerun; existing baseline limitations remain.

Local only; no push or merge. Package manifest binds exact source hashes and NRO.
Hardware upload and measured GPU/CPU profile remain pending the owner's command.
After upload, drive the same event1 route without pause, then Plus to return to menu.
Read final RUN_PROFILE and RUN_STATS; do not infer a bottleneck before that evidence.
