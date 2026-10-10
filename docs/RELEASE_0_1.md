# NFSU Switch 0.1 - playable free-driving preview

This release publishes the exact `boost-r6` NRO accepted on Switch by the
project owner on 2026-10-09. It is not a newly compiled or untested AI build.

## Included

- Olympic Square free driving with the tuned Skyline and original-data frontend.
- Working level 3 nitrous acceleration, bottle use, exhaust flame and sound;
  level 1 turbo and both HUD indicators.
- Minimap, lap timing, camera switching and held R3 look-back.
- Updated body/road reflections, headlights, light glow, particles and textures.
- Release rendering profile: 4x anisotropy, FXAA on, MSAA off, FPS overlay off.
- Atlantic is available through a development event launch; no menu track picker.

Read [the installation guide](GETTING_STARTED.md) before launching. You need
your own North American PC game data. No retail data is distributed here.

## Not included / still in progress

A complete career, finished opponent AI and the ongoing all-circuit-track
conversion are not part of 0.1. Handling/audio/visual fidelity and frame pacing
still need work. This preview does not promise a locked 60 FPS.

## Binary provenance

- Runtime source: `24665d0a03bbae52e5f16c5f1a1d38b62676a23a`.
- Embedded build tag: `boost-r6` (kept unchanged to identify the tested binary).
- The `v0.1` tag adds public documentation; runtime/build/shader inputs are unchanged.
- Release NRO SHA-256:
  `34ff730cdcd290cf2776ac57323abfd30aa85c71077ab966512ef5e3bc9959fd`.
- `SHA256SUMS.txt` authenticates the downloadable assets.

The original package passed the focused nitrous unit test, the retail boost
check with read-only before/after fixtures, composed PC captures for events
1 and 8, and Switch NRO/embedded-shader/frontend-profile checks. The owner
then verified working nitrous on actual Switch hardware. A complete current
Default/Release matrix was not rerun for this publication; do not interpret
these focused checks as full-game acceptance.

The screenshots are labeled by origin: `progress-switch-*` are earlier real
Switch captures; `progress-pc-*` are 0.1 CPU reference images. Neither provides
a frame-rate guarantee. They are progress illustrations, not game data packs.

The NRO and SD-ready ZIP are equivalent. The ZIP contains the NRO, English
installation/release notes and the bundled decoder license, with no game data.
Source and third-party attribution are available at the release tag.
