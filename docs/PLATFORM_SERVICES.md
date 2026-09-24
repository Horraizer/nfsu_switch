# Platform services

What this project asks of the platform it runs on, where each request is
answered, and what a Nintendo Switch build with libnx would have to answer
it with. The parent goal is issue #44, bringing the bounded CPU reference
renderer to the Switch; this document is issue #46, the boundary that makes
that a matter of adding an implementation rather than editing the renderer.
Controller input, which is one of the six services, has its own document:
[`INPUT.md`](INPUT.md).

## The rule, in one paragraph

A library that CMakeLists.txt lists in `NFSU_NEUTRAL_LIBRARIES` calls no
operating system. It reaches the platform only through the six interfaces in
`include/nfsu/platform.h` - filesystem, input, timing, logging, lifecycle and
display - which it is handed, never through a header, a call, a platform
test or a clock of its own. Platform code lives in exactly three libraries
and one source file, each named below with the reason it cannot be neutral.
`tests/platform_neutrality.cmake` enforces the rule on every CTest run and
fails by file, line and symbol; see **The neutrality gate** below.

## Which code is which

The split follows what CMakeLists.txt already said about each library; it
was read off, not decided here.

| Library | Kind | Why |
| --- | --- | --- |
| `nfsu_image` | neutral | a TGA encoder over a `std::ostream` |
| `nfsu_core` | neutral | "nothing under nfsu_core knows what an operating system is" |
| `nfsu_scene` | neutral | backend-neutral prepared scene over in-memory mesh types |
| `nfsu_track`, `nfsu_track_visibility`, `nfsu_track_zones`, `nfsu_track_draw_list`, `nfsu_track_render` | neutral | parsers over `nfsu_core`, and layers that decode and open nothing |
| `nfsu_stock_scene`, `nfsu_texture_decode` | neutral | read no files |
| `nfsu_body_skin`, `nfsu_skin_inputs`, `nfsu_runtime_skin` | neutral | the compositor and the two layers around it: standard library only |
| `nfsu_inspect` | neutral | "opens no window and calls no operating system" |
| `nfsu_inspect_window` | neutral but for one file | the session and the pan arithmetic are neutral; `src/inspect_window_win32.cpp` is exempted by name |
| `nfsu_input`, `nfsu_input_camera` | neutral | the controller model and its camera ([`INPUT.md`](INPUT.md)) |
| `nfsu_platform`, `nfsu_platform_fakes` | neutral | the interfaces, the code written against them alone, and the test fakes |
| `nfsu_arg_path` | platform | decodes arguments with `MultiByteToWideChar` and `GetACP` |
| `nfsu_render` | platform | its frame writer creates and publishes files with `CreateFileW` and `SetFileInformationByHandle`, or `open`, `link` and `unlink` |
| `nfsu_platform_desktop` | platform | reads the steady clock |

Reading files is not on the platform side. The parsers open files with
`std::ifstream` and size them with `std::filesystem::file_size`, both
standard C++ that every supported toolchain provides - devkitA64's newlib
and libstdc++ reach the SD card and RomFS through their devoptab mounts -
so they stay neutral and the gate reads them.

The programs, `nfsu_probe` and `nfsu_viewer`, are desktop programs and are
not libraries; the gate does not read them. A console build would have its
own entry point.

## Three rules for all six

- **One thread.** Every call is made on the thread that runs the session: the
  window thread on the desktop, the main thread on a console, where
  `appletMainLoop` has to run anyway. No implementation calls back into its
  caller from another thread. Nothing here is thread-safe, and nothing needs
  to be.
- **Nothing throws.** A failure is a return value, or, for the calls that
  cannot fail by contract, a documented degraded result. Only an allocation
  failure inside the standard library can escape, as it can from any other
  function in the project.
- **The caller owns the service.** Whoever constructs an implementation keeps
  it alive for as long as anything holds a reference to it. The code that
  uses a service stores a reference and never deletes it. The session also
  holds its `Inspector` by reference, and the inspector holds its scene by
  pointer: a backend keeps all three alive for the length of the loop.

## The six services

