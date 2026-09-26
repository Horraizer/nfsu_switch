# What `nfsu_probe race-timing` refuses, prints and exits with.
#
# The first cases need no input file at all: the argument count, the event
# number and the option grammar are refused before anything is read. The
# rest drive the command over the fixture files the race-route guards use -
# the race-events track and event tables (nfsu_race_event_tests) and the
# synthetic TRACKB1003 bundle (nfsu_race_route_tests), a circle of 16 nodes,
# radius 100, with its start line at 90 degrees, split 1 at 180 and split 2
# at 270 - written into a directory of this script's own, so that the two
# guard tests can run at once (issue #122).
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

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_race_timing_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(failures 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case: the exact exit status, texts the combined output must contain,
# texts it must not, then the command line after "race-timing". The
# standard output with CR LF folded to LF is left in ${last_stdout}.
function(nfsu_case label code needles banned)
    execute_process(
        COMMAND "${PROBE}" race-timing ${ARGN}
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

# The standard output of the last case, pinned by length and SHA-256.
function(nfsu_pin label expected_length expected_hash)
    string(SHA256 hash "${last_stdout}")
    string(LENGTH "${last_stdout}" length)
    if(NOT hash STREQUAL expected_hash OR NOT length EQUAL expected_length)
        nfsu_fail("${label}: the output is ${length} bytes with SHA-256 ${hash}\; expected ${expected_length} bytes with ${expected_hash}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

set(usage_line "nfsu_probe race-timing <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>")

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
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" "one" --drive forward)

nfsu_case("no replay" 1
          "error: race-timing replays a path file or a drive; give --path or --drive"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1)

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
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --path "${work}/p.txt" --skip 1 2)

nfsu_case("a repeated option" 1
          "error: unexpected or repeated argument '--drive'"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --drive forward --drive backward)

nfsu_case("a skip that is not two numbers" 1
          "error: --skip takes two node numbers"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc" 1 --drive forward --skip 1 x)

nfsu_case("three missing files" 3
          "globalb: missing.bun;frontb: missing.lzc;trackb: TRACKB1003.lzc;track table issue at offset 0: cannot size the file;event table issue at offset 0: cannot size the file;trackb issue at offset 0: cannot size the file"
          "event 1 type;clock:"
          "${work}/missing.bun" "${work}/missing.lzc" "${work}/TRACKB1003.lzc" 1 --drive forward)

# ---------------------------------------------------------------------------
# The fixture
# ---------------------------------------------------------------------------

set(globalb "${work}/fixture_globalb.bun")
set(frontb "${work}/fixture_frontb.lzc")
set(broken "${work}/fixture_broken.bun")
set(trackb "${work}/TRACKB1003.lzc")
file(REMOVE "${globalb}" "${frontb}" "${broken}" "${trackb}")
execute_process(
    COMMAND "${EVENT_TESTS}" --write-fixture "${globalb}" "${frontb}" "${broken}"
    OUTPUT_VARIABLE event_text
    ERROR_VARIABLE event_error
    RESULT_VARIABLE event_status)
execute_process(
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

# The forward drive, two laps. The route replay crosses the line at its step
# 36, completes lap 1 at 1300 and finishes at 2564 (the race-route guards);
# held through go at step 162, the race sees each 162 steps later. The first
# crossing of the line is not timed: lap 1 runs from go.
nfsu_case("event 1: the forward drive" 0
          "event 1 type circuit (0) track 1003 laps 2 direction forward\n;route: laps 2 direction forward boundaries 3 start 0 finish 0 path-length 628.1205\n;clock: 4000 ticks a second (0x006CC9FC), the world timer built at 4000 (0x0041F7DA), 60 steps a second of 67 ticks (0x00585CD5..0x00585E90), times rounded to 1/100 s (0x00586690)\n;countdown: three steps of 0.9 s (0x006B79E0), go at 2.6999998 s (0x00434D9F)\n;step 0 tick 4000 stage countdown\n;step 0 tick 4000 countdown 3\n;step 54 tick 7618 countdown 2\n;step 108 tick 11236 countdown 1\n;step 162 tick 14854 stage racing\n;step 162 tick 14854 go race-start 14840\n;step 162 tick 14854 countdown go\n;step 198 tick 17266 untimed boundary 0 ForwardStartLine lap 0 (no new checkpoint)\n;step 216 tick 18472 countdown off\n;step 1462 tick 101954 lap 1 boundary 0 ForwardStartLine;step 2726 tick 186642 lap 2 boundary 0 ForwardStartLine;step 2726 tick 186642 finished position 1;result: racer 0 finished position 1 laps 2 of 2;race: outcome finished stage racing steps 2727 go-step 162 race-start 14840"
          "missed;wrong-way-on;crossed-backward;error"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward)
nfsu_pin("event 1 forward drive" 2339 "38f9c8822e653bb5b447664ec11b470f6973a4db4af2c033282b78ee9cf26092")

# The same number of nodes the other way round: the car goes the wrong way
# from its first move, the lap counter falls, and the race never finishes.
nfsu_case("event 1: the backward drive" 0
          "step 163 tick 14921 wrong-way-on\n;route crossed-backward boundary 2 SplitTimeBoundary2 lap -1;route crossed-backward boundary 0 ForwardStartLine lap -2;replay: the positions ran out after step;result: racer 0 not-finished position 0 laps 0 of 2 time none best-lap 0:00.00 (2147483647);race: outcome running stage racing;wrong-way yes"
          "lap 1 boundary;finished position 1;split boundary"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive backward)
# Review comment 3941 (F2) re-pinned this one: the empty best lap now prints
# as 0x00586580 prints it, a zero time, where the first round printed dashes.
nfsu_pin("event 1 backward drive" 2063 "617292f291183fbfd6da8b17a299ad9fa314afc0443de83bc05832099a3444b3")

# Jumping from node 7 (43.75 percent) to node 13 (81.25): split 1 at 50 is
# missed, so it has no time on that lap, and the lap still counts.
nfsu_case("event 1: a skipped split" 0
          "jumping from node 7 to node 13 past 5;route missed-forward boundary 1 SplitTimeBoundary1 lap 0;lap 1 boundary 0 ForwardStartLine;finished position 1"
          "crossed-backward"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward --skip 7 13)
nfsu_pin("event 1 skipped split" 2357 "5bb730aef23bea3c4b09da58f2b5960b07d32b66a29772a57f73d688098eb78a")

# Jumping from node 3 (18.75) to node 9 (56.25): the line at 25 is missed
# and that lap is lost; the drive runs out one lap short.
nfsu_case("event 1: a skipped finish line" 0
          "route missed-forward boundary 0 ForwardStartLine lap 0;lap 1 boundary 0 ForwardStartLine;replay: the positions ran out after step;result: racer 0 not-finished position 0 laps 1 of 2"
          "finished position 1"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward --skip 3 9)
nfsu_pin("event 1 skipped finish line" 2314 "38ee307e81c968775cf096f2a898f8fd7f872bff492d346f14f4ae25c2130e56")

nfsu_case("an event the table does not hold" 3
          "error: the event table holds no event 99"
          "clock:"
          "${globalb}" "${frontb}" "${trackb}" 99 --drive forward)

nfsu_case("the bundle of another track" 3
          "error: event 2 loads TRACKB1102.LZC, not TRACKB1003.lzc"
          "clock:"
          "${globalb}" "${frontb}" "${trackb}" 2 --drive forward)

nfsu_case("a skip between nodes the path does not hold" 3
          "error: a skip needs two different nodes of the path, 0 to 15"
          "replay:"
          "${globalb}" "${frontb}" "${trackb}" 1 --drive forward --skip 3 16)

# ---------------------------------------------------------------------------
# Path files
# ---------------------------------------------------------------------------

# A square round the circle, ten units a step, from the grid side: the car
# holds its first line until go, then two laps and a little more.
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
          "replay: path square.txt;go race-start 14840;lap 1 boundary 0 ForwardStartLine;lap 2 boundary 0 ForwardStartLine;finished position 1"
          "missed;wrong-way-on"
          "${globalb}" "${frontb}" "${trackb}" 1 --path "${square}")

set(bad "${work}/bad_one_number.txt")
file(WRITE "${bad}" "0 0\n5\n")
nfsu_case("a malformed path file" 3
          "error: path file line 2: expected two numbers, found 1"
          "replay:"
          "${globalb}" "${frontb}" "${trackb}" 1 --path "${bad}")

nfsu_case("a path file that does not exist" 3
          "error: cannot size the path file"
          "replay:"
          "${globalb}" "${frontb}" "${trackb}" 1 --path "${work}/missing.txt")

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} race-timing guard case(s) failed")
endif()
message(STATUS "race-timing guards: every case passed")
