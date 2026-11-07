# Decode UTF-8 path diagnostics explicitly instead of using the Windows
# console code page selected by AUTO.
# What `nfsu_probe race-route` refuses, prints and exits with.
#
# The first cases need no input file at all: the argument count, the event
# number and the option grammar are refused before anything is read. The
# rest drive the command over three fixture files the unit tests write on
# request - the race-events track and event tables (nfsu_race_event_tests)
# and a synthetic TRACKB1003 bundle (nfsu_race_route_tests), a circle of 16
# nodes, radius 100, with its start line at 90 degrees, split 1 at 180 and
# split 2 at 270 - which is the only way to reach the route listing and a
# replay without retail data. The fixture is the same bytes the unit tests
# parse, so the offsets pinned below are the ones those tests assert.
#
# A script rather than add_test properties for the reason
# tests/race_events_guards.cmake gives: the exit status is half of what is
# pinned.
#
# Run from the build directory. PROBE is the built program, TESTS the built
# nfsu_race_route_tests and EVENT_TESTS the built nfsu_race_event_tests.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE OR NOT DEFINED TESTS OR NOT DEFINED EVENT_TESTS)
    message(FATAL_ERROR "PROBE, TESTS and EVENT_TESTS must all be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_race_route_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(failures 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case, as tests/race_events_guards.cmake runs them: the exact exit
# status, texts the combined output must contain, texts it must not, then
# the command line after "race-route". The standard output with CR LF folded
# to LF is left in ${last_stdout}.
function(nfsu_case label code needles banned)
    execute_process(
        ENCODING UTF-8
        COMMAND "${PROBE}" race-route ${ARGN}
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    string(REPLACE "\r\n" "\n" stdout_text "${stdout_text}")
    string(REPLACE "\r\n" "\n" stderr_text "${stderr_text}")
    set(last_stdout "${stdout_text}" PARENT_SCOPE)
    set(text "${stdout_text}${stderr_text}")
    if(text MATCHES "terminate called")
        nfsu_fail("${label}: the run terminated on an exception:\n${text}")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    if(NOT status MATCHES "^[0-9]+$" OR status GREATER 128)
        nfsu_fail("${label}: exit status ${status} is a crash, not a refusal")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    if(NOT status EQUAL ${code})
        nfsu_fail("${label}: exit status ${status}, expected ${code}\n${text}")
        set(failures "${failures}" PARENT_SCOPE)
        return()
    endif()
    foreach(needle IN LISTS needles)
        string(FIND "${text}" "${needle}" found)
        if(found LESS 0)
            nfsu_fail("${label}: expected the output to contain\n  ${needle}\nand it reads\n${text}")
        endif()
    endforeach()
    foreach(needle IN LISTS banned)
        string(FIND "${text}" "${needle}" found)
        if(NOT found LESS 0)
            nfsu_fail("${label}: the output must NOT contain\n  ${needle}\nand it reads\n${text}")
        endif()
    endforeach()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# The standard output of the last case, pinned by length and SHA-256: a line
# reworded, reordered or dropped anywhere changes it, where the needles each
# watch one line.
function(nfsu_pin label expected_length expected_hash)
    string(SHA256 hash "${last_stdout}")
    string(LENGTH "${last_stdout}" length)
    if(NOT hash STREQUAL expected_hash OR NOT length EQUAL expected_length)
        nfsu_fail("${label}: the output is ${length} bytes with SHA-256 ${hash}\; expected ${expected_length} bytes with ${expected_hash}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

set(usage_line "nfsu_probe race-route <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>")

# ---------------------------------------------------------------------------
# No input files
# ---------------------------------------------------------------------------

nfsu_case("three paths and no event are a usage error" 1
          "${usage_line}"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc")

nfsu_case("an event that is not a number" 1
          "error: the event must be a number"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" "one")

nfsu_case("a drive that is neither way" 1
          "error: --drive is forward or backward"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --drive sideways)

nfsu_case("a path and a drive at once" 1
          "error: --path and --drive are two replays; give one"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --path "${work}/p.txt" --drive forward)

nfsu_case("a skip without a drive" 1
          "error: --skip needs --drive"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --skip 1 2)

nfsu_case("a repeated option" 1
          "error: unexpected or repeated argument '--drive'"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --drive forward --drive backward)

nfsu_case("a skip that is not two numbers" 1
          "error: --skip takes two node numbers"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --drive forward --skip 1 x)

# All three files are read and every refusal reported, then the run stops.
nfsu_case("three missing files" 3
          "globalb: missing.bun;frontb: missing.lzc;trackb: TRACKB1003.lzc;track table issue at offset 0: cannot size the file;event table issue at offset 0: cannot size the file;trackb issue at offset 0: cannot size the file"
          "event 1 @;boundary"
          "${work}/missing.bun" "${work}/missing.lzc" "${work}/TRACKB1003.lzc" 1)

# ---------------------------------------------------------------------------
# The fixture
# ---------------------------------------------------------------------------

set(globalb "${work}/fixture_globalb.bun")
set(frontb "${work}/fixture_frontb.lzc")
set(broken "${work}/fixture_broken.bun")
set(trackb "${work}/TRACKB1003.lzc")
file(REMOVE "${globalb}" "${frontb}" "${broken}" "${trackb}")
execute_process(
    ENCODING UTF-8
    COMMAND "${EVENT_TESTS}" --write-fixture "${globalb}" "${frontb}" "${broken}"
    OUTPUT_VARIABLE event_text
    ERROR_VARIABLE event_error
    RESULT_VARIABLE event_status)
execute_process(
    ENCODING UTF-8
    COMMAND "${TESTS}" --write-fixture "${trackb}"
    OUTPUT_VARIABLE route_text
    ERROR_VARIABLE route_error
    RESULT_VARIABLE route_status)
if(NOT event_status EQUAL 0 OR NOT route_status EQUAL 0 OR NOT EXISTS "${globalb}"
   OR NOT EXISTS "${frontb}" OR NOT EXISTS "${trackb}")
    message(FATAL_ERROR
        "the fixture could not be written: ${event_status} ${route_status}\n"
        "${event_text}${event_error}${route_text}${route_error}")
endif()

# The listing alone: no replay line, and every boundary at its marker.
nfsu_case("event 1: the route listing" 0
          "trackb bytes: stored 932 image 932 jdlz 0\n;event 1 @396 type circuit (0) race 0 of 1 @516 track 1003 laps 2 @518 direction forward (0) @519\n;track 1003 @40 lap-skip 0 @145 bundle TRACKB1003.LZC\n;race: laps 2 (0x004342A4) direction forward point-to-point 0\n;path container @336 records @344 nodes @392 zones @720\n;path 0 @352 closed 1 @353 nodes 0..16 count 16 @358 length 628.1205 @364 scale 1 @368 offset 0 @360\n;node 4 @480 x 0 y 100 distance 157.07964 @496 percent 24.999996\n;markers: chunk @0 records @16 count 6\n;marker 1 @64 ForwardStartLine 0x6A56B90A param 0 x 0 y 100 z 7.5 angle 0x00000000\n;zone 0 @728 RESET (2) direction-filter -1 box -10 -10 10 10 vertices 4\n;boundaries (0x00434810): 3 start 0 finish 0\n;boundary 0 ForwardStartLine marker 1 @64 percent 24.999996 distance 157.03009 start finish\n;boundary 1 SplitTimeBoundary1 marker 4 @208 percent 49.99998;boundary 2 SplitTimeBoundary2 marker 5 @256 percent 74.999954"
          "replay:;issue;error"
          "${globalb}" "${frontb}" "${trackb}" 1)
# Round 2 of the review re-pinned both: the retail order of 0x005AC449's
# additions moved the loop's length and every percent in their last digit.
nfsu_pin("event 1 listing" 2914 "f0ce71049c4a229659b5251c5cf5038cd863b683cfd5916bec6da04f698ceed5")

# The built-in drive, two laps: the line at the start, then twice more.
nfsu_case("event 1: the forward drive finishes two laps" 0
          "replay: drive forward from marker 0 through 34 nodes, 2643 positions, 2642 steps of 1/60 s\n;step 36 crossed-forward boundary 0 ForwardStartLine lap 0;step 1300 lap-complete lap 1;step 2564 finished lap 2;result: steps 2564 laps 2 of 2 finished yes at step 2564 lap-counter 2 highest 2 checkpoint 10 highest-checkpoint 10 next-boundary 1 wrong-way no\n"
          "missed;wrong-way-on;crossed-backward"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward)
nfsu_pin("event 1 forward drive" 3901 "17cae2bc3213823a9226c49789deab452f8e482a087dd7a0794ec003377521a1")

# The same number of nodes the other way round: every boundary is crossed
# backward, the lap counter falls, and the car is going the wrong way.
nfsu_case("event 1: the backward drive" 0
          "step 1 wrong-way-on lap -1;crossed-backward boundary 2 SplitTimeBoundary2 lap -1;crossed-backward boundary 0 ForwardStartLine lap -2;finished no lap-counter -3 highest 0;wrong-way yes\n"
          "crossed-forward;lap-complete;finished yes"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive backward)

# Jumping from node 7 (43.75 percent) to node 13 (81.25): split 1 at 50 is
# passed 31.25 beyond the window and missed, split 2 at 75 is crossed, and
# the lap still counts at the line.
nfsu_case("event 1: a skipped split" 0
          "jumping from node 7 to node 13 past 5;missed-forward boundary 1 SplitTimeBoundary1 lap 0;finished yes"
          "crossed-backward"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward --skip 7 13)

# Jumping from node 3 (18.75) to node 9 (56.25): the line at 25 is passed
# 31.25 beyond the window, and that lap is lost.
nfsu_case("event 1: a skipped finish line" 0
          "missed-forward boundary 0 ForwardStartLine lap 0;finished no"
          "finished yes"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward --skip 3 9)

# Event 5 runs 1003 in reverse for three laps: the backward start line and
# the splits in the reverse order.
nfsu_case("event 5: a reverse race" 0
          "race: laps 3 (0x004342A4) direction reverse point-to-point 0\n;boundary 0 BackwardStartLine marker 3 @160;boundary 1 SplitTimeBoundary2 marker 5;boundary 2 SplitTimeBoundary1 marker 4;replay: drive forward from marker 2;finished yes"
          "wrong-way-on"
          "${globalb}" "${frontb}" "${trackb}" 5 --drive forward)

nfsu_case("an event the table does not hold" 3
          "error: the event table holds no event 99"
          "boundary"
          "${globalb}" "${frontb}" "${trackb}" 99)

# Event 2 runs on 1102, and the bundle named is 1003's.
nfsu_case("the bundle of another track" 3
          "error: event 2 loads TRACKB1102.LZC, not TRACKB1003.lzc"
          "boundary"
          "${globalb}" "${frontb}" "${trackb}" 2)

nfsu_case("a skip between nodes the path does not hold" 3
          "error: a skip needs two different nodes of the path, 0 to 15"
          "replay:"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward --skip 3 16)

# ---------------------------------------------------------------------------
# Absurd numbers
# ---------------------------------------------------------------------------

# Three bundles the first round's parser accepted and whose drive never
# returned (review comment 3575, F1): a path length of 1e-30 and a scale of
# 3e38, which the route refuses because the loop no longer measures 0 to
# 100 percent, and a node at x = 1e30, which the parser refuses. Each must
# exit 3 promptly with its reason, and print no replay. The test's TIMEOUT
# is what a regression to the first round would run into.
foreach(variant tiny-length huge-scale far-node)
    set(absurd "${work}/absurd_${variant}/TRACKB1003.lzc")
    file(MAKE_DIRECTORY "${work}/absurd_${variant}")
    file(REMOVE "${absurd}")
    execute_process(
        ENCODING UTF-8
        COMMAND "${TESTS}" --write-fixture "${absurd}" ${variant}
        RESULT_VARIABLE absurd_status)
    if(NOT absurd_status EQUAL 0 OR NOT EXISTS "${absurd}")
        message(FATAL_ERROR "the ${variant} fixture could not be written: ${absurd_status}")
    endif()
    if(variant STREQUAL "far-node")
        set(reason "trackb issue at offset 540: node position 1e+30, 38.2683 is further than 1e+06 from the origin on an axis")
    else()
        set(reason "measure its own loop of 628.12 from")
    endif()
    nfsu_case("absurd ${variant}" 3
              "${reason}"
              "replay:;boundary 0"
              "${globalb}" "${frontb}" "${absurd}" 1 --drive forward)
endforeach()

# ---------------------------------------------------------------------------
# Path files
# ---------------------------------------------------------------------------

# A square round the circle, ten units a step, counter-clockwise from the
# grid side: two laps and a little more. Comments, blank lines, tabs and
# padding are all read, and so are CR LF line ends: file(WRITE) writes every
# LF as CR LF on Windows, and the replay reads the same either way.
set(square "${work}/square.txt")
set(lines "# a square round the circle\n\n")
foreach(y RANGE 20 100 10)
    string(APPEND lines "100 ${y}\n")
endforeach()
foreach(lap RANGE 1 2)
    foreach(x RANGE 90 -100 -10)
        string(APPEND lines "${x}\t100\n")
    endforeach()
    foreach(y RANGE 90 -100 -10)
        string(APPEND lines "-100 ${y}\n")
    endforeach()
    foreach(x RANGE -90 100 10)
        string(APPEND lines "  ${x} -100  \n")
    endforeach()
    foreach(y RANGE -90 100 10)
        string(APPEND lines "100 ${y}\n")
    endforeach()
endforeach()
foreach(x RANGE 90 -50 -10)
    string(APPEND lines "${x} 100\n")
endforeach()
file(WRITE "${square}" "${lines}")
nfsu_case("a path file round a square" 0
          "replay: path square.txt;crossed-forward boundary 0 ForwardStartLine lap 0;lap-complete lap 1;finished lap 2;finished yes"
          "missed;wrong-way-on"
          "${globalb}" "${frontb}" "${trackb}" 1 --path "${square}")

# Every malformed line is refused with its number, and nothing is replayed.
set(bad_cases
    "one-number|0 0\n5\n|path file line 2: expected two numbers, found 1"
    "three-numbers|0 0 0\n|path file line 1: expected two numbers and nothing after them"
    "word|0 zero\n|path file line 1: 'zero' is not a finite number"
    "nan|nan 0\n|path file line 1: 'nan' is not a finite number"
    "huge|1e99 0\n|path file line 1: '1e99' is not a finite number"
    "comma|1,5 0\n|path file line 1: '1,5' is not a finite number"
    "empty|# nothing\n\n|the path file holds no position")
foreach(entry IN LISTS bad_cases)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    list(GET parts 1 body)
    list(GET parts 2 message)
    set(bad "${work}/bad_${name}.txt")
    file(WRITE "${bad}" "${body}")
    nfsu_case("a malformed path file: ${name}" 3
              "error: ${message}"
              "replay:"
              "${globalb}" "${frontb}" "${trackb}" 1 --path "${bad}")
endforeach()

nfsu_case("a path file that does not exist" 3
          "error: cannot size the path file"
          "replay:"
          "${globalb}" "${frontb}" "${trackb}" 1 --path "${work}/missing.txt")

# --write-fixture never replaces a file.
set(protected "${work}/protected.lzc")
file(WRITE "${protected}" "keep-me-untouched")
execute_process(
    ENCODING UTF-8
    COMMAND "${TESTS}" --write-fixture "${protected}"
    OUTPUT_VARIABLE protect_text
    ERROR_VARIABLE protect_error
    RESULT_VARIABLE protect_status)
file(READ "${protected}" protected_after)
if(protect_status EQUAL 0 OR NOT protected_after STREQUAL "keep-me-untouched")
    nfsu_fail("--write-fixture over an existing file must refuse and write "
              "nothing: status ${protect_status}\n${protect_text}${protect_error}")
endif()

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} race-route guard case(s) failed")
endif()
message(STATUS "race-route guards: every case passed")