Each is cut to what a caller in this repository asks of it. Two have no
caller in the programs yet, and say so.

### Filesystem

```cpp
bool FileSystemService::argument_to_path(const char* text,
                                         std::filesystem::path& out,
                                         std::string& reason);
```

**Asked for by** both programs, through `nfsu::check_arguments` and
`nfsu::argument_path`, which now go through the desktop service, and by the
neutral helpers `check_argument_paths` and `argument_path_or_empty` in
`src/platform.cpp`, which hold the argument loop those two used to hold,
moved unchanged.

**Fails** by returning false with `out` untouched and a printable ASCII
sentence in `reason` that names the argument. A null argument is refused.

**Desktop:** `src/arg_path.cpp`, reached as `nfsu::desktop_file_system()`.
On Windows it decodes the bytes with the active ANSI code page and refuses
the two spellings [`PATH_ARGUMENTS.md`](PATH_ARGUMENTS.md) describes; on
POSIX it stores the bytes unchanged and refuses nothing. The two programs
were not touched - their 39 `nfsu::argument_path` calls and two
`nfsu::check_arguments` calls included - so `tests/path_arguments.cmake`
pins them exactly as before.

**libnx would supply** the POSIX behaviour: paths such as `sdmc:/...` are
UTF-8 and need no decoding. Arguments reach a homebrew application from the
loader, and from `nxlink -a` during development. Mounting the SD card is done
by libnx's default application init; mounting RomFS is not wanted, because
game data may never be bundled with the application.

**Not behind it yet:** the frame writer in `nfsu_render`. It needs an
exclusive create and a publish that never replaces a file, and it gets them
from `CreateFileW` with `CREATE_NEW` and a rename with `ReplaceIfExists =
FALSE` on Windows, or `open` with `O_CREAT | O_EXCL` and `link` on POSIX. On
a Switch the POSIX branch is the one newlib selects, and `link` is not
something a FAT32 SD card can do, so a screenshot would fail cleanly at
publication. Horizon's own file-system rename refuses an existing target,
which is the primitive a libnx publish would use; that is not verified here.
Moving the writer behind this service, with those two operations, is what
would let `nfsu_render` join the neutral list.

### Input

```cpp
bool InputService::next_event(InputEvent& event);
void InputService::set_pointer_capture(bool captured);
void InputService::sample_gamepads(GamepadFrame& frame);
```

An `InputEvent` is a resize, a pointer down, up, move or cancel, a wheel
movement in 1/120 of a notch, or a key. Resize travels with the pointer
because it is ordered with it: a move that follows a resize must be
converted with the new size.

**Asked for by** the inspection session, which takes every event through
`next_event` and asks for capture when a drag starts and gives it back when
the drag ends, and by `run_inspection_loop`, which samples the controllers
once per frame.

**Fails** softly by contract: `next_event` never waits and says false when
nothing is queued; capture is best effort, and losing a capture that was
granted arrives as a `pointer_cancel` event; a controller that cannot be read
is reported disconnected.

**Desktop:** `Win32Input` in `src/inspect_window_win32.cpp`. The window
procedure translates each input message into an event, queues it and has the
session take it at once; capture is `SetCapture` and `ReleaseCapture`. There
is no desktop controller backend, so every slot reads disconnected.

**libnx would supply** pointer events from the touch screen
(`hidGetTouchScreenStates`, the first touch as the primary pointer), a
capture that does nothing because a touch is already captured, and
controllers from the pad API: `padConfigureInput`, `padInitializeDefault`
for handheld and player 1 together, `padInitialize` for players 2 to 8,
`padUpdate`, `padGetButtons` and `padGetStickPos`.
[`INPUT.md`](INPUT.md) gives the button mapping.

### Timing

```cpp
std::uint64_t TimingService::now_us();
```

Microseconds on a monotonic clock with an arbitrary origin; never smaller
than a value it returned before; cannot fail.

**Asked for by** `run_inspection_loop` alone, which stamps each controller
sample with it. Nothing in the programs keeps a clock: the window repaints
when something changes, not on a timer. That is also why the controller
model never reads the clock itself - a frame carries its own time, so a
recorded session replays without one.

