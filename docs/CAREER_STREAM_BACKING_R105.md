# Complete backing for native race zone transitions

## Confirmed first-sprint failure

The owner's photo from r104 reads:

    FREE DRIVING: race requested a section outside the uploaded event-1 set

The root-menu notice prefix was generic. This was a career race error,
not proof that the owner selected free driving. Earlier SD journals from
r102-r2 already established a valid first-place event2 finish before the
continuation failed. The photo identifies the previously missing cause.

The first sprint uses track1102. A native residency sample beyond its
finish requests zone707/section724. The permanent race set excludes this
section, which contains real geometry and scenery, not just metadata.
RaceRun refused that request because its backing consisted only of the
startup permanent set. A synthetic finish that stopped inside the old
footprint could not reproduce this error.

## Original rule and port policy

Ghidra444B50 builds requests from fixed sections, the track extra list,
both live slots' zone lists and the initial track-byte set. The original
rule therefore permits loading a zone outside the startup footprint.
The existing native residency model, its pool policy, lookahead, request
limit and release decisions are retained.

The Switch renderer pre-uploads a bounded resource bank. Sprint, drag,
time-trial and free-driving loads now back the union of authored zone
sections in addition to the permanent set. This is a port cache policy,
not a claim that the original preloads the entire region. It avoids race
time filesystem reads and GPU resource replacement at the finish.

TrackSlice records which backing policy it prepared. RaceRun validates
that policy, record count and actual input-byte coverage. Startup is
checked against the native permanent set; later loads and releases must
remain within real prepared backing. Missing sections still fail with
their track and section identity. No successful load of absent geometry
is fabricated, and the coverage guard is not disabled.

The main view retains original camera-zone visibility. Reflection captures
retain their existing local64m frusta. Expanded backing can supply nearby
capture geometry that was absent from the smaller bank. This is not a
claim of identical original streaming/capture pixels; the capture adapter
and console frame cost still need their own acceptance.

## Bounded costs

Expanded preparation uses the existing persistent preparation ceiling.
The old three-region plan remains for the permanent diagnostic path.
The expanded resource namespace and command-cache bounds live in code;
the uploader sizes its ledger to the actual resource count. The GPU byte
budget, native live request limit and normal immediate/prepared draw
limits are unchanged. Large backing is not permission to draw all of it.

The permanent1003 diagnostic/replay loader remains unchanged. Expanded
1003 uses the bounded loader, releasing source ranges and uninstanced
meshes rather than retaining another city-sized copy.

## Evidence and limits

`nfsu_career_finish_backing_retail` and
`nfsu_freedrive_zone_backing_retail` test actual prepared race resources,
native zone requests over the campaign track corpus, the finish-adjacent
transition and valid commands after it. The existing persistent-payload
and conservative GPU upload/residency preflight also run with the real
human/rival/traffic roster. `nfsu_draw_execution_tests` pins the descriptor
boundary and rejects the next resource beyond it.

Current counts and byte bounds belong in those tests and their output,
not this document. The local negative-control experiment using the old
permanent-only backing fails on section724; the expanded bank passes the
same coordinates. Host CPU preparation measurements are not Switch frame
or loading measurements. Console confirmation of the sprint result flow,
GPU allocation and saved victory remains required.

The earlier r104 terminal-result protection and exact SD exception journal
remain enabled. SHADOWRAMP car/traffic shadows are a separate open task;
this change does not claim to restore them.
