# What `nfsu_probe track-render` refuses, what it writes, and what it exits
# with. Every case below needs NO INPUT FILES AT ALL.
#
# WHY THIS FILE EXISTS. An independent mutation pass over the command's
# block of src/main.cpp scored 7 caught out of 34: nothing in the repository
# ran the command's body. docs/TRACK_RENDER.md justified that with "no test
# in this repository runs a probe command against real data", and the code
# contradicts it - `track-render <a path that does not exist> 0 16 0 16
# --out <new> --view-distance 10 --view-target 0 0 0` runs the whole body,
# the framing fallback, every override, validate_camera,
# render_track_draw_list, the report and write_frame_tga, exits 3 and writes
# a valid 1280x720 TGA. So the gap was a choice, not a structural limit.
#
# WHY A SCRIPT RATHER THAN MORE add_test ENTRIES. PASS_REGULAR_EXPRESSION
# makes CTest IGNORE the exit status, and the exit status is half of what is
# being pinned here: the survivor that mattered most turned a run whose
# reports failed into exit 0, which changes no printed byte. This file
# asserts the status, the text and whether a file exists, which no
# combination of test properties can do together.
#
# WHAT EACH CASE MUST ASSERT. A case that only required a refusal would pass
# with any refusal, so every one of them names the diagnostic it expects.
# And every case asserts the OUTPUT FILE's existence either way, because the
# command writes files and "wrote one where it should not have" is the
# failure this command can cause that the others cannot.
#
# Run from the build directory. PROBE is the built program.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE must be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_track_render_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(out "${work}/out.tga")
# A target that already exists, for the no-replace case. Its bytes are
# checked afterwards, so a run that replaced it is caught even if it also
# printed the refusal.
set(taken "${work}/taken.tga")
file(WRITE "${taken}" "not a tga")

# An input that does not exist. Deliberately not created: every case here is
# decided either before any file is opened, or by a pipeline that reports
# "failed to read file size" and carries on, and both are the point.
set(missing "${work}/missing_stream.bun")

