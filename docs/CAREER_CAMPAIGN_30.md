# Campaign through event30

Owner scope: connect16..30 to the existing Underground campaign, with original
prerequisites, rewards, visual reputation, unlocks, media and one combined NRO.
Retail input: owner's read-only PC1.4 USA. No game data is shipped in source.
This is a local owner integration, not an independently reviewed release.

## Authored progression

The event records come from FrontB34A02; track identity and stream region come
from GLOBALB34201. Unlock category/level checks and reputation use the existing
bounded original tuning witnesses. Reward cash uses the selected difficulty's
authored word, not a new balancing table. A win opens an item for purchase;
it does not install the item or charge its price automatically.

| Event | Type | Original continuation and rewards |
| --- | --- | --- |
|16|Circuit|After15; reverse1006 and07_GOD extro on victory|
|17|Drag|After15; Tiburon car unlock, using regional USA reward text|
|18|Drift|After16 or17; neon level2|
|19|Circuit ranking event|After16 or17; forward1001 and earned magazine cover|
|20|Drag tournament|After16,17,18,19; all four authored races; forward1201/1206|
|21|Drag ranking event|After20; cash and campaign status|
|22|Sprint ranking event|After20; Impreza and reverse1103|
|23|Sprint|After21 and22; paint level2 and forward1104|
|24|Lap knockout|After21 and22; Sentra|
|25|Drift ranking event|After23 and24; earned cover and forward1303|
|26|Timed TJ challenge|After25; choose one unique ECU, turbo or brakes; forward1105|
|27|Sprint ranking event|After26, including optional failure; earned cover and forward1106|
|28|Drift|After26; muffler tips level2 and forward1304|
|29|Drag|After26; window tint level2|
|30|Circuit tournament|After27,28,29; engine/exhaust, drivetrain and tyres level2; forward1008|

Exact cash, lap/direction and record values are asserted from retail inputs
by nfsu_career_retail and nfsu_career_full_retail. They are not copied into
a second current-value table here. The original event block is now followed
by [the remaining campaign through the finale](CAREER_CAMPAIGN_FULL.md).

## Tournaments and persistence

Tournament20 is four races, not three. Its second course uses L1RC while its
other rounds use L1RB. The L1RC round requests twenty traffic configurations;
the bounded traffic and world snapshot pools now accommodate that authored
count. The original sixteen-entry local neighborhood remains unchanged. Contact and
near-pass caches allocate for the configured traffic count before start;
rival sessions do not each reserve the maximum traffic pool. Traffic audio
also prepares the configured count; extra voices are disjoint from the
existing rival-engine and turbo ranges. No graph loading occurs in updates. Tournament30 uses three L1RA circuit races. Every round
keeps the parent event's roster, difficulty, reward and accumulated standings.
Intermediate rounds neither pay the prize nor mark the event won.

Original5A29C0 and5A2AB0 are executed without external call hooks by
career_tournament_native.py for supplied1..8-round standings. The portable
score accumulation and stable ordering are checked against that fixture.

Schema8 retains eight result slots, matching the event table's capacity.
Schemas1..7 remain readable; old three-round standings are read as three
rows before the later fields, rather than consuming movie/magazine state.
Tests save and resume each new tournament round and pending award. Older
NROs may refuse the new schema. Tests use isolated temporary saves, never
the owner's console save.

## Unique awards, reputation and presentation

Challenge26 uses the original unique performance package and supplied
category order. It skips already installed unique categories, requires
confirmation and applies one separate unique flag. It cannot grant all
three, turn a unique into a normal level4 purchase, or charge cash.
Original eligibility/grant/voice comparisons are in career_unique_reward_native.

Visual reputation, its integer style multiplier and earned style thresholds
remain the existing native-checked catalog. Earned flags use a high-water
mark, so losing style points does not lock a previously earned item. Kind6
ranking-event completion is retained in the original event-status array.
The separate full UGRanking frontend is still an older open task; adding
these events does not claim that browser is complete.

The existing original speech lookup, music ducking and movie dispatcher are
used. Missing authored briefings remain silent. Speech entry0 is valid.
Event26 briefing and reward are distinct TJ entries. Unique selection, extro,
then earned magazine are the original4AB670 ordering; each acknowledgement
is persistent. Events19/25/27 snapshot the player's actual car and parts.

New route art is loaded on selection and unrelated route/album art is evicted
before atlas packing. Expanding event metadata must not preload all new map
images into the startup atlas. A region-specific car-name lookup also keeps
Tiburon races from failing before their first frame.

## Evidence and acceptance

- nfsu_career_retail: complete16..30 progression at all three difficulties,
  losses, optional failure, one-time prizes, reward choices, save migration,
  earned covers, movies and actual level2 purchases.
- nfsu_career_full_retail: every authored stage through production loading,
  simulation, HUD and recorded rendering commands, including new regions.
- nfsu_career_unique_ui_retail: original chooser, explanation, confirmation
  and grant pages for7/15/26, through CPU pixels and bounded GPU commands.
- nfsu_career_tournament_native_retail: regenerated original score/order witness.
- Full Default/Release retail gates and Switch packaging gates are required
  on the final frozen source. The checkpoint names their logs and receipts.

A local build and desktop playback do not establish Switch memory, frame
time, perceived speech/music mix or final transition appearance. The combined
NRO must still receive owner hardware acceptance. Original live PC execution
is currently unavailable. Previously open crash/audio parity, full Trade In,
whole-game rank/browser presentation and deferred drift HUD work remain open;
this event block does not silently close those older assignments.
