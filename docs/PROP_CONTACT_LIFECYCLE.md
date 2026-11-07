# Native prop conversion and impact dispatch

Scope: PC 1.4 USA leaf props, their conversion to live world bodies, visual
compensation and ordering of player impact starts. This is not an assertion
that every original physics or sound branch is implemented.

## Conversion and visual pose

The original596570 conversion queries440A40 with a fresh440740 ground cache.
Its424EB0 bounds are zero-initialized, COM-relative468A50 extrema. The
596649..596686 expression raises the physical placement only if its lower
bound penetrates the selected road clearance. Body174 retains that lift.

5974FA..5975FF renders the COM transform minus the rotated COM offset and
body174.597614..59765D then decays body174 only while the body is awake and
its activation threshold is zero. The physical correction and visual
compensation are different state. Applying only one causes either an initial
visual jump or an embedded collision shape.

RaceProps::convert_proxy now runs this conversion when the shared world
replaces a negative authored proxy ID with a positive body ID. The GPU scene
uses race_prop_visual_position. Returning an unused body to its static proxy
restores the authored pose and clears both lift states.

`nfsu_prop_settling_tests` pins arithmetic outputs from authenticated original
instruction ranges. `nfsu_race_session_tests` checks the complete conversion,
physical placement, visual compensation, post-render decay and proxy restore.
Reproduce the original arithmetic from the repository working directory:

```text
python tools/prop_conversion_native.py <retail-root>
```

The tool verifies speed.exe identity before execution. The ground query and
bounds inputs are supplied explicitly; it does not emulate allocation or a
complete game frame. Existing ground-cache and shared-world tests cover those
separate producers.

## Composite-body census

5966D0 reads child definitions and a damage threshold.597980 accumulates
damage;597D10 replaces a broken parent with child bodies only after the
response. Immediate creation inside an impulse callback is not equivalent.

The inspected PC retail TRACKB inputs contain no child or damage definitions,
including definitions not used by placements. Reproduce the read-only check:

```text
python tools/prop_breakup_census.py <retail-root>
```

The runtime still explicitly refuses a non-leaf child graph. A generic child
factory and dynamic child-model rendering have not been implemented or tested;
the census establishes that this branch is not exercised by these retail
track files. It does not establish support for modified track data.

## Impact sound dispatch

465830 updates retained contact magnitude before530240 invokes the associated
controller.531670 reads record60 and the smokeable inverse mass. Material
variant selection5314B0 and bank callbacks are separate stages.

The shared physical queue already numbers its observations. The snapshot now
keeps numbered car and road observations in that shared order even when the
telemetry groups have different solver-slice clocks. Prop observations copy
the magnitude retained after each individual response. The bounded snapshot
can retain both responses for every job in the current shared queue budget.

RacePropSound separates admission from effect-graph ticks. Accepted car/road
starts and prop graph ticks are merged by the shared observation order before
publication to the Mixer. They are no longer grouped as all prop starts before
all other starts. Repeated responses update the current prop voice from the
retained magnitude without starting a second voice for the same episode.
A zero-intensity pinned response still creates its controller, so a later
activation response can update it instead of being suppressed.

`nfsu_race_audio_tests` and `nfsu_race_audio_retail` check real-bank mixed-order
RNG/PCM equivalence, repeated magnitude updates and duplicate-tick suppression.
These tests compare the port dispatcher against explicit ordered bank calls;
they do not replay the entire original sound manager.

Limits remain explicit: the backend has bounded impact/prop/scrape voices,
port contact-episode policy, and continuous scrape graph scheduling. Exact
natural original controller-list admission and whole-manager callback order
are not established by the impact-start fix. Console listening/rendering and
independent review are separate acceptance criteria.
