# NFSU Switch v0.2-r42

Development prerelease for Nintendo Switch homebrew. Build tag: drag-grid-r42.

## Download and install

- NFSU_switch.nro is the standalone application.
- nfsu-switch-v0.2-r42-sd.zip places the same NRO at
  sdmc:/switch/nfsu-switch/NFSU_switch.nro and includes installation notes.
- SHA256SUMS.txt contains the download checksums.

Use your own Need for Speed: Underground PC 1.4 USA data. Copy the game data
directories into sdmc:/nfsu/ and use full-memory application mode. See the
[installation guide](https://github.com/Horraizer/nfsu_switch/blob/v0.2-r42/docs/GETTING_STARTED.md)
for the complete directory list. The archive contains no retail game data
or saves. The existing forwarder uses the same NRO path.

## Highlights since LAN r7

- Race resolution can be set to Auto, 540p or 720p. Motion blur, road
  reflections and MPH/km/h are configurable. Diagnostic file logs are opt-in.
- For a 60 FPS target in handheld mode, start with 540p at stock clocks.
  The project owner recommends 720p with a GPU overclock for the same
  target. Performance varies by race and scene.
- Updated profile selection, garage presentation, career voice cues,
  race endings and results music.
- Improved traffic crash presentation, collision recovery, and turbo HUD
  for cars with a turbo and no nitrous.
- The first drag of career tournament 9 now stages traffic before GO.
  Quick Race, Free Run and experimental two-player router LAN are available.

This is the same r42 NRO used for local delivery. Desktop validation used
PC 1.4 USA data. Switch executable, icon and RomFS packaging were checked.
This artifact still awaits a physical Switch playtest. Include the build
tag, Switch model, handheld/docked mode and event in feedback.
