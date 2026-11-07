# Quick Race and Free Run

The main-menu QUICK RACE entry opens a mode chooser for Circuit, Sprint,
Drag, Drift, Lap Knockout and Free Run. Free Run is solo practice. The
other modes use their race rules, countdown, finish and results. Quick Race
does not award career progression, change career saves or require an
Underground profile.

- Tracks and directions are selected from original track records. For a
  competitive mode, the matching TrackInfo mode flag must be set. Free Run
  retains the port's unrestricted practice on all authored courses, including
  drag and drift courses with their respective controls.
- Traffic offers None, Minimum, Moderate and Maximum. Original 4B55B0
  publishes the selector and 4B3AE6 uses the track's direction-specific pool.
  The live traffic driver also consumes the selector for its spacing rules.
  Drift has no road traffic.
- Race options include difficulty, opponent count, laps or catch-up where
  the mode uses them. Drift uses the existing score standings. Road rivals
  are stock car candidates chosen from the original 4B3C60 model list and
  performance ceiling. Their contexts come from the 4B4590 thresholds in
  GLOBALB. The port supplies its own setup seed and reuses the prepared
  campaign route assets for each selected track.
- Choose any playable car and customize it in the shared garage. Changes
  apply to the sandbox ride. Each car retains its setup until application exit.

Up/down selects a row. Left/right changes its value. A enters or starts.
B returns to mode selection or to the main menu. Y restores the car to
stock from setup. Plus opens the in-race pause menu.

## Verification

The retail test nfsu_quick_race_retail checks the mode catalog, direction
and traffic settings, representative sessions and Switch draw-package
admission. nfsu_free_drive_retail also exercises every authored practice
course and sandbox ride. The port uses a shared setup screen for these
options. The PC tests exercise the production world and command packages;
visual and performance acceptance on Switch requires a console run.