set(failures 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case. `label` names it, `code` is the exact exit status required,
# `needle` is text the combined output must contain, `banned` is text it must
# NOT contain (empty for none), `wrote` is YES when the run must have created
# ${out} and NO when it must not have, and everything after that is the
# command line after "track-render".
#
# `banned` is there because some faults ADD a line rather than removing one,
# and a case that can only require presence cannot see those.
function(nfsu_case label code needle banned wrote)
    file(REMOVE "${out}" "${out}.partial")
    if(EXISTS "${out}")
        nfsu_fail("${label}: could not remove the output before the run")
        return()
    endif()
    execute_process(
        COMMAND "${PROBE}" track-render ${ARGN}
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    set(text "${stdout_text}${stderr_text}")

    # The failure class issue #87 is about, and the one a bad argc guard
    # produces here: an exception escaping main(), or a structured-exception
    # exit code, before anything is printed.
    if(text MATCHES "terminate called")
        nfsu_fail("${label}: the run terminated on an exception:\n${text}")
        return()
    endif()
    if(NOT status MATCHES "^[0-9]+$" OR status GREATER 128)
        nfsu_fail("${label}: exit status ${status} is a crash, not a refusal")
        return()
    endif()
    if(NOT status EQUAL ${code})
        nfsu_fail("${label}: exit status ${status}, expected ${code}\n${text}")
        return()
    endif()
    if(NOT text MATCHES "${needle}")
        nfsu_fail("${label}: expected the output to contain\n  ${needle}\nand it reads\n${text}")
        return()
    endif()
    if(NOT banned STREQUAL "" AND text MATCHES "${banned}")
        nfsu_fail("${label}: the output must NOT contain\n  ${banned}\nand it reads\n${text}")
        return()
    endif()
    if(wrote STREQUAL "YES")
        if(NOT EXISTS "${out}")
            nfsu_fail("${label}: no output file was written")
            return()
        endif()
        file(SIZE "${out}" size)
        if(NOT size EQUAL 3686418)
            nfsu_fail("${label}: the output is ${size} bytes, expected 3686418")
            return()
        endif()
    else()
        if(EXISTS "${out}")
            nfsu_fail("${label}: a file was written for a run that must write none")
            return()
        endif()
        if(EXISTS "${out}.partial")
            nfsu_fail("${label}: a .partial file was left behind")
            return()
        endif()
    endif()
    file(REMOVE "${out}" "${out}.partial")
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# The output rules. This command is the only nfsu_probe command whose
# failure mode is a file, so these come first.
# ---------------------------------------------------------------------------

nfsu_case("--out is required" 1 "--out <FILE> is required" "" NO
          "${missing}" 0 16 0 16)

nfsu_case("an existing --out is refused" 1 "already exists; this command never replaces a file" "" NO
          "${missing}" 0 16 0 16 --out "${taken}")

file(READ "${taken}" taken_after)
if(NOT taken_after STREQUAL "not a tga")
    message(SEND_ERROR "the existing target was modified by a run that refused it")
endif()

# THE ONE THAT WRITES A FILE WHERE CORRECT CODE WRITES NONE. A fatal
# diagnostic from the pipeline must stop the run before the render. Dropping
# the early return after build_track_scene_pipeline leaves the command
# printing this exact message and then rendering and writing the TGA anyway.
nfsu_case("a fatal pipeline diagnostic writes nothing" 1
          "the camera is degenerate and builds no frustum" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --eye 1 1 1 --target 1 1 1)

# ---------------------------------------------------------------------------
# The exit status. It is the machine-readable half of the contract and it
# appears nowhere in the printed report, so a run that always returned 0
# would look identical to a reader.
# ---------------------------------------------------------------------------

# A run whose reports failed: the geometry and object regions cannot be
# read, so the whole body still runs and writes a frame of background, and
# the status must say the reports failed. 3 exactly, not merely non-zero:
# 1 and 2 are this command's other refusals and mean something else.
nfsu_case("failed reports exit 3 and still write the frame" 3
          "failed to read file size" "" YES
          "${missing}" 0 16 0 16 --out "${out}"
          --view-distance 10 --view-target 0 0 0)

# ---------------------------------------------------------------------------
# The positional guard. Removing it lets std::logic_error escape main() -
# the same failure class issue #87 and tests/path_arguments.cmake exist to
# prevent - and shortening it by one gives a structured-exception exit.
# nfsu_case refuses both shapes above, so these two cases carry that.
# ---------------------------------------------------------------------------

nfsu_case("one positional short" 1 "Usage:" "" NO "${missing}" 0 16 0)
nfsu_case("three positionals short" 1 "Usage:" "" NO "${missing}" 0)
nfsu_case("no positionals at all" 1 "Usage:" "" NO)

# ---------------------------------------------------------------------------
# Option bounds. Each is decided before any file is opened. --alpha-cutoff
# is the one that changes rendered pixels with no diagnostic at any layer if
# its bound goes: 300 wraps to 44 in a uint8 and renders.
# ---------------------------------------------------------------------------

nfsu_case("--alpha-cutoff above a byte" 1 "--alpha-cutoff needs 0 to 255" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --alpha-cutoff 256)
nfsu_case("--alpha-cutoff wrapping a byte" 1 "--alpha-cutoff needs 0 to 255" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --alpha-cutoff 300)
nfsu_case("--lod-slot past the six" 1 "--lod-slot needs a slot from 0 to 5" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --lod-slot 6)
nfsu_case("--width zero" 1 "--width needs a size from 1 to 8192" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --width 0)
nfsu_case("--height past the limit" 1 "--height needs a size from 1 to 8192" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --height 8193)

# The framing options carry frame_track_bounds' own ranges, checked at parse
# time. Before that they were not refused at all: the framing failed on the
# bad value, the explicit-camera fallback caught the failure, and the run
# rendered from the renderer defaults and wrote a TGA with the option
# silently ignored.
nfsu_case("--view-margin below one" 1 "--view-margin needs a number from 1 to 100" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-margin 0.9
          --view-distance 5 --view-target 0 0 0)
nfsu_case("--view-fov past the limit" 1 "--view-fov needs a number from 1 to 170" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-fov 200
          --view-distance 5 --view-target 0 0 0)
nfsu_case("--view-pitch at the pole" 1 "--view-pitch needs a number from -89 to 89" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-pitch 90
          --view-distance 5 --view-target 0 0 0)
nfsu_case("--view-yaw past the limit" 1 "--view-yaw needs a number from -36000 to 36000" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-yaw 40000
          --view-distance 5 --view-target 0 0 0)
nfsu_case("--view-distance is not a number" 1 "--view-distance needs a finite number" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-distance notanumber)

# The same for a RANGED option, which is a different function:
# --view-distance goes through take_double and the four framing angles go
# through take_ranged. A take_ranged that accepted an unparsable value would
# leave the option at its default and write a TGA where this writes none.
nfsu_case("--view-yaw is not a number" 1
          "--view-yaw needs a number from -36000 to 36000" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-yaw notanumber
          --view-distance 5 --view-target 0 0 0)

# ---------------------------------------------------------------------------
# Options that are only meaningful in pairs. Each alone used to produce a
# plausible frame built on a value the caller never gave.
# ---------------------------------------------------------------------------

# Without a target, the framing has nothing to fall back to and must refuse
# rather than invent one. Accepting it renders at the car defaults' target
# of 0.2,0.35,0.
nfsu_case("--view-distance without a target" 2
          "the region could not be framed automatically" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-distance 10)
nfsu_case("--view-target without a distance" 2
          "the region could not be framed automatically" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-target 0 0 0)

# The cull camera's two halves. Either alone silently gives the other a
# zero vector and switches the policy to retail_tree, and the frame that
# comes out is wrong but plausible.
nfsu_case("--eye without --target" 1 "--eye and --target are given together" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --eye 1 2 3)
nfsu_case("--target without --eye" 1 "--eye and --target are given together" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --target 1 2 3)

# ---------------------------------------------------------------------------
# Enumerations and the catch-all. An unknown option must be refused, not
# ignored: ignoring it renders the wrong picture and says nothing.
# ---------------------------------------------------------------------------

nfsu_case("--order is an enumeration" 1 "--order must be visible-instance or" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --order sideways)
nfsu_case("--residency is an enumeration" 1 "--residency must be every or" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --residency sometimes)
nfsu_case("--cull is an enumeration" 1 "--cull must be none, back or front" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --cull sideways)
nfsu_case("--shading is an enumeration" 1 "--shading must be flat or none" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --shading gouraud)
nfsu_case("an unknown option is refused" 1 "unknown option --view-roll" "" NO
          "${missing}" 0 16 0 16 --out "${out}" --view-roll 5)
nfsu_case("a non-numeric region bound is refused" 1
          "geometry-size must be a decimal or 0x integer" "" NO
          "${missing}" 0 sixteen 0 16 --out "${out}")

# ---------------------------------------------------------------------------
# The camera the report prints. These four cases are the only ones here
# that read a rendered run's own numbers back, and they exist because an
# option can be parsed, range-checked and then dropped on the floor: the
# value reaches the report or it does not.
#
# With no readable geometry the automatic framing has no bounds to work
# from, so it fails and the run draws from the renderer defaults with the
# explicit distance and target applied. That is the path these pin, and it
# prints the reason, which is also pinned - without it, a frame that is not
# the one the options describe is produced with no signal at all.
# ---------------------------------------------------------------------------

nfsu_case("the framing angles reach the camera" 3
          "render: camera yaw 88 pitch -12 " "" YES
          "${missing}" 0 16 0 16 --out "${out}"
          --view-yaw 88 --view-pitch -12 --view-distance 5 --view-target 1 2 3)

nfsu_case("the distance override reaches the camera" 3
          "distance 5 " "" YES
          "${missing}" 0 16 0 16 --out "${out}"
          --view-yaw 88 --view-pitch -12 --view-distance 5 --view-target 1 2 3)

nfsu_case("the target override reaches the camera" 3
          "target 1,2,3 " "" YES
          "${missing}" 0 16 0 16 --out "${out}"
          --view-yaw 88 --view-pitch -12 --view-distance 5 --view-target 1 2 3)

nfsu_case("an abandoned framing is reported" 3
          "render: framing failed" "" YES
          "${missing}" 0 16 0 16 --out "${out}"
          --view-distance 5 --view-target 1 2 3)

# THE DRAW-LIST TRAILER IS NOT PINNED HERE, deliberately, and this comment
# is the whole of what this file has to say about it.
#
# A case was written for it and then deleted, because it could not fail:
# every case in this script runs without input files, so the draw list is
# always empty, the loop that prints the trailer never runs, and a build
# with the "args.list_limit > 0" guard REMOVED prints no trailer either. A
# reviewer removed that guard, rebuilt, ran the retail region and watched
# the trailer come back while this script still reported Passed.
#
# Catching it needs a non-empty draw list, which needs real geometry, which
# is the one thing every case here does without. It is pinned by the
# retail acceptance in docs/TRACK_RENDER.md, which names it in an
# expectation for exactly that reason - a manual check, not a gate, and
# said plainly there rather than implied here.

# ---------------------------------------------------------------------------
# Two guards that a later one would otherwise cover for. Both are about
# WHICH check fires, because a refusal that arrives from somewhere else
# costs the diagnostic and the exit status even when the outcome is safe.
# ---------------------------------------------------------------------------

# An output naming an input. nfsu::write_frame_tga refuses this too, so the
# guarantee does not rest here - but it refuses at exit 2 after the whole
# pipeline has run, and this is the early refusal that names the cause.
nfsu_case("--out naming an input is refused early" 1
          "--out must differ from every input file" "" NO
          "${missing}" 0 16 0 16 --out "${missing}")

# The fourth region number. A parse loop one iteration short leaves
# object-size at zero and reports nothing, and the three cases above only
# ever put a bad number in the second position.
nfsu_case("a non-numeric object-size is refused" 1
          "object-size must be a decimal or 0x integer" "" NO
          "${missing}" 0 16 0 sixteen --out "${out}")

# The command's own validate_camera. render_track_draw_list revalidates with
# identical text, so what this pins is that the refusal happens HERE, at
# exit 1 before the renderer, rather than at exit 2 inside it.
nfsu_case("an out-of-range near plane is refused before the render" 1
          "near plane must be within" "" NO
          "${missing}" 0 16 0 16 --out "${out}"
          --view-distance 5 --view-target 0 0 0 --view-near 100 --view-far 1)

# That case is refused by the NEAR bound, so it says nothing about whether
# --view-far reaches the camera at all. This one is refused by the FAR
# bound and can only be, so dropping the --view-far override - which would
# leave the default far plane in place and render - fails here.
nfsu_case("the far override reaches the camera" 1
          "far plane must be greater than near" "" NO
          "${missing}" 0 16 0 16 --out "${out}"
          --view-distance 5 --view-target 0 0 0 --view-far 0.0001)

file(REMOVE_RECURSE "${work}")
message(STATUS
    "track-render guards: 36 cases, each asserting the exit status, the "
    "diagnostic and whether a file was written, with no input files")
