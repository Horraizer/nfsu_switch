# Trade In and world integration audit

Owner scope, resumed2026-11-03: fix the Trade In browser and continue the
original trajectory/traffic/rival contact audit without a new owner drive.
This work is local-only under the existing Git/index/publication HOLD.

## Trade In

The highlight used to invalidate the preview request and load only the current
candidate. The preview identity is now the installed ride plus the unlocked
roster. One resident scene contains the owned car and every available target;
left/right changes its visible car through the existing original travel mode.
Native4C3390 clamps the browser at either end; it does not wrap around.

Native4C3240 binds CARSELECT_MANUFACTURER_<brand> to the thumbnail browser.
Native4C2B00/4C3120 bind SECONDARY_LOGO_<car> to the large selected-car image.
The port previously used a generic thumbnail and allowed the authored FNG
sample (Supra) to overwrite the dynamic selected logo. Dynamic binding now
runs after static FNG resource lookup. Native small logos replace the larger
PC_ variants; the existing atlas dimensions/byte ceiling are unchanged.

The test nfsu_career_trade_retail drives the real menu, retained scene and UI
for stock starter and tuned all-car profiles. It checks every candidate's
visible car after complete travel, manufacturer binding, independent logo
raster region, changed data, hover ownership and confirmed transaction.
It writes fresh software-rendered frames. It does not operate owner saves.

## Traffic prediction and trajectories

Native57A530 publishes the retained Driver path before controllers. The
role3 branch of4576B0 calls TrafficController44ACB0 on the selected raw road
point, not the first retained road-chain cell. Inside the road it retains the
Driver path; except action7 it applies45A510/45A5B0. Near or outside the edge
it runs45A250/45A2F0 with zero acceleration and the retained owner magnitude.
Traffic virtual50 returns4. The previous adapter used a single straight
endpoint regardless of the selected road or action.

The complete caller now feeds the production pair query. Racer inputs use
published Actor acceleration, axes, heading and signed speed, instead of a
new quaternion projection and an independent fixed-rate velocity difference.
The road-edge arithmetic follows the original x87 control word007F:24-bit
operations, including intermediate rounding. The decompiler's float10 type
must not be mistaken for the running game's precision mode.

The native tool traffic_world_path_native.py executes original57A530 and
complete4576B0 on declared stock bodies, raw roads and Driver inputs. It first
checks world_pair_native.py. The generated traffic_world_path_native.inc
covers road widths/boundaries, actions, stationary/forward/reverse traffic,
straight/curved paths and pair outcomes. nfsu_ai_path_tests compares actual
forecast geometry, cursor/timers, pair target and contact time with it.
The original tool has declared bootstrap services and does not execute a
natural race or certify physical impulses.

## Physical integration and contacts

Native420B40 publishes neighbors and prepares controllers/Drivers before
420310 integrates the registered bodies.468680 maintains that list by upper
X, with signed body ID as its equal-bound tie.420380 then resolves the shared
contact queue;426930 publishes the resulting world acceleration afterwards.

The port previously advanced rivals, then the human, then interleaved each
traffic controller/Driver/body. This mixed pre-world and post-world actor
states. Traffic preparation is now a separate controller/Driver phase.
Car integration uses a fixed-capacity upper-X-sorted snapshot, followed by the
existing shared contact queue and acceleration publication. Bounds are cached
from the selected setup/hull at construction, not recalculated for sorting on
every frame. The dispatch sort does not allocate a temporary heap buffer.

The contact refresh interval now belongs to the last integrated rigid car.
It is no longer overwritten unconditionally by the human or impacted traffic.
No impulse, engine, gearbox or catch-up constants were retuned. The existing
nfsu_race_world_contact_retail checks original selected hull/setup producers,
mixed car/track queue response, traffic activation and continuation. Added
separated airborne cases isolate initial dispatch from floor-contact rewinds;
traffic reaction checks reject positive-time body integration in preparation.

## Evidence limits

Code and controlled native execution can certify a supplied state's branch,
formula and calling contract. They do not certify all paths through an entire
race merely because a component test passed. This pass does not claim that
all initial world/RNG/action states equal the original.

The current car dispatcher uses canonical actor order for exactly equal upper
bounds. Original pool body IDs and their reuse, and props interleaved in the
same physical phase, remain separate integration work. Do not call this full
420B40 world scheduling parity. Traffic admission is still the port's bounded
pool policy, not certified native admission timing.

New Switch hardware acceptance is pending. PC software frames and NRO packing
are separate from console visual and natural-driving acceptance. The worker
has not performed an independent review or published a commit/PR under HOLD.
