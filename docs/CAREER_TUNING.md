# Career tuning catalog

CareerCatalog reads the owner's readonly PC1.4 USA assets. The catalog,
atomic purchases, installed selections, earned unlocks and schema8 saves are
connected to the live garage and the owned human car at race entry. The live
career now extends through final event113, including late tournaments and
one-choice unique awards. Performance packages stay locked until their authored
event rewards. See CAREER_CAMPAIGN_FULL.md for scope and remaining acceptance.

## Original code and data

- FrontB's 80034A00 container holds 34A01 price/reputation rows and 34A02
  events. Original loader5A00A0 creates the category and event pointer tables.
- 5A03D0 maps model/paint slots to economic categories.5A0350 separately maps
  physical upgrade fields. They are not the same numbering scheme.
- 5A08B0/5A08F0 select price words for levels1..3. Stock and unique level4 do
  not read a price word. A zero price does not establish availability.
- 5A1550 scans event numbers in ascending order for the first kind0 reward
  matching category and level. No matching reward means no event gate;
  a matching reward requires its event status to be won. Merely making an
  event available does not unlock its reward.
- GLOBALB34A08 has aligned style reward records.4AA970 sets earned flags
  at threshold equality and never clears them when a later point bank is
  lower.4AA9D0 queries them. This requires retained earned state or an earned
  high-water mark in the persistent inventory.
- The retail style rows contain vinyl families, manufacturer/unique vinyls and magazine
  rewards. The catalog does not invent a kind1 string-match implementation
  absent from these retail rows.
-5A05B0 consumes installed visual parts and category threshold masks to
  compute reputation. The original routine is exercised by
  tools/career_tuning_reputation_native.py and its C++ fixture. Default
  rendering paint does not count as a purchased visual upgrade.

The first performance packages for engine/exhaust, drivetrain and tyres
require winning event6. The early first-five career slice must not sell those
packages before the tournament. Early cosmetic categories have independent
level gates. Later cosmetic unlocks and style thresholds remain data driven.

## Verification

The values checked by C++ live in tests/career_tuning_native.inc. They are
regenerated from original code by tools/career_tuning_native.py, which runs
the actual loader and bounded original price/category/event/style routines
with supplied career states. There are no external call hooks. The sticky
style-flag case lowers the bank after earning a threshold and checks that
its earned flag remains set. This is not a live original-garage session.

Run from the actual workspace, with a real Python and Unicorn installed:

```text
python tools/career_tuning_native.py --root <readonly-PC1.4-USA-root> --out build/tuning-native-fresh
cmake --build build --target nfsu_career_tests
build/nfsu_career_tests <readonly-PC1.4-USA-root>
```

The oracle refuses existing output directories and checks the retail input
identities before execution. Default operation verifies the fixture text;
--write-fixture is an explicit fixture-generation mode. The production
catalog also rejects malformed/truncated or duplicate price categories.
No retail files are included in source or written by this workflow.

## Purchased and installed state

Original5A34D0/5A3570 use a category grid with bounded key lists. Wheel
ownership uses the authored family attribute10C98090, so rim size does not
create a separate purchase. Body-category ownership packs category, style
variant and level; paints, tint, vinyls and neon use original part hashes.
The actual-record oracle is tools/career_tuning_ownership_native.py, checked
by tests/career_tuning_owned_native.inc through nfsu_career_tests.

CareerProfile retains installed selections separately from purchased keys.
Returning to stock leaves purchases available; reinstalling an owned item
charges no money. Refused locked, incompatible or unaffordable transactions
leave the profile unchanged. Normal vinyl-family availability uses the
original prefix/type/level key. Special whole-car vinyl policy is deferred;
it is not approximated as another normal vinyl family.

Save writes use schema8; schemas1..7 still load. Schema8 keeps eight
tournament result slots; earlier three-slot standings migrate with empty
additional slots. Old saves start with stock
installed state and use their current style bank as their initial earned
high-water mark. Earlier NROs may refuse newer schemas. No console save has been
modified by desktop tests; those tests use unique temporary slots. Runtime
frontend integration updates both the point bank and earned high-water mark
when committing a result. Post-race style uses the native integer multiplier
from visual reputation; newly crossed thresholds create original reward
messages without awarding the same unlock twice.

