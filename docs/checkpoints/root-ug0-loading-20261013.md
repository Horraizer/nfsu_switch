# UG0 loading-r2 correction

Owner reported indefinite loading after selecting UG0 in complete-r1 and
requested correction. No delegation, source publication or merge.

Hardware log: main-relative build-artifacts/draw-distance-20261008-root/
ug0-complete-r1-final/nxlink-ug0-complete-r1-run1.log. Grid setup completed;
last preparation line was PARTICLES ON. Menu reopened twice; no fatal error
was printed. Session exited normally. The old log cannot prove the exact
stalled phase; no claim of a diagnosed deadlock.

Confirmed implementation defect: Progress timestamped before full software
loading-art rendering. On hardware slower than the100ms interval, each next
poll could draw again immediately. The static retail background was fully
rasterized for every poll. Cache those two static sprites once per size,
retain authored moving bar, and timestamp after presentation. Cache is freed
before race GPU upload. Add bounded1Hz phase/elapsed/heap logging plus grid
begin/ready and cancellation messages for the next hardware run.

Host Release diagnostic:120 draws, reference15.9297ms mean, cached0.219877ms
mean. This measures drawing only, not Switch load time. Eighteen comparisons
at1280x720/640x480/back to1280, with padded row strides and transition times,
match every byte against uncached original rendering. No gameplay change.

Checks: targeted viewer build and loading mode PASS; Switch SDK race NRO
build PASS; race package/frontend lifecycle2/2PASS. Changed source/scripts
ASCII and git diff --check PASS. Initial host build attempt failed because
ranlib could not create temporary archive (permission denied); no concurrent
build process remained, retry completed. Full known-failing matrices were
not repeated for this loading-only correction, per owner's targeted-test rule.

Base71f15ac; pending local commit records source. Artifact tag ug0-load-r2.
Owner confirmed Netloader open and authorized resend. Actual loading-time
improvement and successful entry into UG0 remain hardware criteria.
