# What `nfsu_probe track-probe` refuses and what it exits with. Every case
# below needs NO INPUT FILES AT ALL.
#
# WHY THIS FILE EXISTS. tests/track_render_guards.cmake was written because an
# independent mutation pass over one command's block of src/main.cpp scored 7
# caught out of 34: nothing in the repository ran the command's body. That was
# true of every nfsu_probe command, and it is still true of every one but two.
# This file is the second, for the same reason and in the same shape.
#
# WHY A SCRIPT RATHER THAN add_test ENTRIES. PASS_REGULAR_EXPRESSION makes
# CTest IGNORE the exit status, and the exit status is half of what is being
# pinned: a run whose reports failed exits 3, a usage error exits 1 and a
# pipeline failure exits 2, and none of those three appears in a printed byte.
#
# WHAT IS DIFFERENT FROM THE RENDER SCRIPT. track-probe writes no file, so
# there is no output rule to pin and no target that could be replaced. What
# takes its place is a ban: no run may print a "wrote:" line, because a probe
# that started writing something would be a new failure mode rather than a
# wrong number.
#
# WHAT EACH CASE MUST ASSERT. A case that only required a refusal would pass
# with any refusal, so every one of them names the diagnostic it expects. The
# last group is the other half: runs that get all the way through the body
# with no readable input, which is where the report's own lines are pinned.
#
# Run from the build directory. PROBE is the built program.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE must be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_track_probe_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# An input that does not exist. Deliberately not created: every case here is
# decided either before any file is opened, or by a pipeline that reports
# "failed to read file size" and carries on, and both are the point.
set(missing "${work}/missing_stream.bun")

set(cases 0)