## Garage and race integration

CareerGarage owns only navigation and confirmation state. Preview profiles
are temporary copies. A confirmed purchase updates cash and ownership once;
backing out preserves the installed ride. Body, accessories, rim brand/style/
size, body/part paint, vinyl layers/colors and decal layouts/positions use
original part records. The PC color picker uses the authored color grid and
separate gloss/metallic/pearlescent and part-specific palettes. Stock special
parts cannot be painted. Unrelated unique level4 award items remain outside the normal shop. Unique
vinyl type15 uses the original kind2 earned-style flag, including its level4
CUSTOM records; type16 contains manufacturer vinyls. The categories are not
interchanged. Unearned Unique entries remain locked; earned entries can be
installed at their original zero purchase-table price.

FrontendGpuScene holds one environment and a bounded bank of component
variants. It shares equal immutable GPU payload and releases source-file and
parser copies after preparation. Switching a resident variant changes draws;
changing the bank replaces scene uploads while retaining the GPU device,
swapchain, display targets and HUD. The previous CPU scene remains alive
until its borrowed GPU commands have retired. Music and camera state survive
the replacement. There is no explicit loading screen. Bank replacement
latency and the actual Switch memory ceiling still require hardware acceptance.

Automatic category focus uses the original first matching slot camera row,
angular shortest path, transition duration and easing. The bounded original
constructor/setter/update/view witness is tools/frontend_garage_camera_native.py;
nfsu_frontend_tests compares its saved states in
tests/frontend_garage_camera_samples.inc. Right-stick orbit is a port
controller mapping using the recovered FE camera projection. Its input rates and pitch limits are port choices, not claimed
native input constants. D-pad selects colors; L/R changes body paint finish.

The same CareerRideSetup supplies preview and race visual/physical selection.
Neon uses the genuine car texture, native part colors and56F061 color cadence.
It reuses the existing shadow ground placement rather than adding terrain
queries. tools/career_neon_native.py checks the bounded original color routine.
Special rim, exhaust, caliper and spoiler material bindings follow their
original CarRender consumers. Original5A05B0 reputation and49F920 integer
conversion feed the displayed and persisted post-race style award.

## Scope still outside acceptance

This is not a completed whole-game garage. Trade In, installed and hypothetical
performance ratings, and car-specific wide-body decal layouts are implemented;
`nfsu_career_retail`, `nfsu_career_trade_retail` and
`nfsu_career_garage_live_retail` cover these paths with retail inputs. The full original
magazine browser, complete special whole-car vinyl award presentation and
detailed performance brand/tuner screens still need separate acceptance. The special
award pages use the original unique performance and visual packages, separate
from the regular shop; native eligibility and grant routines are tested.
Their final presentation still requires hardware acceptance. Purchase
confirmation currently uses platform text rather
than the native generic dialog package.

Use nfsu_career_tests with a fresh output directory and mode garage for the
actual navigation, purchase, preview, material/skin and resource smoke. This
is desktop evidence. The owner must still check the combined NRO on Switch,
including the previously reported post-finish transition and save migration.


## Career presentation correction pass

Regular part thumbnails follow the original category icon table and rims use
an actual representative part while choosing style. Stock vinyl/decal entries
use their native category icons. Sponsor brand aliases use the original
68F1F0/512294 lookup, verified by tools/career_decal_icons_native.py; the
corresponding original icon sprites are loaded into the frontend atlas. Authored leaf headings and vinyl layer
sprites replace editor placeholders. The garage-pages mode of
nfsu_career_tests walks and renders the restored leaf pages with retail art;
its garage mode checks the loaded car and camera focus.

