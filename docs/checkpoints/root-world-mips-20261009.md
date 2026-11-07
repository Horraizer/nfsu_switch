# World mip comparison candidate

role: worker
agent: Codex root, direct owner assignment
phase: awaiting hardware evidence
branch: codex/draw-distance-original
base: d5ee8d5
saved: local only; owner requested sequential direct work without reviewers

Original authored world mip levels are retained with original alpha, uploaded
per level with bounded SDK layouts and sampled with linear mip filtering.
Frontend/car defaults and original top-level payloads remain unchanged.
Additional memory allowance is derived from retained data and actual layouts.

Validation: focused native 4/4; real retail event1/event8 mip and top-level
identity comparisons passed; Switch SDK 51/51 passed. ASCII and diff checks
passed. Logs are under build-artifacts/draw-distance-20261008-root.
Full native Default is still running and has reported a synthetic upgraded
replay pin failure in nfsu_performance_levels_retail. Full native acceptance
is not claimed. Release status remains pending. No pins changed.

Owner requested fewer tests, only necessary checks, then reported Netloader
ready and asked for the comparison build. Candidate tag mips-r1, FXAA on,
MSAA2/MSAA8 off, SDK debug on. This is a hardware comparison candidate,
not a release/merge verdict. Actual foliage shimmer, memory and GPU time
remain unverified until the Switch run. No automatic merge or publication.

## mips-r2 correction

Owner rejected mips-r1: free drive returned to menu. The actual socket log
reported wet race command cache exceeds 8 MiB. Root reproduced the exact
refusal on PC with wet reflections, visuals, particles, glows, original
distance and authored mips together. Earlier track-only mip tests omitted
this application configuration and did not establish successful race load.

The added mip count grew TextureUploadShape from 40 to 48 bytes. Placing
it in existing alignment padding restores 40 bytes without raising the
cache limit or removing effects. Before/after logs under the artifact root
show the refusal and successful unchanged-budget load.

The permanent Skyline lifecycle test now covers both events with full
Switch visual settings and authored mips, 240 simulation steps, reflection
frames and preserved frontend lifecycle. Final native test: 0 failures.
Draw execution validation: 0 failures. Switch SDK: 51/51 PASS.
Full Default at mips-r1 ended 134/169 with 35 failures; Release not run.
No claim of full native acceptance. Owner requested only necessary checks;
this correction uses the reproduced lifecycle regression and affected SDK
checks, with no repeated full matrix. Hardware verification remains pending.
