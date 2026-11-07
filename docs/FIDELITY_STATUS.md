# Original fidelity status

Owner scope: interface, cars, world objects and sounds, resumed2026-10-28.
This is a current source audit, not a release or hardware acceptance. The
accumulated work is local-only under the owner's publication/index HOLD.
Use OWNER_REQUEST_AUDIT.md for the historical owner reports. Later evidence
below supersedes old omissions, not the remaining acceptance criteria.

## Current Trade In and world integration pass

See TRADE_WORLD_FIDELITY_R98.md for original function boundaries, production
corrections, tests and explicit limits. Trade In now retains all available
cars and binds each selected logo after static FNG lookup. Traffic prediction
uses the complete role3 caller and published owner motion. Car integration
separates traffic preparation and uses a spatially ordered physical phase;
original pool-ID ties, prop interleaving and natural whole-race parity remain
unverified. Hardware acceptance and independent review are pending.

## Corrections in this pass

| Boundary | Original source and current correction | Verification entry point |
| --- | --- | --- |
| Drag lane control |The drag tail now publishes the human path. Input4605E0 and driver45E970 consume actor3B8 travelled distance, produced by426930, instead of current speed or a signed local road coordinate |nfsu_drag_steering_event4_retail; nfsu_drag_steering_event9_round0_retail through round2; drag_steering_native.py |
| Drag hard impact |465D00 latches actor874/heat;435570 consumes Totaled reason7 before engine reason6, with failure HUD/result labels. The numeric drag callback uses its original additional impulse multiplier |drag_contact_native.py; nfsu_car_contact_tests; nfsu_drag_steering_event9_round0_retail. Supplied contacts/terminal seams, not full-world parity |
| Human high-RPM shift |5292B0 ->528D70 on notification,529530 ->528F80 after the first audible RPM curve; separate selected FX_SHIFTING_01 instances share PCM |shift_disengage_native.py; nfsu_race_audio_retail |
| Human shift buses |529100 selects category1 for the main delayed sound;528D70/528F80 select category5 for the separate clutch events, through526AA0/536C20 |shift_category_native.py; race_audio_mix_native.py; nfsu_race_audio_tests |
| Human coast backfire |527480,526A40,564A50/80 and674898 produce the timers/gain/variants;521C80 row1 selects CAR_1 SparkChatter for the engine upgrade |backfire_audio_native.py; nfsu_race_audio_retail; race_effect_audio_native_check.py |
| Retiring one-shot effects |Corrected started-Player witness executes64D860, changes backend type and observes destruction through DirectSound Stop/Release; the earlier static-byte ownership claim is retracted |effect_retirement_native.py; AUDIO_BANK_RUNTIME.md |
| Drift achievement comments |43EF30 ->4A7920/4A7AA0 owns a separate localized comment lifetime; Comment_DATA supplies position, scale and color, independently from the style award queue |drift_comment_native.py; nfsu_drift_state_check; nfsu_drift_track_check |
| Style loss presentation |4A87B0 uses packed FF800000 and signed/base-multiplier formatting; drift hides the general trick title but retains its signed award |race_hud.cpp; connected drift HUD checker |
| Collision intensity |530F80/531030 and674898 truncate the curve result; nearest-even remains in the separate bank graph arithmetic |collision_intensity_native.py; nfsu_race_audio_tests |
| Collision observation |465830 accumulates every same-slice solver response; the output coalesces voice controls without discarding magnitudes |nfsu_race_audio_tests |
| Full traffic roster |Traffic stop histories and collision audio buffers now cover all admitted traffic/rival identities, including the final pool slot |nfsu_race_world_contact_retail; nfsu_race_audio_tests |
| Spark origins |The world contact owner publishes its real solver witness; particle output no longer invents a body-side point |nfsu_race_particles_tests; nfsu_race_world_contact_retail |

Original code executes in the native tools; only explicitly declared output
IO is supplied. Selected bank graphs are compared separately. Physics forces,
engine tuning, transmission and rival difficulty are not retuned by these
sound/UI corrections. No fixed recording outcome is used as a balance target.

## Rechecked existing production boundaries

- Cars and shadows: race_gpu_scene uses the selected car/upgrade geometry,
  its own shadow texture/solid footprint and the terrain-grid projector.
  Stock and tuned rivals use the same projected shader packet. See
  CAR_SHADOW_GPU.md and nfsu_car_shadow_projection_tests.
