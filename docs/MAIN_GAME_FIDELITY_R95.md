# Traffic, collision audio, magazine delivery and NOS verification

This local continuation uses the authenticated PC1.4 USA executable through
Ghidra and bounded native execution. It is not a whole-game equivalence claim.
Git/index/ref/board/publication holds remain; multiplayer remains parked.

## Traffic production ownership

The live traffic adapter now supplies actual road chains, actor hulls,
neighbor identities, lane transitions, direction policies and shared WORLD
state to strategy4 actions8/3/18. It uses the original odd full-update phase,
controller horizon/cap, action admission/completion and physical promotion.
Intersection arrivals share the authored table and its original phase owner.
The old permanent junction cache, short obstacle corridor and nearby-point
minimum-speed substitute are removed.

5A6CE0 checks the existing chain context-event flag. It does not mean that
an authored road ended; an early proposed extra road-end flag was rejected.
The original policy identity at controller virtual40 is an integer direction,
not route progress. Explicit ride/route initialization precedes placement.

Evidence: nfsu_ai_traffic_avoidance_tests,
nfsu_traffic_avoidance_native_retail and nfsu_traffic_reaction_retail. The
latter checks real authored roads/boxes, real obstacle queries, action choice,
path publication, physical promotion and retained-frame cadence. The native
avoidance tool declares query results as external boundaries; it does not
claim to execute a natural traffic drive. Source spawn selection, far recycle
and final retirement still include bounded port policies. Natural congestion
and all-map visual/behavior acceptance need runtime observation.

## Collision sounds

531FF0 impact admission is now separate from scrape admission. The attached
same-actor subtype5 direction comparison, previous actual-start geometry and
rejection latch are checked against original execution.465AAF normalizes the
current solver impulse into record70; production supplies that direction.
Refused impacts retain their independent scrape controller.

52FB60 orders pending/attached records before backend dispatch. Its original
configured impact capacity is represented by kCollisionImpactCapacity. The
bounded two-slot alternating world/car output is replaced by that admission
and channel budget. Variant counters and SND jitter advance on actual admitted
start, rather than while a request may still be rejected. The full native
queue witness includes the unusual over-capacity traversal; replacing it with
a conventional complete distance sort would disagree with the executable.
The original distance producer421400 was inspected; a clock interpretation
was rejected.

The common physics owner copies a shared audio order for world/car responses.
The audio snapshot retains it instead of grouping all car hits before all
wall hits. Prop contacts retain physical order instead of energy sorting.

Evidence: nfsu_collision_voice_pool_tests,
nfsu_collision_admission_native_retail,
nfsu_collision_voice_pool_native_retail and nfsu_race_audio_retail.
Native queue allocation/destruction and backend dispatch are explicit witness
boundaries. This human-contact adapter does not establish the complete global
remote/prop voice occupancy, natural cross-module callback order, all original
body-record lifetimes or final speaker PCM. The separate bounded prop/scrape
outputs remain; listening and natural impacts still require acceptance.

## Magazine delivery

The original4C9240 terminal Giveth-batch command acknowledges the entire batch.
The old port acknowledged only the highlighted cover when leaving, allowing
other old rewards to appear again at the next victory. Dismissal now records
the whole delivered batch while preserving its car snapshots and campaign
progress. Selected-cover viewing remains separate. The existing unique/movie/
magazine priority and authored event rewards remain in effect.

Evidence: nfsu_career_magazine_lifecycle_native_retail and nfsu_career_retail,
including original batch flags, multiple rewards and subsequent refresh.
This closes that repeat-opening cause; it does not claim that every original
MagazineScreen/Giveth widget, song override or special-photo normalization
has been reproduced. See CAREER_MEDIA_MAGAZINES.md for those boundaries.

## NOS rendering and automatic checking

Fresh Ghidra checks cover409580,40CD40,40D370,40D090,40D6E0 and the original
projection producer40DD00. The half-size reduction and two radial passes run
before cars/shadows/HUD.40D6E0 uses the same constant factor for every RGBA
channel; using sampled image alpha as that factor is a different contract.
The renderer now preserves source RGBA and uses the actual constant blend.
The owner enabled the original ambient speed blur and release tail on
2026-11-08; the active race call site now uses that original arithmetic.

