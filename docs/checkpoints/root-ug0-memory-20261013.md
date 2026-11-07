# UG0 memory-r3

Owner explicitly requested +10 MiB after loading-r2 failed on actual Switch.
At ee17f30, nxlink-ug0-load-r2-run1.log records HUD staging requested2097152,
resident277803008, limit279089152. Grid and race session initialization
completed. This is the programmed GPU allocation cap, not exhaustion of
all system memory. Previous session exited; its log remains preserved.

Raise canonical base GPU budget256 to266 MiB. Existing authored-mip and
MSAA allowances remain additive. Diagnostic pages now label the base budget
and read the constant. No streaming/CPU budgets or resource ceilings change.

Native targeted memory_arena/gpu_upload2/2PASS; real SDK race and track
build PASS; race package/frontend profile2/2PASS. Full known-failing
matrices not repeated per owner instruction. Hardware race entry remains
unverified. Owner confirmed fresh Netloader readiness and authorized send.
Local-only correction; no source publication or merge.
