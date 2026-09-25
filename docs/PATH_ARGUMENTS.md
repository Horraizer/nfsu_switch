# Path arguments

Which paths `nfsu_probe` and `nfsu_viewer` accept on the command line, which
they refuse, and why the two are not the same question as the repository's
ASCII-only artifact rule.

## The rule, in one paragraph

Both programs accept any path the operating system can hand them, including
one under a directory whose name is not ASCII. On Windows they refuse
exactly two things: an argument whose bytes do not decode in the process's
active ANSI code page, and an argument holding `?` anywhere but in a leading
`\\?\` extended-length prefix. Either refusal names the argument by
position, quotes it with every byte outside printable ASCII shown as `\xHH`,
says what is wrong, and exits 1 before any file is opened or written. On
other platforms nothing is refused, because there is nothing to decode.

The ASCII-only artifact rule in `CLAUDE.md` is about tracked files, paths,
branch names, commit messages and comments. It is not about the workspace,
and this repository's own checkout sits under a Cyrillic directory. A viewer
that refused such a path would be enforcing a rule that does not exist.

## Why an argument needs decoding at all

A Windows program with a narrow `main` never receives the characters that
were typed. Windows holds the command line as UTF-16 and, for a narrow
`main`, re-encodes it into the process's active ANSI code page before the
program starts. So `argv[k]` is a byte string in that code page, and the
program has to decode it with that same code page to get back to a file
name.

`std::filesystem::path` does not do that. libstdc++ stores a path as wide
characters on Windows and reads a narrow string as UTF-8, so
`std::filesystem::path(argv[k])` decodes ANSI bytes as UTF-8. For any
argument the code page spelled with a byte above `0x7E` that is not
accidentally valid UTF-8, the conversion throws
`std::filesystem::filesystem_error`. Nothing in either program catches it,
so it escapes `main` and the run ends in `std::terminate` with no
diagnostic and an abnormal exit status. Measured before the fix, on code
page 1251:

```text
> nfsu_probe summary E:\<Cyrillic>\nfsu-switch
terminate called after throwing an instance of 'std::filesystem::__cxx11::filesystem_error'
  what():  filesystem error: Cannot convert character sequence: Illegal byte sequence