# One case. `label` names it, `code` is the exact exit status required,
# `needle` is text the combined output must contain, `banned` is text it must
# NOT contain - empty for none - and everything after that is the command line
# after "track-probe".
function(nfsu_case label code needle banned)
    execute_process(
        COMMAND "${PROBE}" track-probe ${ARGN}
        WORKING_DIRECTORY "${work}"
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    set(text "${stdout_text}${stderr_text}")

    # The failure class issue #87 is about, and the one a bad argc guard
    # produces here: an exception escaping main(), or a structured-exception
    # exit code, before anything is printed.
    if(text MATCHES "terminate called")
        message(SEND_ERROR "${label}: the run terminated on an exception:\n${text}")
        return()
    endif()
    if(NOT status MATCHES "^[0-9]+$" OR status GREATER 128)
        message(SEND_ERROR "${label}: exit status ${status} is a crash, not a refusal")
        return()
    endif()
    if(NOT status EQUAL ${code})
        message(SEND_ERROR "${label}: exit status ${status}, expected ${code}\n${text}")
        return()
    endif()
    if(NOT text MATCHES "${needle}")
        message(SEND_ERROR
            "${label}: expected the output to contain\n  ${needle}\nand it reads\n${text}")
        return()
    endif()
    if(NOT banned STREQUAL "" AND text MATCHES "${banned}")
        message(SEND_ERROR
            "${label}: the output must NOT contain\n  ${banned}\nand it reads\n${text}")
        return()
    endif()
    # This command writes nothing, ever. A run that started to would be a new
    # failure mode rather than a wrong number, and every case watches for it.
    if(text MATCHES "wrote:")
        message(SEND_ERROR "${label}: the command wrote a file\n${text}")
        return()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# The positional guard. Removing it lets std::logic_error escape main() - the
# same failure class issue #87 and tests/path_arguments.cmake exist to prevent
# - and shortening it by one gives a structured-exception exit. nfsu_case
# refuses both shapes above, so these carry that.
# ---------------------------------------------------------------------------

nfsu_case("no positionals at all" 1 "Usage:" "")
nfsu_case("three positionals short" 1 "Usage:" "" "${missing}" 0)
nfsu_case("one positional short" 1 "Usage:" "" "${missing}" 0 16 0)

# ---------------------------------------------------------------------------
# The four region numbers. A parse loop one iteration short leaves the last
# one at zero and reports nothing, so the first and the last are both driven.
# ---------------------------------------------------------------------------

nfsu_case("a non-numeric geometry-size is refused" 1
          "geometry-size must be a decimal or 0x integer" ""
          "${missing}" 0 sixteen 0 16)
nfsu_case("a non-numeric object-size is refused" 1
          "object-size must be a decimal or 0x integer" ""
          "${missing}" 0 16 0 sixteen)

# ---------------------------------------------------------------------------
# The option table. An unknown option must be refused, not ignored: ignoring
# it answers a question the caller did not ask and says nothing.
# ---------------------------------------------------------------------------

nfsu_case("an unknown option is refused" 1 "unknown option --view-roll" ""
          "${missing}" 0 16 0 16 --view-roll 5)

# EVERY OPTION IS GIVEN AT MOST ONCE, except the three that repeat. That is
# what makes a selection deterministic: an option written twice would make the
# answer depend on the order the command line was written in.
nfsu_case("a repeated option is refused" 1 "--limit is given at most once" ""
          "${missing}" 0 16 0 16 --limit 1 --limit 2)
nfsu_case("a repeated camera option is refused" 1
          "--eye is given at most once" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --eye 4 5 6)
# And the three that DO repeat are not caught by it. --texture twice is a
# wasted read and not an error, so this run reaches the body and exits 3.
nfsu_case("a repeatable option is not refused" 3
          "probe: instances 0" "is given at most once"
          "${missing}" 0 16 0 16 --texture "${missing}" --texture "${missing}"
          --eye 1 2 3 --look-at 4 5 6)

# ---------------------------------------------------------------------------
# Options that are only meaningful in pairs or in company. Each alone used to
# produce a plausible answer built on a value the caller never gave.
# ---------------------------------------------------------------------------

nfsu_case("--eye without --look-at" 1 "--eye and --look-at are given together"
          "" "${missing}" 0 16 0 16 --eye 1 2 3)
nfsu_case("--look-at without --eye" 1 "--eye and --look-at are given together"
          "" "${missing}" 0 16 0 16 --look-at 1 2 3)
nfsu_case("a framing angle beside an explicit eye" 1
          "describe the automatic framing" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6 --view-yaw 10)
nfsu_case("a framing margin beside an explicit eye" 1
          "describe the automatic framing" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6 --view-margin 2)
nfsu_case("--zones without --zone-point" 1
          "--zones and --zone-point are used together" ""
          "${missing}" 0 16 0 16 --zones "${missing}")
nfsu_case("--zone-point without --zones" 1
          "--zones and --zone-point are used together" ""
          "${missing}" 0 16 0 16 --zone-point 0 0)
nfsu_case("--zones beside an explicit residency" 1
          "--zones replaces the residency" ""
          "${missing}" 0 16 0 16 --zones "${missing}" --zone-point 0 0
          --residency every)
nfsu_case("--zone without the zone rule" 1 "--zone needs --residency zone" ""
          "${missing}" 0 16 0 16 --zone 303)
nfsu_case("--instance without --section" 1 "--instance needs --section" ""
          "${missing}" 0 16 0 16 --instance 0)
nfsu_case("--mask under the fallback policy" 1
          "not consulted under --policy draw-everything" ""
          "${missing}" 0 16 0 16 --policy draw-everything --mask 1)
nfsu_case("--screen-scale under the fallback policy" 1
          "not consulted under --policy draw-everything" ""
          "${missing}" 0 16 0 16 --policy draw-everything --screen-scale 5)

# ---------------------------------------------------------------------------
# Enumerations and byte bounds, each decided before any file is opened.
# ---------------------------------------------------------------------------

nfsu_case("--order is an enumeration" 1 "--order must be visible-instance or"
          "" "${missing}" 0 16 0 16 --order sideways)
nfsu_case("--policy is an enumeration" 1 "--policy must be retail-tree," ""
          "${missing}" 0 16 0 16 --policy sometimes)
nfsu_case("--residency is an enumeration" 1
          "--residency must be every, no-zone or zone" ""
          "${missing}" 0 16 0 16 --residency sometimes)
nfsu_case("--mask past a byte" 1 "--mask needs a byte from 0 to 255" ""
          "${missing}" 0 16 0 16 --mask 256)
nfsu_case("--lod-slot past the six" 1 "--lod-slot needs a slot from 0 to 5" ""
          "${missing}" 0 16 0 16 --lod-slot 6)
nfsu_case("--mesh with no name" 1 "--mesh needs a prepared mesh name" ""
          "${missing}" 0 16 0 16 --mesh)
nfsu_case("--limit is not a number" 1
          "--limit needs a decimal or 0x integer" ""
          "${missing}" 0 16 0 16 --limit notanumber)

# ---------------------------------------------------------------------------
# The camera ranges. Each is the range the library enforces, checked at parse
# time so the diagnostic names the option and arrives before any file is
# opened. Without them the value reaches the camera, the camera is refused,
# and the message names a field rather than the option that set it.
# ---------------------------------------------------------------------------

nfsu_case("--width zero" 1 "--width needs a size from 1 to 8192" ""
          "${missing}" 0 16 0 16 --width 0)
nfsu_case("--height past the limit" 1 "--height needs a size from 1 to 8192" ""
          "${missing}" 0 16 0 16 --height 8193)
nfsu_case("--view-fov past the limit" 1
          "--view-fov needs a number from 1 to 170" ""
          "${missing}" 0 16 0 16 --view-fov 200)
nfsu_case("--view-pitch at the pole" 1
          "--view-pitch needs a number from -89 to 89" ""
          "${missing}" 0 16 0 16 --view-pitch 90)
nfsu_case("--view-yaw is not a number" 1
          "--view-yaw needs a number from -36000 to 36000" ""
          "${missing}" 0 16 0 16 --view-yaw notanumber)
nfsu_case("--view-margin below one" 1
          "--view-margin needs a number from 1 to 100" ""
          "${missing}" 0 16 0 16 --view-margin 0.9)
# The two bounds spelled in exponent form. format_double is the shortest
# round trip, so kMinNear prints as 1e-04 and kMaxFar as 1e+05, and the '+'
# is escaped because this needle is a regular expression.
nfsu_case("--view-near past the limit" 1
          "--view-near needs a number from 1e-04 to 10" ""
          "${missing}" 0 16 0 16 --view-near 20)
nfsu_case("--view-far past the limit" 1
          "--view-far needs a number from 1e-04 to 1e\\+05" ""
          "${missing}" 0 16 0 16 --view-far 200000)

# The three multi-value options, one value short. take_vec3 and the pair
# reader are separate functions, so a bounds check dropped from either is a
# read past the argument vector rather than a refusal.
nfsu_case("--eye one value short" 1 "--eye needs three finite numbers" ""
          "${missing}" 0 16 0 16 --eye 1 2)
nfsu_case("--fly one value short" 1 "--fly needs three finite numbers" ""
          "${missing}" 0 16 0 16 --fly 1 2)
nfsu_case("--turn one value short" 1 "--turn needs a yaw and a pitch" ""
          "${missing}" 0 16 0 16 --turn 5)
nfsu_case("--turn with a value that is not a number" 1
          "--turn needs a yaw and a pitch" ""
          "${missing}" 0 16 0 16 --turn notanumber 0)

# ---------------------------------------------------------------------------
# The camera the library refuses, reported by the command rather than left to
# produce an answer about a camera nobody named.
# ---------------------------------------------------------------------------

nfsu_case("an up vector along the view is refused" 1
          "parallel to the view direction" ""
          "${missing}" 0 16 0 16 --eye 0 0 10 --look-at 0 0 0 --up 0 0 1)
nfsu_case("a pitch onto the up axis is refused" 1 "no basis" ""
          "${missing}" 0 16 0 16 --eye 0 0 10 --look-at 0 0 0 --turn 0 90)
nfsu_case("an eye at its target is refused" 1 "eye-to-target distance" ""
          "${missing}" 0 16 0 16 --eye 1 1 1 --look-at 1 1 1)

# ---------------------------------------------------------------------------
# The framing, which has nothing to frame here. Without an explicit camera
# there is no fallback to invent, so the run must refuse rather than draw a
# conclusion about a camera it chose for itself.
# ---------------------------------------------------------------------------

nfsu_case("an unframeable region with no explicit camera" 2
          "the region could not be framed automatically" "probe: camera"
          "${missing}" 0 16 0 16)

# ---------------------------------------------------------------------------
# The selection, refused by the library and reported by the command. Exit 2,
# not 1: the value parsed, and what it names is a property of the region.
# ---------------------------------------------------------------------------

nfsu_case("a section the region does not hold" 2
          "outside the region's 0 sections" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6 --section 0)
nfsu_case("a mesh name the region does not hold" 2
          "no prepared mesh of this region is named" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6 --mesh NOSUCHMESH)

# ---------------------------------------------------------------------------
# THE EXIT STATUS, and the report. A run whose reports failed still runs the
# whole body: the scene is empty, the cull is empty and the probe reports
# nothing, and the status must say the reports failed. 3 exactly, not merely
# non-zero: 1 and 2 are this command's other refusals and mean something else.
#
# These are also the only cases that read a completed run's own numbers back,
# and they exist because an option can be parsed, range-checked and then
# dropped on the floor: the value reaches the report or it does not.
# ---------------------------------------------------------------------------

nfsu_case("failed reports exit 3 and still print the report" 3
          "failed to read file size" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the explicit camera reaches the report" 3
          "probe: camera eye 1,2,3 target 4,5,6" "probe: camera framed"
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the camera is reported as explicit" 3 "probe: camera explicit" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the moves reach the report" 3 "moves turn 10,20 fly 1,2,3" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6
          --turn 10 20 --fly 1 2 3)
nfsu_case("no move is reported as none" 3 "moves turn none fly none" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the frame size and the lens reach the report" 3
          "frame 640x480" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6
          --width 640 --height 480)
# The near and far OVERRIDES, which the cases above only ever drive out of
# range. An override that parsed, passed its bound and was then dropped would
# leave the camera at its defaults of 1 and 2000 and say nothing.
nfsu_case("the near and far overrides reach the camera" 3
          "near 2 far 500" "near 1 far 2000"
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6
          --view-near 2 --view-far 500)
# THE MOVED CAMERA IS THE ONE THE FRUSTUM IS BUILT FROM. The camera lines
# report the moved camera whether or not the move reached the frustum, so the
# planes are the only place the difference shows. From 0,0,10 looking at the
# origin the view runs along world -z, which is raw slot 1; a quarter turn to
# the right puts it along world +x, which is raw slot 0. A frustum built from
# the unmoved camera gives the near plane a normal of 0,1,0 instead.
nfsu_case("a turn reaches the frustum and not only the report" 3
          "probe: plane 0 raw normal 1,-0,0" "probe: plane 0 raw normal 0,1,0"
          "${missing}" 0 16 0 16 --eye 0 0 10 --look-at 0 0 0 --turn 90 0)
# And the same for a fly, which moves the planes' constants rather than their
# normals: the near plane through 0,0,10 along -z has d 10, and flying five
# units forward moves it to 5.
nfsu_case("a fly reaches the frustum and not only the report" 3
          "probe: plane 0 raw normal 0,1,0 d 4" "probe: plane 0 raw normal 0,1,0 d 9"
          "${missing}" 0 16 0 16 --eye 0 0 10 --look-at 0 0 0 --fly 5 0 0)
nfsu_case("the six frustum planes are printed" 3 "probe: plane 5 raw normal" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the residency rule reaches the report" 3
          "probe: residency rule no-zone" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6
          --residency no-zone)
nfsu_case("the mask and the scale reach the report" 3
          "mask 0x0000002A screen scale 5" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6
          --mask 42 --screen-scale 5)
nfsu_case("an absent scale is reported as off" 3 "screen scale off" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the draw order reaches the draw list" 3 "order material group" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6
          --order material-group)

# THE TWO LISTING LINES ARE UNCONDITIONAL, and that is a design decision this
# file can therefore check. A listing whose "... N more not listed" trailer
# only appeared for a non-empty listing could not be reached from here at all,
# which is the gap tests/track_render_guards.cmake documents and cannot close.
# Printing the count either way removes the branch instead of excusing it.
nfsu_case("the instance listing reports its own count" 3
          "probe: listed 0 of 0 instances" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the section listing reports its own count" 3
          "probe: listed 0 of 0 sections" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("a limit of zero still reports the count" 3
          "probe: listed 0 of 0 instances" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6 --limit 0)

# And the totals, which are the line a caller reads first.
nfsu_case("the totals are printed" 3
          "probe: instances 0 probed 0 visible 0 culler 0 disagreements 0" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)
nfsu_case("the gate totals are printed" 3
          "probe: rejected by mask 0 box 0 size 0 pruned 0" ""
          "${missing}" 0 16 0 16 --eye 1 2 3 --look-at 4 5 6)


# ---------------------------------------------------------------------------
# AND NOW WITH REAL DATA.
#
# Everything above runs with no input files, which reaches every refusal and
# the whole body of a run that reads nothing - and reaches no line that only
# prints when a region holds an instance. That is the gap
# tests/track_render_guards.cmake documents and could not close: the listing
# limit, the per-instance block and every total that is non-zero only over a
# real region were left to a manual acceptance and to no gate at all.
#
# The fixture nfsu_track_probe_tests already builds in memory is written to a
# file here and driven through the command. It is the SAME bytes the unit
# tests parse, at the same absolute offsets, so the two halves cannot describe
# different regions; the test binary prints the two regions it laid down and
# this reads them back rather than repeating them.
#
# The numbers below are this fixture's and are pinned exactly. That is a
# coupling and it is the point: a change to the fixture that moves an offset
# has to be acknowledged here, and a change to what the command PRINTS cannot
# pass unnoticed.
# ---------------------------------------------------------------------------

if(NOT DEFINED TESTS)
    message(FATAL_ERROR "TESTS must be defined")
endif()

set(fixture "${work}/fixture.bun")
file(REMOVE "${fixture}")
execute_process(
    COMMAND "${TESTS}" --write-fixture "${fixture}"
    OUTPUT_VARIABLE fixture_text
    ERROR_VARIABLE fixture_error
    RESULT_VARIABLE fixture_status)
if(NOT fixture_status EQUAL 0)
    message(FATAL_ERROR
        "the fixture could not be written: ${fixture_status}\n"
        "${fixture_text}${fixture_error}")
endif()
if(NOT EXISTS "${fixture}")
    message(FATAL_ERROR "the fixture writer reported success and wrote no file")
endif()
if(NOT fixture_text MATCHES
   "fixture: geometry ([0-9]+) ([0-9]+) scenery ([0-9]+) ([0-9]+)")
    message(FATAL_ERROR
        "the fixture writer did not name its two regions, and it reads\n"
        "${fixture_text}${fixture_error}")
endif()
set(geo_off "${CMAKE_MATCH_1}")
set(geo_size "${CMAKE_MATCH_2}")
set(obj_off "${CMAKE_MATCH_3}")
set(obj_size "${CMAKE_MATCH_4}")

# One explicit camera for every case below, so that no case depends on the
# automatic framing and two cases never differ by the camera by accident.
set(cam --eye -6 0 30 --look-at -6 0 0 --view-fov 10
        --width 100 --height 100 --view-far 100)

# The same case runner, with the fixture and its two regions in front.
function(nfsu_data_case label code needle banned)
    nfsu_case("${label}" "${code}" "${needle}" "${banned}"
              "${fixture}" ${geo_off} ${geo_size} ${obj_off} ${obj_size}
              ${ARGN})
endfunction()

# The exit status over data that parses. It is 3 rather than 0 because the
# fixture carries one mesh the decoder refuses - which is what makes a
# prepared mesh index differ from its source index below - so this pins the
# "a report raised an issue" arm with a region that otherwise decodes.
nfsu_data_case("a region with a refused mesh exits 3" 3
               "probe: instances 13 probed 13 visible 3 culler 3 disagreements 0"
               "" ${cam})

# THE GATE TOTALS, with a non-zero count in three of the four places. Every
# one of these is zero for a run with no input, so a totals line that printed
# a literal zero passed every case above.
nfsu_data_case("the gate totals carry non-zero counts" 3
               "probe: rejected by mask 0 box 1 size 0 pruned 9" ""
               ${cam})
nfsu_data_case("the placement totals carry non-zero counts" 3
               "probe: placed 10 unplaced 3 groups 16 triangles 20" ""
               ${cam})

# THE REGION BOUNDS, which are measured over the placed geometry and not over
# what the camera sees. With no input they read `none` either way.
nfsu_data_case("the region bounds are measured" 3
               "probe: region bounds: x -21 .. 30.5 y -1 .. 1"
               "probe: region bounds: none" ${cam})

# THE LISTING LIMIT. A listing that ignored it prints every record and still
# prints a count line, so only a count below the total tells the two apart.
nfsu_data_case("the instance listing honours its limit" 3
               "probe: listed 1 of 13 instances" "" ${cam} --limit 1)
nfsu_data_case("a limit above the count lists them all" 3
               "probe: listed 13 of 13 instances" "" ${cam} --limit 20)
nfsu_data_case("a limit of zero lists none of them" 3
               "probe: listed 0 of 13 instances" "  instance 0:0"
               ${cam} --limit 0)
nfsu_data_case("both sections are listed" 3
               "probe: listed 2 of 2 sections" "" ${cam} --limit 0)

# THE CULL OPTIONS REACH THE PROBE. Section 310 modulo 100 is 10, so the
# no-zone rule drops it while section 303 stays - which no run without input
# can show, because it has no section to be resident or not.
nfsu_data_case("the residency rule reaches the probe" 3
               "reason sub-index-out-of-range" "" ${cam}
               --residency no-zone --limit 0)
nfsu_data_case("and only one of the two sections survives it" 3
               "probe: sections 2 valid 2 resident 1" "" ${cam}
               --residency no-zone --limit 0)

# THE SCREEN-SIZE EYE, in the raw slot order. The level below is the retail
# arithmetic over this instance's own radius from this eye; mapping the eye
# with the world frame instead gives another number.
nfsu_data_case("the screen-size eye is mapped to the raw slots" 3
               "size not-applied level 14" "" ${cam}
               --screen-scale 40 --limit 1)

# THE INSTANCE BLOCK. Every line of it is unreachable without a region, and
# each of these pins a different group of fields.
nfsu_data_case("the source offsets are printed" 3
               "offsets: section 8192 instance 8288 object 8632 mesh container 680 mesh record 704 hash record 168"
               "" ${cam} --limit 1)
nfsu_data_case("the object record is printed" 3
               "object: radius 5 flags 0x11223344 flags2 0x55667788 slots 0x00003001 0x00000000"
               "" ${cam} --limit 1)
nfsu_data_case("the stored box is printed in the raw slot order" 3
               "instance: flags 0x00000060 stored raw px -22 .. -18 pz -1 .. 3 py -2 .. 2"
               "" ${cam} --limit 1)
nfsu_data_case("the transform is printed" 3
               "transform: linear 1 0 0 0 1 0 0 0 1 translation -20,0,0" ""
               ${cam} --limit 1)
# The stored box's world image, where slot 1 is negated and its ends
# exchanged: raw pz -1 .. 3 is world z -3 .. 1 and not -1 .. 3.
nfsu_data_case("the stored box reaches the world frame negated" 3
               "stored world: x -22 .. -18 y -2 .. 2 z -3 .. 1 points 2"
               "stored world: x -22 .. -18 y -2 .. 2 z -1 .. 3" ${cam} --limit 1)
nfsu_data_case("the drawn box is measured over the vertices" 3
               "drawn world: x -21 .. -19 y -1 .. 1 z -0 .. -0 points 6" ""
               ${cam} --limit 1)
nfsu_data_case("the tree provenance is printed" 3
               "tree: policy retail-tree node 1 depth 1 carried intersecting pruned by none"
               "" ${cam} --limit 1)
nfsu_data_case("the gate verdicts are printed" 3
               "gates: mask passed bits 0x00000000 box rejected tested outside own box outside"
               "" ${cam} --limit 1)
nfsu_data_case("the placement names the source mesh apart from the prepared one" 3
               "placement: mesh 0 'PANEL' hash 0x00003001 source mesh 1 slot 0 requested 0 fallback no"
               "" ${cam} --limit 1)
nfsu_data_case("the material totals are printed" 3
               "materials: groups 1 drawable 1 triangles 2 resolved 0 unresolved 1 unusable 0 invalid slot 0"
               "" ${cam} --limit 1)
nfsu_data_case("each material group is printed" 3
               "group 0 material 0 record 1136 first 0 count 6 slot 0 hash 0xBBBB0001 binding unresolved drawable yes"
               "" ${cam} --limit 1)
nfsu_data_case("an unresolved group names no texture" 3
               "texture none key slot 255 flags 0x00000000 record bounds min -1,-2,-3 max 1,2,3"
               "" ${cam} --limit 1)

# A PRUNED INSTANCE, which is the line this whole layer exists for, and which
# no run without input can produce.
nfsu_data_case("a pruned instance names the node that stopped it" 3
               "tree: policy retail-tree node 1 depth 1 carried outside pruned by 1"
               "" ${cam} --section 1 --instance 0 --limit 1)
nfsu_data_case("and reaches no gate at all" 3
               "gates: mask not-applied bits 0x00000000 box not-applied" ""
               ${cam} --section 1 --instance 0 --limit 1)
nfsu_data_case("and its own box is measured anyway" 3
               "own box outside" "" ${cam} --section 1 --instance 0 --limit 1)

# An instance that places nothing says why, which is the other end of the
# same line.
nfsu_data_case("an unplaced instance names its reason" 3
               "placement: none reason skipped hash" "" ${cam}
               --section 1 --instance 4 --limit 1)

# THE SELECTIONS, over a region that has something to select from.
nfsu_data_case("a mesh name selects the instances that place it" 3
               "probe: instances 13 probed 6 visible 1 culler 1" ""
               ${cam} --mesh POST --limit 0)
nfsu_data_case("a section selects its own instances" 3
               "probe: instances 13 probed 6 visible 0 culler 0" ""
               ${cam} --section 1 --limit 0)
nfsu_data_case("two instances of one section are selected" 3
               "probe: instances 13 probed 2 visible 2 culler 2" "" ${cam}
               --section 0 --instance 3 --instance 1 --limit 0)

# And the draw order, which changes the draw-list line and nothing above it.
nfsu_data_case("the draw order reaches the draw list over real data" 3
               "draw list: order material group visible 3" "" ${cam}
               --order material-group --limit 0)

file(REMOVE "${fixture}")
file(REMOVE_RECURSE "${work}")
message(STATUS
    "track-probe guards: 95 cases, each asserting the exit status and the "
    "diagnostic: 64 with no input files at all and 31 over a fixture the "
    "unit tests laid down, and none of them wrote anything")