**Desktop:** `DesktopTiming`, `std::chrono::steady_clock` in microseconds.
**libnx would supply** `armTicksToNs(armGetSystemTick()) / 1000`.

### Logging

```cpp
void LoggingService::write(LogLevel level, const std::string& line);
```

Best effort, one line per call, ASCII: the implementation escapes every
byte outside printable ASCII, so a caller may pass a raw file name.

**Asked for by nothing yet.** The libraries report failures as return values
and error strings, the programs print to their own streams, and the window
reports a failed render in its overlay. The interface is kept to one call
until a caller needs more. The likely first caller is a console backend:
its overlay has no font yet (see **Display**), so a failed render there
would otherwise go unreported.

**Desktop:** `DesktopLogging`, "`<level>: <line>`" on a stream the caller
owns. **libnx would supply** standard output redirected over the network
with `nxlink` during development, or a file on the SD card; which one is
not decided.

### Lifecycle

```cpp
bool LifecycleService::pump();
void LifecycleService::request_exit();
```

`pump` lets the platform do its own work and says whether the session may go
on; once false it stays false, and it may block while there is nothing to
do. `request_exit` asks for the end a user closing the session would cause.

**Asked for by** the Win32 backend's loop and `run_inspection_loop`, which
pump until told to stop; the session, whose `Q` key asks to exit; and the
frame loop again, when a controller's `exit_session` is pressed.

**Desktop:** `Win32Lifecycle`: one pump is one `GetMessageA`, translated
and dispatched, and an exit request is the `WM_CLOSE` a user closing the
window sends. **libnx would supply** `appletMainLoop()` as the pump, which
returns false when the system asks the application to close, and a flag for
the exit request. With libnx's default focus handling an application is
suspended inside `appletMainLoop` while the HOME menu has focus - expected,
not verified here - and the first controller frame after it resumes then
reports the whole absence, which the controller model clamps to 100 ms
([`INPUT.md`](INPUT.md)).

### Display

```cpp
void DisplayService::surface_size(int& width, int& height);
bool DisplayService::begin_frame(int& width, int& height);
void DisplayService::draw_frame(const DisplayImage& image,
                                const std::vector<std::string>& overlay);
void DisplayService::end_frame();
void DisplayService::request_redraw();
```

A frame is B, G, R, 255 pixels, rows top down, stretched nearest-neighbour
over the whole surface, with the overlay lines drawn on top from the top
left. `begin_frame` may refuse; the implementation has then cleaned up and
the caller does not call `end_frame`. Otherwise `end_frame` is called
whether or not anything was drawn.

**Asked for by** the session, which asks the surface size when it starts,
presents one frame per paint, and asks for a redraw after every change the
window used to invalidate for.

**Desktop:** `Win32Display`: `begin_frame` is `BeginPaint` and
`GetClientRect`, `draw_frame` the double-buffered `StretchDIBits`, GDI text
and `BitBlt` the window always drew with, `end_frame` is `EndPaint`, and a
redraw request is `InvalidateRect`.

**libnx would supply** a `Framebuffer` over the default `NWindow`, created
with `framebufferCreate` in `PIXEL_FORMAT_BGRA_8888`, which is the session's
own byte order; `framebufferBegin` and `framebufferEnd` for the frame; a
nearest-neighbour stretch, because the session renders at a fraction of the
surface while the camera moves; and a flag for the redraw request. The
overlay needs a font the project does not have, and where text is
rasterised is not decided (see below).

## The inspection session: what moved out of the window

Everything `src/inspect_window_win32.cpp` decided that was not a window is
now `InspectionSession` in `src/inspect_session.cpp`, compiled on every
platform and tested through the fakes. It was moved, not rewritten: each
function below did the same thing in the same order as the window handler
it came from, and each Win32 call became a service call at the point where
it was made.

