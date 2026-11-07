# Two-player LAN room

Open Multiplayer in the Switch game's main menu. The standalone
`nfsu_switch_lan.nro` is also available for debugging. Use the same LAN
protocol version on both consoles.

## First room

- Both consoles connect to the same home router over Wi-Fi or Ethernet.
- Game data is the same owner-provided PC1.4 USA installation used by the
  ordinary port, under `sdmc:/nfsu`. Each console needs its own local data.
- Launch the LAN NRO through the usual application-memory homebrew setup.
- The menu displays the console's IPv4 address. Press A to create a room or X
  to enter the host's IPv4 address. The standalone accepts `--host`,
  `--join <IPv4>` and `--root <data root>`.
- The host chooses Olympic Square or Atlantic in either direction, 1 to 6
  laps and a performance tier for both cars: Stock, Street, Pro or Extreme.
- Both players choose their own car from the retail playable roster. In the
  main game, Y opens the visual garage and B at its root returns to the room.
  The guest presses A to confirm the current car and appearance. The host
  presses A to launch once the guest is ready.
- Both consoles load the same frozen settings. The host starts the shared
  countdown after both are ready and the guest has sent a pad sample.
- Ordinary player steering, automatic transmission, car/track contacts,
  car/car contacts, drafting, lap timing and finish order are used.
- ZR accelerates, ZL brakes/reverses, A uses nitrous when fitted, B is the handbrake, X changes the
  local camera and holding R3 looks back. Press + to leave the room.
- The other participant returns to the menu after acknowledging a disconnect.

No Nintendo online session is part of this custom router-LAN design.
Direct local wireless without a router is not implemented by this prototype.
There is no room discovery; enter the host address manually. Router client
isolation can prevent peers from communicating even on the same Wi-Fi.

## Authority and scope

The host owns both physical cars, the world contact queue and race timing.
The guest sends sequenced controls and renders host snapshots. The guest's
car uses the ordinary player path on the host, with the same steering and
strut-query policy as the host car; it does not use an AI physical boost.
Both actors count as humans, so the race does not end at the first finish.
The host tier selects all nine performance package fields for both actors.
Each driver chooses the car model and installed cosmetic hashes. Peers use
their own retail assets to resolve and draw the received parts. Body choices
do not change the host's performance tier or add physical aero bonuses.

Rendering chooses the local actor for camera, HUD and engine audio. The host
publishes each physics step, and the guest plays both actors on a shared host
timeline with about three steps of jitter headroom. A large reset snaps the
body and resets the camera when that step is presented. Guest physics are
still host-authoritative. `LAN_TIMELINE` reports received and presented steps,
underrun frames, and the time from sending a pad sample to receiving and
presenting a snapshot that acknowledges it. Presentation is measured at the
renderer call, not the display scanout.
The most recent guest timing summary is saved at room exit in
`sdmc:/nfsu-lan-runtime-XXXX.log` when the SD journal is available.
Audio presentation does not publish into the authoritative world RNG.

The initial room has no AI opponents, traffic, moving breakable props,
campaign progress, purchases or rewards. Track decoration may remain visible
without dynamic breakable behavior. The prototype's audio/visual presentation
is bounded and is not a claim of full retail multiplayer presentation parity.

UDP uses a versioned bounded wire format, explicit little-endian fields,
peer/session checks, ready retries, serial-number filtering and timeouts.
The host stops using stale remote throttle before the longer disconnect
timeout expires. It never waits for a datagram to advance an ordinary game
frame. There is no raw serialization of C++ structs and no video streaming.

## Development checks

`nfsu_lan_tests` covers malformed/truncated messages, non-finite state,
serial wrap, real loopback UDP, a lost room hello, reordered/duplicate
datagrams, peer authority, disconnect handling, independent human controls,
shared collision response, drafting and guest-local results presentation.

`nfsu_lan_probe --root <retail root> --inspect` loads the real circuit and
both cars and validates prepared render commands. The inspection also accepts
`--course 0..3`, `--laps 1..6`, `--tier 0..3`, `--car NAME`,
`--guest-car NAME` and `--guest-view`. `--list-cars` lists the retail roster.

Two instances of `nfsu_lan_probe`, one with `--host` and the other with
`--join 127.0.0.1`, use real UDP and synthetic pad pilots through the ordinary
player input path. Use `--port`, `--steps` and separate `--trace` outputs.
Existing trace paths are refused. Match canonical snapshot hashes by step.
Do not equate received visual snapshots with independent physical replay.

`--fast` is test pacing: each world step waits for a fresh synthetic guest
control, and every step publishes a snapshot. This prevents an accelerated
world from skipping the remote pilot's decision updates. It is not the game
host policy and does not measure realistic Wi-Fi latency. The normal probe
mode and the Switch game use time-based simulation and periodic snapshots.

Required Default/Release retail gates and Switch packaging checks still
apply. Two real consoles must verify loading, local controls, frame pacing,
contacts, both finishes, reversed host roles and disconnection before this
feature can be accepted. A compiled NRO and desktop checks are not that
hardware evidence. See the local `build/lan-root-r1` receipts/checkpoints for
the actual validation state; no hardware result is asserted here.
