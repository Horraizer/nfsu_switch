# Deferred: pre-start throttle, engine RPM and launch

Recorded: 2026-10-08.
Source: direct owner observation and request to record for later.
Status: deferred; no implementation or new worker assignment authorized now.

Owner reports that the original game allows pressing the accelerator and
revving the stationary car in neutral before the race starts. The current
port does not reproduce this pre-start behavior.

When the handling/tachometer scope resumes, verify the original countdown
input, engine/gear state and start transition, then restore supported behavior:
- Throttle before the start changes engine RPM while the car stays stationary.
- The tachometer and engine sound follow those pre-start engine revs.
- The transition into launch preserves the original RPM/gear behavior rather
  than inventing a reset or launch advantage.

The original mechanism and exact launch effect have not been measured here.
Do not label this as implemented or tested. Current graphics/reflection work
continues separately; this note does not widen its implementation scope.
