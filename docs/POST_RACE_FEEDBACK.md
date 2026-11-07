# Post-race feedback and non-winning results

This pass follows UI_FLOW_PROFILES_R100.md. It changes the post-race frontend,
not vehicle acceleration, AI difficulty or the campaign's event catalog.

## Original evidence

The source is the read-only PC1.4 USA speed.exe with SHA-256
c0fad450912952f53809a54690d235aa8b4e25b9f46650693c7cbee1b4c074b7.
Ghidra functions used in this pass:

- 49E100/49E0B0: conditional results, style, reward and menu package queue.
- 49DFC0..49E0A7 and49FBC0: forward/back traversal, style skip and Next.
- 49BA10: results FadeOut completion before changing packages.
- 496240/4969E0: ordinary and tournament menu configuration.
- 496E00/497110/497180/4971F0: Continue, race/tournament restart and next-race
  confirmations; the original dialog's default focus is Cancel.
- 49CBD0: bounded selected-racer changes emit LEFT/RIGHT only when the row
  actually changes. They are not UP/DOWN sounds.
- 4CD0D0/4CCD20: post-race packages use the common frontend sound family.
- 4DC6E0: opening a confirmation emits SUMMON_DIALOG.
- 49F120/49F2E0: authored category, subtotal, multiplier and total Flashy states.
- 49E9D0: style unlock notices use the separate Style Reward title.
- 5A2F10: a failed terminal status or a non-first ordinary finish is not a win.

The authentic INGAMEB packages supply geometry, fonts and animation keys.
MU_PostRaceConfirm.fng is decoded from its authenticated package, rather than
drawn as a new hand-positioned dialog. Its existing textures are already in
the compact result atlas. No second renderer, music device or texture reload
is introduced for these page changes.

tools/post_race_selector_native.py executes original selector bytes and records
the exact sound notification as well as its admission. The fixture is
tests/post_race_selector_native.inc. tools/post_race_outcome_native.py executes
the ordinary original reward predicate for terminal statuses and places; its
fixture is tests/post_race_outcome_native.inc. Neither launches the original.
Tournament aggregation and time-trial deadlines retain their separate existing
native witnesses. The local sound witness executes the original static table
initializers, package-family resolver and common cue resolver.

## Sound and animation behavior

Up/down, left/right, select/back and summon-dialog clips come from the genuine
FE_COMMON_MB bank. Rejected Back on the first results page, clamped racer
selection and input blocked during an outgoing animation do not emit clicks.
The final accepted click drains before the race audio device is torn down.
Music retains the existing continuous post-race path. Native49BA10/4CD3E0
also select conditional victory speech from the authored event table. It is
requested only after a whole career-event win, including the final cumulative
tournament result, and is mixed on the same output with the original speech
ducking envelope. Events absent from that table and losses stay silent.
The shared English speech decoder currently retains its existing first-variant
policy for multi-variant groups; exact original speech RNG selection is not
claimed by this pass.

No new synthetic whoosh or ticking counter sound is invented for panels.
The recovered style update calls named visual states; those calls alone do
not constitute an audio event. Actual frontend notifications play their
corresponding original samples.

Results now finish their authored FadeOut before opening the next package.
Packages without that state do not receive an invented fade. Entry states
restart when a package is revisited; selection highlight keys, original
tournament backing variants, RewardBlurb AnimateIn and the recovered style
Flashy states are sampled against their own clocks. Draws without a clock
advance do not advance animations.

## A finish below first place

Ordinary campaign results still show every available physical racer and the
player's actual finishing row. Earned style is counted when present. Cash and
event victory unlocks require a win; being first in a stale live ranking does
not turn Knocked Out, Blown Engine, Totaled or Timeup into a win. A stale
elapsed clock does not replace terminal reason text in the result row.

Reaching a cumulative style threshold can unlock an item after a loss. That
notice is headed Style Reward, not Race Victory Reward. Continue, Statistics
and Restart remain available with native confirmations for terminal actions.
Statistics initially selects the player's actual result-order row.

An ongoing tournament has separate Quit Tournament, Statistics, Restart
Tournament, Restart Race and Next Race actions. A completed tournament has no
Next Race action. The confirmations are distinct. Restarting or quitting
does not commit the current race's completion, style or prize. Restarting the
tournament discards its intermediate standings and starts its first round.
Continuing a finished tournament uses the existing cumulative standings and
campaign reward/progression path; winning an individual round alone is not
the final tournament reward predicate.

UG0 victory retains its separate career introduction/movie path. UG0 loss
retains the retry/menu route. Timed challenges keep their original deadline
predicate; a solo racer's physical first place alone does not mean success.

## Validation and boundaries

frontend_tests covers rejected/accepted sound admission, native selector
notifications, loss flow, confirmations, reward backtracking, actual FNG
rendering, outgoing result vertex colors and real common-bank PCM playback.
career_tests compares the original terminal reward fixture and retains
time-trial/tournament/career reward checks. intro_grid_viewer's post-race mode
exercises the combined original package flow and confirmed terminal actions.

Final Default/Release/SDK gate receipts and the exact candidate identity are
stored with the packaged build. Software reference images in the local
evidence are UI layers on a synthetic neutral background, not a running-race
capture or a screenshot from the original executable.

Switch acceptance remains separate: hear click tails and dialog cues through
the actual device; inspect panel animation over the retained race scene;
finish below first; cancel/confirm Continue and Restart; inspect statistics;
exercise next-round, restart-race and restart-tournament actions. No complete
console acceptance or independent review is claimed from static analysis,
bounded native execution or SDK packaging alone. Other UI_AUDIT_R99 findings
outside the requested post-race flow remain separate work.
