# v0.2-lan-r7 - LAN room and garage development snapshot

This is a **prerelease**, built from the current development integration.
It is not a stable 0.2 replacement or a claim of complete original-game parity.
The earlier v0.1 release and the owner's local 0.2 artifact remain unchanged.

## Downloads and installation

- `nfsu-switch-v0.2-lan-r7.nro`: main game with Multiplayer in its menu.
- `nfsu-switch-v0.2-lan-r7-sd.zip`: the same NRO in an SD-ready `switch/` tree,
  with a short installation guide. No retail assets or save files.
- `SHA256SUMS.txt`: checksums of the supplied assets.
- `garage-unique-warning-pc.png`: PC reference image, not a Switch capture.

Use your own **Need for Speed: Underground PC1.4 USA** files. Extract the
SD archive at the card root; copy the game's data directories under
`sdmc:/nfsu/`. Use full-memory application mode, not Album/applet mode.
Read [GETTING_STARTED](GETTING_STARTED.md) for the exact layout and controls.

## Since the historical 0.1 source publication

The current source snapshot includes campaign records/progression through
event113, tournaments and race modes, profiles, rewards/unlocks, garage,
Quick Race/Free Run, traffic/rivals, original-data audio/HUD and continued
physics/rendering recovery. Those systems have different remaining fidelity
and console acceptance tasks; source inclusion does not mean each is finished.
Campaign smoke and finish/save automation do not prove a complete driven
campaign or a locked 60 FPS result.

## Experimental LAN room

Use two consoles on the same router with the same build and their own data.
A creates a room; X joins a host IPv4 address. The host chooses Olympic Square
or Atlantic, direction, laps and Stock/Street/Pro/Extreme performance for both
cars. Drivers choose any playable car and their own appearance; Y opens the
garage. The guest presses A to ready, then the host starts with A.

The host computes both cars and sends snapshots to the guest. The prototype
has no AI grid, traffic or campaign rewards. It is custom router LAN, with
manual IP entry, not Nintendo online or direct local wireless. Guest latency,
synchronization and handling still need paired hardware verification in this
exact build. See [LAN_MULTIPLAYER](LAN_MULTIPLAYER.md).

## Garage r7 changes

- Original group/family/design paths and rim brand/style/diameter flow.
- Replace/Remove is a dialog over the vinyl layers, not a separate page.
- Unique vinyl exclusivity and hood-vinyl stock-hood confirmation, with the
  original messages and cancellation preserving the installed ride.
- A repeated design moves off its other layer; recoloring/reordering retains
  the correct layer focus, channel cursors and preview/cancel behavior.
- Authored GenericDialog art for confirmations; already installed vinyls
  show information rather than being bought again.
- Read-only PC checks confirmed the carbon surface of the exercised Civic
  CF hoods stays unchanged when repainting the body. The original base-paint
  branch has no separate carbon-paint warning; no new error was invented.

These garage changes have PC runtime/data checks. Their final appearance and
input behavior on Switch are still awaiting owner acceptance.

## Validation scope

The runtime/source integration passed its required Default and Release checks,
with retail fixtures and no skips. The final hood-button text follow-up reran
affected UI checks in both configurations. Switch NRO/shader packaging and
metadata/RomFS checks passed. These are bounded code/data/packaging results,
not console performance measurements or independent full-game approval.

The source publication preserves the integration's runtime, tests and build
inputs. Public README/install notes and fixture-byte checkout attributes are
publication changes. The binary retains build tag `garage-flow-r7` and custom
NACP title `NFSU_switch LAN r7`, display version 0.2. Its runtime source was
built before this publication-only documentation/packaging commit. Rebuilding
with another SDK need not produce an identical binary.

## Known issues / feedback

- Paired LAN timing and this exact r7 garage still need hardware checks.
- Remaining AI, suspension/contact/traffic behavior, collision sounds,
  drift HUD and frontend transitions continue to be compared with the original.
- Full campaign behavior, material/shadow fidelity and stable 60 FPS are not
  guaranteed by automated startup/finish tests.
- Use a complete, unmodified 1.4 USA data tree and application-memory homebrew.
  In particular, missing `Languages/English.bin` prevents the frontend loading.

Report build tag, console model, handheld/docked mode, event/course and steps,
plus available logs/video. Do not upload retail game files, personal saves or
credentials. This is an unofficial fan project; existing third-party notices
and the lack of a repository-wide license grant remain in effect.

## Source build corrections

The publication fixes first-configure test target registration by using the
already-declared NFSU_BUILD_TESTS option before CTest initializes its cache.
A fresh ON/OFF configure regression checks the affected executable targets.
The README retains the probe examples required by the CLI documentation guard;
[CLI_REFERENCE](CLI_REFERENCE.md) contains the full developer command reference.
These are host build/documentation corrections. Runtime source and the supplied
r7 NRO are unchanged.