| Window procedure, before | Session, after | Service call it makes |
| --- | --- | --- |
| `WM_SIZE` | `handle(resize)` | `request_redraw` |
| `WM_LBUTTONDOWN`, `WM_RBUTTONDOWN`, `WM_MBUTTONDOWN` | `handle(pointer_down)` | `set_pointer_capture(true)` |
| any button up | `handle(pointer_up)` | `set_pointer_capture(false)`, `request_redraw` |
| `WM_CAPTURECHANGED` | `handle(pointer_cancel)` | none |
| `WM_MOUSEMOVE` | `handle(pointer_move)` | `request_redraw` |
| `WM_MOUSEWHEEL` | `handle(wheel)` | `request_redraw` on a whole notch |
| `WM_KEYDOWN` | `handle(key_down)` | `request_exit` for `Q`, `request_redraw` for a command |
| `WM_PAINT` | `update()`, then `present()` | `begin_frame`, `draw_frame`, `end_frame` |

What stayed in the window file is what only a window can do: registering
the class, creating the window, translating a message into an event or a
virtual key into a `Key`, the message loop, and the GDI drawing. The one
ordering that needed care is the capture change Windows sends from inside
`ReleaseCapture`: the window procedure queues each event and has the session
take it at once, so that nested message is still handled inside the drag's
release, exactly as it was when both were handlers in one file.

Two differences that change nothing a window shows: the overlay lines are
built before the memory bitmap is created rather than after, which is pure
computation; and the session refuses to render an inspector with no scene,
which `run_inspection_window` already refuses before a session exists.

The evidence that nothing changed is in four places. The Win32 backend is
built with MinGW - the project's own toolchain - in this branch with no
warning, and `nfsu_inspect_tests.exe` passes under Wine against it. The
window itself was compared with the one on `main`: a one-off program, not
part of the tree, linked once against each build's libraries, opened the
window under Wine and Xvfb, sent it the messages of a click, an orbit, two
pans, a lost capture, the wheel, every key and a resize, and read back the
client area's pixels, the viewport, the camera and who holds the capture at
41 checkpoints. The two builds printed the same lines, three runs each, and
ten deliberate faults in the new window file - a key misread, the buttons
swapped, a release, a lost capture or a resize not reported, a redraw that
invalidates nothing, the overlay not drawn, the wheel reversed, the capture
never taken - each changed them. The logic the window hands to the session
is covered on every platform by `tests/platform_tests.cpp`, which could not
test it before: a drag and its viewport, the click slack, a click on a
stretched frame being dropped, the wheel's fraction of a notch, every key
in the table against an inspector given that command, a lost capture, a
resize, a failed first frame, the highlight's exact colour. And the stub
that replaces the window elsewhere, `src/inspect_window_none.cpp`, is
untouched and still refuses a valid inspector with the same words.

### Kept as they were

Two behaviours moved with the code although they look wrong, because the
move was to change nothing. Both are the same on `main`.

- **A second button during a drag.** The session asks for the capture on
  every press. Under Wine, `SetCapture` from the window that already holds
  the capture sends that window `WM_CAPTURECHANGED`, which arrives as a
  `pointer_cancel`: the drag ends instead of switching to the new button;
  the viewport returns to full size without a redraw, so the drag-size
  frame stays on screen until the next paint, as after any lost capture;
  and the release that follows finds no drag, so nothing gives the capture
  back. The comparison above shows all three on both builds. Whether
  Windows itself sends the message in that case is not verified here.
- **A render that fails after one that worked.** `render_scene` clears the
  frame before it draws, and the pixel-test limit can stop it part way. The
  session then presents that partly drawn frame with the `render failed`
  line, not the frame before it. `tests/platform_tests.cpp` checks the line
  and that the session goes on, and deliberately pins no pixels of it.

## The frame loop

`run_inspection_loop` in `include/nfsu/inspect_session.h` is the loop a
platform that draws on its own clock runs - a console, or a test - and it
uses four of the six: input, timing, lifecycle and display. Each iteration
pumps the lifecycle, feeds every queued event to the session, samples the
controllers and applies them through `GamepadCamera` when one is given,
renders when the frame is stale, and presents only when something changed.
While a controller moves the camera the frame is rendered at 1 / drag
divisor of the surface, as the window does for a drag, and the full size
returns on the frame the motion stops, including when a controller
disappears mid-gesture. The Win32 backend does not use it: its loop is the
message loop.