exit status 0xC0000409
```

`src/arg_path.cpp` decodes with `MultiByteToWideChar` against the active
code page instead, and builds the path from the wide result, so the
conversion is the exact inverse of the one Windows performed and those paths
now open. That is the substance of the fix: the common case is not a
refusal, it is a path that works.

## What is refused, and why each one cannot be supported

**An argument that does not decode in the active code page.** There is no
second reading to fall back on. Guessing UTF-8 would open a different file
on a machine where the guess is wrong, and opening the wrong file is worse
than refusing.

**An argument holding `?`.** Windows substitutes `?` for every character the
active ANSI code page cannot express, and it does so before the process
starts, so a `?` in `argv` is either a character the user typed or the wreck
of one that could not be delivered, and nothing in the process can tell
those apart. Outside a leading `\\?\`, `?` is reserved in a Windows file
name, so neither reading names a file that can be opened. On other platforms
`?` is an ordinary file-name character and is not refused.

The exception is exactly one spelling. `\\?\C:\dir\out.tga` is the Windows
extended-length prefix, the only way to name a path longer than `MAX_PATH`,
and it is a legitimate spelling this project already handles in one place:
`strip_extended_prefix` in `src/render.cpp` exists for it,
`tests/render_tests.cpp` covers it, and `docs/RUNTIME_SKIN.md` records a
review that fixed `--skin-dump` for it. The `?` scan starts after that
prefix; a `?` anywhere later is refused as before, at its real offset.

What that place is, precisely, because an earlier draft of this paragraph
claimed more. `strip_extended_prefix` is reached only through
`absolute_name` at `src/render.cpp:366`, which is the same-file and
overwrite alias comparison, not a file open. The prefix does not work end
to end and never has: measured on this branch and identically on
`origin/main`, `nfsu_viewer <GEO> <TEX> \\?\C:\...\out.tga` fails with
Windows error 123 and writes nothing, and `nfsu_probe meshes
\\?\E:\...\GEOMETRY.BIN` reports the file as `\\GEOMETRY.BIN`.
`src/render.cpp:316-319` already says why: this standard library does not
parse the prefix, and `fs::absolute` rewrites it. So the project supports
the prefix as an alias spelling in its overwrite checks, and nothing more.

That is pre-existing and outside issue #87, and it does not change the
exemption: refusing the prefix would take away a spelling the alias checks
accept today, and would be a regression this branch introduced rather than
a defect it found.

### What the `?` refusal does not do

Two measurements, both reproducible with any program built by this
toolchain, and the second one is the reason this section exists rather than
a sentence claiming the rule prevents opening the wrong file.

The C runtime this project builds with **expands wildcards in `argv` before
`main` runs**. Measured with a program that prints `argv` byte by byte, in a
directory holding one file named `aXb.BIN`:

| argument on the command line | what `main` receives |
| --- | --- |
| `a?b.BIN` | `aXb.BIN`, one argument, the `?` gone |
| `a*.BIN` | `aXb.BIN`, and `argc` grows by one per further match |
| `q?z.BIN` (matches nothing) | `q?z.BIN`, unchanged |

So a `?` reaches this code only when it matched no file, which is also when
it could not have opened the wrong one. The harm the substitution could do -
a name the code page could not carry becoming `??.BIN`, which then matches
some unrelated `XY.BIN` - happens in the runtime, before any code here runs,
and this rule cannot see it. What the refusal buys is a diagnostic that
names the substitution, in place of a later `failed to read file size` that
does not. That is worth having and it is all it is.

`*` is reserved in a Windows file name as well, but Windows never produces
one by substitution, so nothing here has grounds to refuse it. On this
toolchain a `*` argument does not reach this code at all: the runtime
expands it, and one argument becomes several.

### Why it stays anyway

Once that measurement was in, the rule was worth deleting on its face, and
an independent reviewer was asked to weigh exactly that. It argued for
keeping it and the argument was accepted, on three measured points rather
than on taste:

* it costs nothing real. `?` cannot appear in a Windows file name, and the
  reviewer checked the retail data rather than assuming: no mesh, texture
  or car name in the retail 240SX `GEOMETRY.BIN`, `TEXTURES.BIN` or
  `GLOBAL/GLOBALB.BUN` contains one. So the claim under **Scope** below,
  that the non-path arguments have no spelling this refuses, holds against
  the data and not just against expectation;
* on this machine it is the whole refusal surface. Code page 1251 decodes
  all 128 of its high bytes, so the decoding refusal never fires here.
  Delete the `?` rule and the entire refusal path becomes unreachable on
  the primary development machine, and the diagnostic that names a
  substitution goes with it - along with any test that could observe
  either;
* that diagnostic is the real scenario, not a hypothetical one. A Japanese
  or Greek path on a code page 1251 machine arrives as `?`, and the caller
  gets a sentence naming the substitution instead of a downstream `failed
  to read file size` that says nothing about why.

The complexity it buys that with is four lines in `src/arg_path.cpp` plus a
scan offset, pinned by unit tests.

## What the code page decides, measured

Which byte sequences fail to decode is a property of the active code page,
not of the bytes. `tests/arg_path_tests.cpp` pins these against code pages
named in the test rather than against whichever one the machine runs:

| Code page | Refuses |
| --- | --- |
| 65001 (UTF-8) | any invalid UTF-8 sequence |
| 932 and other double-byte pages | a lead byte with no trail byte after it |
| 1253, 1257 | its unassigned bytes, `0xAA` and `0xA1` |
| 1251 | nothing: all 256 bytes are assigned |

So on this repository's own machine, whose active code page is 1251, the
decoding refusal never fires at all and `?` is the whole of what a run
refuses. That is measured, not assumed, and it is why the `?` rule carries
the refusal rather than being a footnote to it.

One thing the programs cannot see: a character the code page cannot express
arrives as `?`, so a path under a Japanese directory on a code page 1251
machine is refused rather than opened, and refusing is the only honest
answer available. The remedy is a code page that can express the name.

And one the reports share. On success both programs echo a path with
`std::filesystem::path::string()`, which on this standard library encodes it
as UTF-8 whatever the console's code page is. So a Cyrillic path prints as
mojibake on a code page 1251 console, and pasting that line back into
another command names a different file, which the code page accepts because
it assigns all 256 of its bytes. Refusal diagnostics are unaffected: they go
through the `\xHH` escaping above and are ASCII whatever the argument was.

## Scope

Both programs check every argument in one pass, before parsing, not only the
ones that become paths. The rest are command and option names, decimal and
hexadecimal numbers, and retail texture, mesh and car names, and none of
those has a spelling this refuses. Checking once means one diagnostic shape
and one place where the decision lives, rather than one per path argument.

`nfsu_probe` checks from `argv[1]`, `nfsu_viewer` from `argv[1]`. Neither
reads `argv[0]`.

## Where this lives

* `include/nfsu/arg_path.h`, `src/arg_path.cpp` - the conversion and the
  two refusals. The only file outside the window backend that calls the
  Windows API.
* `tests/arg_path_tests.cpp` - the conversion against stated code pages,
  against the code page the machine actually runs, and against the real
  filesystem, so that a conversion producing the right characters that
  still cannot open a file is not mistaken for a working one.
* `tests/path_arguments.cmake` - what the two programs do: the exit status,
  the diagnostic, the absence of the terminate this issue was about, and a
  Cyrillic path driven through every path *position* in turn, which is 48
  across the twenty-six probe commands and 8 across the viewer - the two
  numbers the gate itself prints, read out of its own `every position:`
  lines rather than counted by hand. Positions, not commands: the first
  version drove one position per program, and an independent review
  reverted nine individual call sites that each kept the suite green and
  each reproduced the crash. Reverting any one of the 53
  conversion sites now fails the suite, measured one at a time - the 39
  that predate `nfsu_probe track-probe`, `nfsu_probe track-budget` and
  `nfsu_probe race-events`, track-probe's own three when it arrived,
  track-budget's own two when it arrived, race-events' own two when it
  arrived, track-whole's own five when it arrived, and vehicle-sim's own
  two (`--script` and `--handling`) when it arrived, each of which
  `nfsu_path_arguments` refused. The file
  also refuses, in the source and before it runs anything, the three
  spellings that turn an argument into a path without the conversion; that
  is the guard that caught PR #97 arriving with one.