The unmodified owner filter.efx was executed as ps_1_1 on local D3D9 HAL with
D3DX9_24. Synthetic ramp/checker images, including variable alpha, exercise all
passes. The decoded shader and private captures remain ignored local analysis.
Effect provenance SHA-256:
fd731c437b4107c00fde2923d69d42d444190335bab7a8f20f2ee4c9b7263988.
The original-effect pixel pins live in nfsu_speed_blur_tests, alongside the
independently checked CPU reference used by the diagnostic.

A real PC port run with automatic gas/NOS and rendered world/car/HUD was also
captured. This is software rendering evidence, not Switch GPU acceptance or
a frame-rate measurement.

NFSU_SWITCH_SPEED_BLUR_PROBE compiles the opt-in console diagnostic.
--nos-self-check starts live event1 with installed nitrous, supplies gas/NOS,
waits for the strong active effect and compares complete RGBA pass readbacks
with the reference after their owning GPU fence. It then exits automatically.
BLUR_PASS and BLUR_PROBE_END report errors/guards. Ordinary launch leaves this
probe disabled. The diagnostic deliberately perturbs timing and is excluded
from performance conclusions. A successful build is not a successful GPU run.

The ignored fidelity-r95 checkpoint and validation receipts own the precise
source archive, commands and remaining hardware result. Final NOS appearance
and upstream natural camera/effect scheduling remain unaccepted until that
console check and owner observation; synthetic shader agreement alone cannot
certify them.

## Live original NOS capture

The owner supplied a live UG0 drive in the PC original.
`tools/original_nos_recorder.py` authenticates that executable and creation
time, observes the native NOS trigger and captures the same native frame
before the world filter, after it, and at Present. Temporary D3D9 surfaces
are released, transient hooks detach automatically, and gameplay inputs,
assets and saves are never edited. The capture is not an FPS benchmark.

The actual original radial vertices, camera matrix/velocity and blend state
are recorded with the pixels. The local report `port-comparison.json` in
the owner's recording compares the current `speed_blur_reference.h` against
that original output using the captured input. `camera-comparison.json`
checks `camera_motion_to_view` and `SpeedBlur` on that same original state.
These bounded comparisons passed; they do not establish the port camera
trajectory, onset/release policy or Switch GPU execution. The owner identifies
the additional video as event0; no exact video-to-capture frame alignment
is claimed. The capture utility was first checked with a hidden ia32 D3D9
HAL fixture, including exact before/after/final pixels and HUD isolation.

The complete Default gate found three regressions after the initial target
checks: a missed traffic-test shape input, a prop admission overwriting a
sound before rendering its first sample, and the loader's existing memory
ceiling. The follow-up retains actual shapes, bounds per-frame prop admission
without reordering, and decodes temporary FE data before retaining impact PCM.
Required gates must be repeated on the corrected source; previous receipts
are not acceptance for the changed source.

## Moving console diagnostic

The first full console readback agreed with the reference, but its camera
velocity and radial offsets were zero. The external checker rejected it as
a complete NOS acceptance. The diagnostic held gas on the initial connected
frame, so the real input mapper correctly latched it until release. A local
reproduction through the actual Npad bridge and default DriveControls showed
the missing throttle and confirmed a neutral connecting frame fixes it.

The diagnostic now releases first, then presses gas. It requires at least
six racing seconds and actual speed of80 km/h before starting NOS, holds NOS
for two seconds, and ends on a bounded timeout if the required state is not
reached. Ordinary game input still uses its existing connection policy.
The moving run remains required; a stationary copy/composition comparison
is not a radial-motion acceptance.

### Moving hardware radial-pass finding

The lossless opt-in capture compares the actual half-image input and output,
not screenshots with different camera positions. The radial pass lost the
vertical corner addend: its pixels match `(bottom-top)*v` instead of
`top+(bottom-top)*v`. The horizontal interpolation and recorded constants
remain correct. A vector FMA replacement reproduced the same GPU output. The next shader
keeps both corner contributions scalar and precise through lowering, with
separate multiply/add instructions and no FMA contraction.
See the `root-radial-exact-r95.json` capture analysis in the local checkpoint;
its assertion is limited to this captured pass. Hardware acceptance of the
replacement remains separate from the diagnosis.

The automatic test resets its wall-clock baseline after the deliberately slow
reference/export step. This avoids treating its own diagnostic work as an OS
suspend; normal gameplay pause and suspend handling are unchanged.
