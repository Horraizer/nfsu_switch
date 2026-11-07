# Garage preview follow-up, 2026-10-20

Role: worker, Codex root owner integration. Direct owner scope: garage
camera moves, but tuning does not change visually; fix and send one NRO.
Worktree: build/day20261008-root-combined, branch codex/draw-distance-original.
Local-only integration. No commit, push, merge or publication in this pass.

The owner checked only garage in garage-feedback-r20. Preserve the sealed
r20 artifact and its evidence; other r20 hardware criteria are still open.
The r20 run3 capture records only the initial component-bank publication,
not completion of later part banks. It does not identify a specific part.

The background loader used the process default core and priority0x3B while
the main thread continuously prepares frontend frames. Starvation is a
scheduling hypothesis, not proven by that old log. Asset work now uses the
other application core at priority0x30, below the audio output thread.
New GARAGE_JOB and GARAGE_BANK fields distinguish job start, CPU preparation,
selected resident variant and main-thread GPU publication time.

The initial GPU replacement previously occurred on installed variant0;
selection was applied afterwards. adopt_garage now validates the requested
resident variant, keeps the old borrowed command owner alive, and applies
the current highlight before GPU replacement. Category preparation no longer
adds a duplicate copy of the already-installed variant.

The garage diagnostic in nfsu_career_tests now starts real asynchronous
preparation while advancing the old scene. It checks invalid-publication
transactionality, old command lifetime, and the actual vertex/index/texture
payload changes for stock, front bumper and rims. It checks the selected
rim at the publication boundary and subsequent resident selections without
new uploads. Its images are under the uniquely named scratch run.
This desktop test cannot reproduce Switch scheduling or certify latency.

Evidence: build/garage-preview-r21/root-*; required host/SDK/ASCII matrices
and final source seal follow. Do not call the garage hardware-fixed until
owner confirms visible part switching on this exact binary. Remaining r20
finish/results, audio, tyre-mark and flare acceptance is not superseded.
Race engine/music mix observation is preserved separately under r20; its
constants are unchanged in this follow-up.
