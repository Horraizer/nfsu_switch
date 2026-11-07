# LAN multiplayer backlog

Status: parked by the owner on 2026-11-02. Main-game work has priority.

## Confirmed checkpoint

The owner reports that the standalone prototype starts after simultaneous
Netloader uploads to both Switches. This establishes a usable starting point,
but does not establish complete two-player race acceptance.

The main menu reuses the disabled Online row as MULTIPLAYER with a
COMING SOON badge. It does not launch the experimental prototype. Existing
Underground and Free Driving navigation remains available.

## Preserve for resumption

- Standalone host-authoritative LAN scene, shared vehicle simulation and
  two-player input/snapshot protocol.
- Offline SD startup/runtime diagnostics that remain valid after consoleExit.
- Correct nxlink argument transport: --args accepts one process argument
  containing the complete game argument string. Guest tokens must arrive
  as --join followed by the host IPv4, in that order.
- Simultaneous upload: the host can time out if the guest is sent later.

The original failed guest invocation was reproduced with the actual installed
nxlink against a loopback receiver. The repaired command's token order and
transferred NRO bytes were checked before the successful simultaneous retry.
Local receipts are under build/lan-root-r1; the latest corrected sender is
root-send-output-concurrent-r3.py. These are local recovery materials, not
published release artifacts.

## Remaining acceptance

1. Confirm an actual connection and independently controlled cars on both
   consoles, including latency and packet loss behavior.
2. Check contacts, both finishes, reversed roles and disconnect handling.
3. Replace launch arguments with the intended in-game lobby and error flow.
4. Run performance/memory checks for the final presentation and audio set.
5. Obtain independent review and owner hardware acceptance before enabling
   the main-menu entry.

Do not present prototype startup as completed multiplayer support. Do not
resume this feature until the owner moves it out of the backlog.
