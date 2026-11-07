# Campaign finish follow-up

Role: worker, root owner integration, solo per explicit owner instruction.
Source baseline: campaign-results-r16 sealed source manifest and archive.
Owner reported a complete circuit win, immediate return to event map, the
circuit still offered, and no result sequence. Owner confirmed Netloader.
The r16 network capture contains only main-menu presentation and clean exit;
there is no race entry/result evidence. Do not overrule the owner report or
claim a localized cause for the missing pages from this capture.

Confirmed software defect: event_finished records a win and available excludes
won events, but selected_event retained the won race. The map consumes that
stale selection and its live scroller rejects selection outside available.
Correction: choose an available event after completion. Loading old saves
recovers a completed selection, preserves money/status, and redirects a stale
completed briefing to the map. Save schema is unchanged from r16.

The result/statistics implementation is shared by circuit, drag and drift.
The first normal page is results. Detailed statistics is a menu choice after
results/style/rewards. No code-local cause of the reported complete bypass
has been confirmed. Boundary-only SD journals now record race entry, terminal
flags, first submitted frame of each result page, choice and return. There
are no per-driving-frame filesystem writes. A post-race failure now reports
an error rather than silently returning a zero completion to the event map.
NFSU_BUILD logs the exact tag immediately after nxlink setup.

Validation from the actual workspace:
- cmake --build build --target nfsu_career_tests nfsu_intro_grid_viewer -j6
  PASS; build/campaign-flow-r17/build-pc-r2.log.
- nfsu_career_tests with the readonly PC1.4 USA installation PASS;
  career-final-r2.log includes win/loss progression, unavailable selection,
  stale-save recovery, original rewards and save recovery.
- nfsu_career_tests with feedback mode PASS; map-final-r2.log exercises a
  genuine post-win map CPU render and GPU command submission.
- nfsu_intro_grid_viewer with post-race mode PASS; pages-final-r2.log
  exercises circuit statistics, results/style/rewards and menu controls.
- build/campaign-flow-r17/build-switch-root-r1.sh PASS; final SDK build and
  selected NRO, embedded shader and GPU sample contract packaging checks are
  in switch-build-final-r2.log.
- build/surface-shadow-r1/scan-surface-shadow-r2.sh PASS; ascii-r1.log.

Full matrices and independent review remain deferred under the owner's
minimal-test and solo instructions. No commit, push, merge or publication.
Main checkout was clean at the validation boundary. Existing staged GLM merge
and all unrelated owner integration changes remain intact.

Remaining: Switch confirmation of progression and all post-finish pages.
The unexplained hardware page bypass is OPEN; new journals localize it if
it repeats. General animated texture/NOS/shadow acceptance remains OPEN.
No new upload until owner confirms current Netloader readiness.