## What a libnx backend has to supply

1. The six implementations above, owned by its `main`.
2. `padConfigureInput` once, a `PadState` per player, and the button mapping
   of [`INPUT.md`](INPUT.md).
3. A `Framebuffer` in BGRA order and a nearest-neighbour stretch.
4. A scene prepared from data on the SD card, an `Inspector` over it, and a
   call to `run_inspection_loop` with a `GamepadCamera`.

Nothing above the services changes for it. The gate is what keeps that true.

## The neutrality gate

`tests/platform_neutrality.cmake`, registered as `nfsu_platform_neutrality`,
reads every source of every neutral library and every project header they
reach, and fails on any of four things, naming the file, the line and the
symbol and quoting the code:

- an `#include` of anything but a C or C++ standard header or a project
  header - an allowlist, because platform headers cannot be listed;
- a call to a known operating-system function: POSIX, `pthread_*`, the
  Win32 API, the MSVC runtime's underscore calls, libnx's and deko3d's
  prefixes, `std::system`;
- a preprocessor conditional testing a platform macro or `__has_include`;
- a read of a clock, an entropy source or the environment.

Every library target must be classified. The manifest CMakeLists.txt writes
once the whole directory has been read lists them all, and one in neither
list, or in both, fails by name. The gate measures its own rules before it
reads the tree, against a corpus of lines each rule must catch - including
traps for its tokeniser - and ordinary lines none may, and it measures its
classifier on made-up lists. The script's header states what it cannot see:
an operating-system function it does not name reached through a standard
header, a call through a pointer or a macro, what a library links, whether a
classification is right, and two C++ spellings the tokeniser does not parse.

A new library goes in `NFSU_NEUTRAL_LIBRARIES` if it calls no operating
system, and the gate will read it; or in `NFSU_PLATFORM_LIBRARIES` with a
comment saying what it calls and why that cannot sit behind a service. A
source of a neutral library that must be platform code goes in
`NFSU_PLATFORM_SOURCES`, which today holds only the Win32 window.

## Not decided yet

- **Rendering.** The session renders with the CPU reference renderer, which
  is the renderer issue #44 brings to the Switch. A deko3d backend replaces
  that render step; whether it becomes a seventh service or a second session
  is not decided.
- **Overlay text.** The Win32 display draws the overlay with GDI. A console
  has no font; whether the display rasterises the lines or the session hands
  it pixels is open.
- **Focus and suspension.** The lifecycle reports only whether to go on. A
  drag interrupted by the HOME menu ends when the touch lifts; whether the
  session should hear about focus changes is open.
- **Resolution changes.** Docked and handheld output differ. Whether the
  display reports that as a resize event is open.
- **The frame writer.** It stays platform code inside `nfsu_render` until it
  moves behind the filesystem service; see **Filesystem**.
- **Logging's first caller and its console sink.**
- **A keyboard on the console, and a controller on the desktop.** Neither
  has a backend.

## Where this lives

- `include/nfsu/platform.h`, `src/platform.cpp` - the six interfaces, the
  event constructors and the neutral argument helpers (`nfsu_platform`).
- `include/nfsu/platform_fakes.h`, `src/platform_fakes.cpp` - deterministic
  fakes, which read no clock, environment or file (`nfsu_platform_fakes`).
- `include/nfsu/platform_desktop.h`, `src/platform_desktop.cpp` - the
  desktop timing and logging services (`nfsu_platform_desktop`).
- `include/nfsu/arg_path.h`, `src/arg_path.cpp` - the desktop filesystem
  service.
- `include/nfsu/inspect_session.h`, `src/inspect_session.cpp` - the session
  and the frame loop; `src/inspect_window_win32.cpp` - the desktop display,
  input and lifecycle.
- `tests/platform_tests.cpp` - the session, the loop and the helpers through
  the fakes, the desktop clock and log, and the stub.
- `tests/platform_neutrality.cmake` - the gate.