- Minimap:5912B0 style1 geometry/palette,58EFB0 filter and58E810 marker
  ordering reach race_minimap_hud. See CAREER_MEDIA_MAGAZINES.md and
  nfsu_race_minimap_hud_tests. Fixed port roster gates and unsupported optional
  styles are not the complete original minimap option/lifecycle system.
- Garage: category highlight and part entry have distinct camera ownership;
  palettes, remappable vinyl channels, installed restoration and live preview
  resources have compiled production checks. See GARAGE_MENU_ORIGINAL.md,
  GARAGE_LIGHTING.md and nfsu_career_retail. This is not an all-screen visual
  acceptance from the owner.
- World animation: authored reverse arrow placement, texture-frame aliases
  and fountain UV velocity reach live static-world GPU descriptors. See
  nfsu_career_races_retail and its named animation checks.
- Props: parsed descriptors, hulls, selected mass and retained contact records
  reach the common world contact owner; prop sound uses the smokeable bank.
  See WORLD_OBJECT_BODIES.md, nfsu_race_world_contact_retail and
  nfsu_race_audio_retail. This does not establish every natural interaction.
- Career media/results: the current event transitions, recorded rewards,
  spoken map messages, playlist state and end-of-race owners are exercised by
  nfsu_career_retail. A complete natural campaign playthrough is still absent.

## Still open: source restoration

These are actual boundaries, not claims that the old reported symptom is
necessarily still visible in the latest source.

| Domain | Unclosed boundary | Next source work |
| --- | --- | --- |
| Traffic cars |Real strategy4 action/context/query/intersection wiring now replaces the permanent junction cache, short corridor and nearby speed minimum. Native full/retained cadence and real-road reactions are checked |See MAIN_GAME_FIDELITY_R95.md; natural spawning, retirement, congestion and all-map acceptance remain bounded/open |
| Racer/traffic physics |Natural whole-race contacts/recovery, physical selector1/3/4 scheduling and some road/terrain producer boundaries are not certified by isolated solver comparisons |Compare complete admitted original states and caller order; keep unsupported capture states excluded |
| Ordinary rival route adapter |The r94 source check confirms ordinary45E970 also consumes actor3B8. Full and retained plan publication now use the actor odometer; local road coordinates remain geometry inputs. Owner-reported event8 route/acceleration parity remains open |Actual original ordinary boundary fixture and production moving-actor publication regression; check complete routing, action lifetime and natural race trajectories separately |
| Collision audio |Original531FF0 admission,52FB60 ordering and actual-start SND side effects are connected to human world/car contacts; shared physical order reaches audio and prop energy sorting is removed |See MAIN_GAME_FIDELITY_R95.md; natural cross-module ordering, shared remote/prop occupancy and device listening remain open |
| Rival audio |Remote engine/exhaust, prerace scripts, separate shift/end banks,529EA0 adaptive gain and ordinary524130 Turbo controls/spatial output are connected;196C producer ownership, remaining NOS/tyre actors and natural whole-module output order remain incomplete |See remote_shift_native.py, remote_gain_native.py and nfsu_race_audio_retail for bounded control/output evidence; continue each remaining actor's own notification/category/spatial controls |
| Human audio |Base controls, common529EA0 profile,529CD0 mode mixing and POV-dependent shift-bank variant are connected; transient SND commands and nondefault11C8 fade are not |See human_gain_native.py and nfsu_race_audio_retail for bounded output evidence; continue actual transient/fade producers and natural device order |
| Sound-bank scheduling |Original651150 initialization/6511B0 shared carry state is now connected and checked; full natural module construction/caller order is not |Keep the native generator/interleaving witnesses distinct from whole-race sound sequence acceptance |
| PCM spatial output |Recovered mono software stereo pan now reaches Mixer; DirectSound3D/HRTF, other speaker layouts, full Player status/restart/timer semantics and remaining NOS/skid/mastering adapters are not certified |See AUDIO_BANK_RUNTIME.md; do not infer complete device parity from software coefficient or wiring equality |
| Tyre audio |The bank is original; the physical-slip to control adapter and remaining output mastering are not |Recover the actual1498..14A8 producers and527CA0 controls before changing thresholds |
| Particles |Smoke/spark triggers, appearance and generated sprites are still approximations; a full original emitter runtime is not connected |Recover the named car/world effect producer and preset consumers, not only their resource strings |
| Drift HUD |Independent achievement comments and the separate item0 publication/display lifetime, loss retention, comment hiding and bonus-zone tint are restored locally; general continuous-notice retirement and every authored group/leaderboard transition are not |See drift_popup_native.py and registered drift HUD/state checks; continue the remaining queue and FNG group owners |
| Magazine UI |Original terminal batch acknowledgement fixes the identified repeat-opening cause after dismissing multiple covers |See MAIN_GAME_FIDELITY_R95.md and CAREER_MEDIA_MAGAZINES.md; full Giveth/browser presentation and remaining special tasks remain open |
| Camera/blur |Original ps_1_1 effect executed on D3D9, RGBA composition corrected, full-pass opt-in Switch readback and automatic gas/NOS launch added |See MAIN_GAME_FIDELITY_R95.md and the r95-r6 Switch readback receipt; all shader passes were compared on the moving console test. Natural camera/effect scheduling and owner appearance acceptance remain separate |