Post-race presentation retains the finish scene, retires live NOS/light-history
buffers and the race HUD, then uploads the results atlas. Career atlases trim
unused bottom rows without rescaling the original art. This addresses the
GPU allocation failure in the preserved r18 owner log; successful Switch
results/style/reward transitions still need a new hardware run.

Message playback ducks only music using the original speech mixing profile;
attack and release are recovered by tools/frontend_speech_mix_native.py.
The default original menu playlist starts with Get Low. Its subsequent menu
and race bank selection is verified by tools/frontend66_music_native.py.
Race audio now retains the next song between separate race service instances,
using consumed music frames rather than output-device time. This is process
state; saved custom playlists are not claimed implemented.

Track loading also imports common texture-animation metadata from auxiliary
texture sources. This binds the actual red neon barrier families in INGAMEB
as well as track-local water families. Only needed animation frames are
resident; existing descriptor rotation changes frames without another world
draw pass. nfsu_career_races_tests checks the named neon/water families in the
actual first circuit scene, rather than accepting an unrelated animation
counter. Independent clocks during pause are outside this correction pass.

The actual special-vinyl selector branch4E3F90/4E4033 and threshold flags are
executed by tools/career_special_vinyl_native.py. The fixture covers locked,
threshold-equality and earned cases with supplied empty ownership. Other
whole-car body vinyl policies and later unique reward selectors are not
inferred from this correction.

## Current correction witnesses

After the first purchased paint job, unlocked palette changes cost zero. This
is the owner's confirmed original transaction behavior. The generic5A34D0
per-hash ownership witness alone does not establish that parent picker policy.

Career wheel styles/sizes are filtered by CarTypeInfo+DC and the actual
EB0101E2/CE7D8DB5 attributes, as50D5C1/50D779 do. The ownership key remains
the family key, independent of size. This removes other cars' tire dimensions
and repeated display names from the live rim picker. Existing saved purchases
are retained. tools/career_wheel_fit_native.py reproduces the filter fixture.

Career Time Trial type4 builds runtime mode2 at4B5FDF. Its78A310 countdown
is read by4A1932, bypassing lap hold.5A3098 requires a recorded time strictly
less than the selected deadline for a career reward.435875's reason8 belongs
to the separate78A346 challenge context; it must not be substituted here.
The witness is tools/career_time_trial_native.py.

Original445AC2 applies4462A0 once to fresh reverse scenery. Flag400 mirrors
the local X matrix row before deriving world placements. The shared course
loader now applies that fixup; tools/career_reverse_scenery_native.py executes
the original unpack/multiply/pack path using its scalar CPU implementation.
Texture animation remains the original per-frame alias rotation. The current
pixel probe verified animated water in the bowl and the actual neon barrier
materials. It does not certify fountain jet/particle emitters.

Engine bank RPM uses523DD7's physical RPM / tuned maximum RPM *10000.
The RideInfo binding is proved by57F2EB and526638, not by interpreting
CarTypeInfo+258 as maximum RPM. tools/career_engine_rpm_native.py reproduces
the fixture. Native shift/load envelopes and complete original volume
mastering remain separate fidelity criteria.

Run any witness from the actual repository root with --root pointing at the
read-only PC1.4 USA installation and --out naming a fresh build-prefixed
subdirectory. Default mode verifies its checked-in fixture. --write-fixture
is an explicit regeneration mode. No witness writes retail files.

World texture movement is separate from frame animation. TextureInfo+52
selects continuous, quantized or static-offset UV transforms; the signed
fields at+58..60 retain their native units. The original40F599 binding and
40EC20/40EC90 functions are reproduced by tools/career_texture_uv_native.py.
The texture tests compare both the CPU helper and the actual scalar from
both production GLSL variants against that native fixture. TRN_WATERFALL_A
on the authored fountain jet meshes now carries this metadata through decode,
static GPU materials, main views, reflections and environment captures. Only
the shared simulation clock changes per frame; no extra world draw is added.
The prior bowl-frame witness alone did not cover these separate jet meshes.
Full original particle-emitter behavior is not inferred from this UV fix.
