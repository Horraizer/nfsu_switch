# Minimal instrumentation comparison

Owner authorized one sequential control run against streaks-rel-r1.
Source parent: b53850ac529d9193546e4511b58004cc45754afa.
Local only; no worker, reviewer, push or merge.

--ab-minimal implies saved-route replay and the original total GPU timestamp
pair. It bypasses eight added pass timestamps and their fragment barriers,
CPU substage timers/histograms, per-frame heap traversal sampling, and menu
pass profiling. Canonical state and camera validation, CPU total/interval
samples and buffered total GPU rows remain. No graphics/physics change.
Unavailable stages are omitted from rows, not interpreted as zero cost.
The analyzer compares the intersection of actually available metrics.

This measures detailed-minus-minimal instrumentation, NOT all profiling cost
or an uninstrumented production frame. Vsync waits can absorb CPU savings.
Input replay, state hashing, total timestamps and RAM rows still cost work.
Do not report unchanged presentation timing as zero CPU overhead.

Validation: race NRO and actual embedded-shader gates passed. Initial profile
source contract failed its old menu-call spelling; updated to the explicit
minimal-mode exclusion and added a negative control. All ten source negatives
passed. Parser exercised all4053 existing paired rows with reduced columns:
only totalGPU/work/interval metrics, exactly equal values. Source toggle guards
verified pass barriers skipped, total pair and route equality retained.
No broad native matrix; hardware replay and matched comparison pending.
Next: send nfsu-streaks-min-r1.nro with --ab-minimal, compare to saved Release
replay; route identity9ca6c8fac4f2cda6,4219frames. Preserve SD recording.