## Parked multiplayer prototype

On 2026-11-02 the owner confirmed prototype startup and parked LAN work.
The main menu shows MULTIPLAYER / COMING SOON on its disabled Online row.
See [LAN multiplayer backlog](LAN_MULTIPLAYER_BACKLOG.md) for the confirmed
checkpoint and remaining two-console acceptance. Ordinary game work resumes.

## Acceptance that code checks cannot replace

Portable appearance/listening, the previous natural flying-car/missing-impact
reports, final NOS blur appearance, full campaign flow and the remaining
performance spike are not accepted. Current HOLD excludes a new original game
launch/recording, computer use and independent review. The moving Switch NOS readback is complete in the r95-r6 receipt. The owner
now prefers the NRO as a file; no Netloader request is pending. Keep
those criteria open and preserve a precise continuation checkpoint. A green
build or a bounded native comparison does not close them.

The current pass's exact commands and source identity belong to
checkpoints/root-fidelity-r64-20261028.md and the continued
checkpoints/root-bank-audio-r67-20261028.md, checkpoints/root-traffic-owner-r68-20261029.md and checkpoints/root-drift-popup-r69-20261029.md and checkpoints/root-remote-shift-r70-20261029.md and checkpoints/root-remote-gain-r71-20261029.md and checkpoints/root-human-gain-r72-20261029.md and checkpoints/root-traffic-runtime-r73-20261029.md and checkpoints/root-traffic-context-r74-20261029.md and checkpoints/root-traffic-actions-r75-20261029.md and checkpoints/root-traffic-geometry-r76-20261029.md and checkpoints/root-traffic-stop-r77-20261029.md and checkpoints/root-lane-permission-r78-20261029.md and checkpoints/root-motion-runtime-r79-20261029.md and checkpoints/root-world-pair-r80-20261030.md and checkpoints/root-collision-output-r81-20261030.md and checkpoints/root-remote-turbo-r82-20261030.md and checkpoints/root-factory-turbo-r83-20261030.md, with their local validation receipts.

## Additional integration boundaries checked in this pass

The process-global backfire variant counter78EAE0 is not reset by original
actor or race replacement. Its only executable references are527480 reads
and writes. The human output now preserves that lifetime. The native controller
fixture checks its read/write sequence. A proposed PCM cycling assertion was rejected:
the actual selected bank does not use that parameter as its random sample
selector; the original wiring witness produces identical controls for all
five counter inputs with a fixed synthetic backend RNG. Bank callback RNG
now follows shared original state; full natural actor scheduling stays open.

The complete retail Sound directory remains an external SD dependency. The
new coast actor specifically needs Sound/CAR_1/ENG_UG_2/Sparkchatter_01.abk
and Sound/CAR_1/ENG_UG_3/Sparkchatter_01.abk. No retail bank is inside the NRO.
A missing bank now names its exact path in the audio diagnostic. No current
SD inventory has been accepted under the transfer/hardware HOLD.

## Campaign-wide integration follow-up

See [Campaign event integration audit](CAREER_EVENT_AUDIT_R97.md) for the
all-event matrix, regional drift-start and prop fixes, rival grid settlement
correction, traffic course sweep and the exact limits of those checks.
