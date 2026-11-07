# Unbacked WorldObject collisions

This continuation connects the fixed generic WorldObject constructor branch
to the shared runtime contact queue. It does not claim complete original
world-object simulation or full physics parity. Animated train/Trolley
controllers are covered separately in [World motion](WORLD_MOTION_CONTROLLERS.md).

## Input and pose

The original constructor at 0x578880 instantiates group-zero 0x3B300 records.
The filter at 0x578820 suppresses DrawBridge and the named TRSABC_Track_Jump
in reverse. The collision request is record+0x08, the full pose starts at
record+0x10, and the class hash is record+0x00. Model, shape, object-name and
runtime body identities remain separate.

VehicleTrackCourse retains source offsets, class/name/shape/model hashes and
raw matrices. Generic WorldObject records without independent backing use
their named hull. The known absent 0x84E701E8 request uses the exact default
primitive from 0x592280 and the vertex ordering from 0x473D60. An unknown
missing shape is refused. No arbitrary box replaces a failed known decode.

RaceSession retains the authored matrix, including its scale. Replacing it
with a normalized vehicle quaternion changes object geometry. Fixed poses
have zero velocity and no rigid-body response. The inert holder descriptor
does not supply physical mass or inertia: 0x5927E0/0x592820 return zero, and
0x592940 ignores its impulse.

Source record order reserves the WorldObject identity range before actor
registration ranks. The actual global allocator's absolute retail IDs are
not established by this relative ordering. TrainEngine, TrainCar and Trolley
records retain metadata/reservation. Their animated controllers are now
connected separately; they do not become guessed fixed blockers.

## Shared response

Objects, cars, props and road jobs share the existing chronological queue,
remaining-time search, contact records, requery and response budget. Fixed
objects do not get a duplicate road solver or an integration substep. Their
last callback cannot overwrite the last actual component's force interval.
Both pair endpoint orders are supported. The separation code at 0x46B260
leaves an unbacked object's pose unchanged.

Ghost suppression at 0x467F17/0x468175 requires two dynamic bodies.
Recovery/ghost cars therefore retain collisions with fixed WorldObjects.
Object support and collision stay separate: a qualifying top face still
supplies the wheel height queried through 0x577060, while the complete hull
now also participates in body contacts.

Two numeric differences mattered:

- The upright-car rule at 0x46C09B applies against zero inverse mass,
  including unbacked kind 4. It is not specific to pinned props. Its friction
  and fast-impact arm adjustment must reach the prepared response as well.
- The check at 0x4663A0 requires two kind-0 bodies. A non-opposing car/kind-4
  impact enters collision state 1 without the car/car lateral correction or
  added vertical impulse. The opposing branch retains its RNG-dependent
  response. The callback at 0x465D00 uses the raw witness arm, before the final
  fast-response arm shortening.

Typed kind-4 observations preserve the physical witness, tangent point speed,
pre-contact axes and shared audio order. They use the existing world-impact
and scrape families and do not become traffic style penalties. Existing
crash-state/camera and drag-failure consumers receive the actual car response
and other class identity.

## Evidence and limits

The test named nfsu_world_object_contact_tests pins original outputs from
0x46BAE0/0x465D00 on supplied state: both endpoint orders, soft/hard responses,
current collision state and the upright threshold. It checks momentum,
angular momentum, record strength/gate and world RNG. Reproduce the original
outputs with:

    python tools/world_object_contact_native.py <retail-root>

The tool authenticates PC 1.4 USA speed.exe, uses declared synthetic geometry
and state, and omits only the audiovisual tail after 0x465D00. It is not a
whole original game frame or a recording of natural admission.

The same test exercises the production queue with a car, a recovering car
and active props approaching from both directions. It checks the fixed pose,
typed sound publication including nonzero slide speed, and rejects
object/road response jobs. The nfsu_world_object_contact_retail test checks
named city/drag records, the default primitive, raw rotation/scale retention
and reverse jump exclusion.

Train/Trolley motion has its own fixtures in WORLD_MOTION_CONTROLLERS.md.
Arbitrary parented/animated source poses, the BB factory and full original
allocator/list/callback parity remain separate branches. They are not
certified by fixed-body fixtures. Console
rendering, runtime behavior and independent review require their own evidence.
