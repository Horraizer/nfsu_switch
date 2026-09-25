# What `nfsu_probe race-events` refuses, prints and exits with.
#
# The first three cases need NO INPUT FILES AT ALL: the argument count and
# two paths that do not exist. The rest drive the command over three small
# files nfsu_race_event_tests writes on request - a track table, a
# JDLZ-compressed event table, and an uncompressed event table one of whose
# races names a track the track table does not hold - which is the only way
# to reach the listing, the Underground order lines and the cross-table
# refusal without retail data. The fixture is the same bytes the unit tests
# parse, so the offsets pinned below are the ones those tests assert.
#
# A script rather than add_test properties for the reason
# tests/track_render_guards.cmake gives: PASS_REGULAR_EXPRESSION makes CTest
# ignore the exit status, and the status is half of what is pinned here.
# Exit 3 with a listing and exit 0 with the same listing differ in whether
# the order lines were earned.
#
# Run from the build directory. PROBE is the built program and TESTS the
# built nfsu_race_event_tests.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE OR NOT DEFINED TESTS)
    message(FATAL_ERROR "PROBE and TESTS must both be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_race_events_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(failures 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case. `code` is the exact exit status required. `needles` is a list of
# texts the combined output must each contain literally, and `banned` a list
# it must not contain; both are semicolon-separated CMake lists, so no needle
# here contains a semicolon. Everything after them is the command line after
# "race-events". The standard output, with CR LF folded to LF, is left in
# ${last_stdout} for a case that pins more than a line.
function(nfsu_case label code needles banned)
    execute_process(
        COMMAND "${PROBE}" race-events ${ARGN}
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

set(usage_line "nfsu_probe race-events <GLOBALB.BUN> <FrontB.lzc>")

# ---------------------------------------------------------------------------
# No input files
# ---------------------------------------------------------------------------

nfsu_case("one path is a usage error" 1
          "${usage_line}"
          "globalb:"
          "${work}/only.bun")

nfsu_case("three paths are a usage error" 1
          "${usage_line}"
          "globalb:"
          "${work}/a.bun" "${work}/b.lzc" "${work}/c.lzc")

# Both files are read and both refusals reported: a run that stopped at the
# first would leave the second file's problem for the next run to find.
nfsu_case("two missing files" 3
          "globalb: missing_globalb.bun;frontb: missing_frontb.lzc;track table issue at offset 0: cannot size the file;event table issue at offset 0: cannot size the file"
          "track table: chunk;event table: container;race 1:;winning event"
          "${work}/missing_globalb.bun" "${work}/missing_frontb.lzc")

# ---------------------------------------------------------------------------
# The fixture
# ---------------------------------------------------------------------------

set(globalb "${work}/fixture_globalb.bun")
set(frontb "${work}/fixture_frontb.lzc")
set(broken "${work}/fixture_broken.bun")
file(REMOVE "${globalb}" "${frontb}" "${broken}")
execute_process(
    COMMAND "${TESTS}" --write-fixture "${globalb}" "${frontb}" "${broken}"
    OUTPUT_VARIABLE fixture_text
    ERROR_VARIABLE fixture_error
    RESULT_VARIABLE fixture_status)
if(NOT fixture_status EQUAL 0 OR NOT EXISTS "${globalb}" OR NOT EXISTS "${frontb}"
   OR NOT EXISTS "${broken}")
    message(FATAL_ERROR
        "the fixture could not be written: ${fixture_status}\n"
        "${fixture_text}${fixture_error}")
endif()

# Every number here is the fixture's: the unit tests build the same bytes
# and assert the same offsets. The records are deliberately out of number
# order (5 before 3), one event is a three-race tournament, one is any-of,
# and one is kind 8, which the availability rule never offers.
nfsu_case("the fixture pair" 0
          "globalb bytes: stored 1068 image 1068 jdlz 0\n;frontb bytes: stored 3514 image 3108 jdlz 1\n;track table: chunk offset 32 records at 40 count 4\n;track 1210 @544 number @652 region L1RB @608 lap-skip 1 @649 number-copy 1217 @654 mode-mask 0x00000002 @660 stream STREAML1RB.BUN pack STREAML1RB_1210.BUN textures TEX1210TRACK.BIN bundle TRACKB1210.LZC\n;event table: container offset 48 chunk offset 56 records at 64 count 9\n;event 6 @1724 kind 1 @1728 tournament type circuit (0) @1732 races 3 @1876 prerequisites all-of @1892 count 3 @1894 [1 2 5] @1880\n;  race 1 @1848 track 1003 laps 2 @1850 direction reverse (1) @1851 region L1RA stream STREAML1RA.BUN pack STREAML1RA_1003.BUN textures TEX1003TRACK.BIN\n;event 7 @2056 kind 2 @2060 type time-trial (4) @2064 races 1 @2208 prerequisites any-of @2224 count 2 @2226 [3 6] @2212\n;events: 9 tournaments 1 race slots 11\n;types: circuit 4 drag 1 sprint 1 drift 1 time-trial 1 lap-knockout 1 unknown 0\n;new career enters: event 0 (0x004B383E)\n;winning event 0 opens: 1 2 5\n;race 1: event 1 type circuit races 1 track 1003 laps 2 direction forward region L1RA stream STREAML1RA.BUN pack STREAML1RA_1003.BUN textures TEX1003TRACK.BIN\n;first circuit from race 1: event 1 type circuit races 1 track 1003 laps 2 direction forward region L1RA stream STREAML1RA.BUN pack STREAML1RA_1003.BUN textures TEX1003TRACK.BIN\n;circuit race slots by stream: STREAML1RA.BUN 5 STREAML1RB.BUN 1\n"
          "issue"
          "${globalb}" "${frontb}")

# The whole listing, pinned: a line reworded, reordered or dropped anywhere
# changes this, where the needles above each watch one line.
string(SHA256 listing_hash "${last_stdout}")
string(LENGTH "${last_stdout}" listing_length)
set(expected_hash "c3dfd16daaa827822b60ad2109c86bbb6618771934e06a1dd42d43010c74a899")
set(expected_length 4419)
if(NOT listing_hash STREQUAL expected_hash OR NOT listing_length EQUAL expected_length)
    nfsu_fail("the fixture listing is ${listing_length} bytes with SHA-256 ${listing_hash}\; expected ${expected_length} bytes with ${expected_hash}")
endif()

# The game would dereference a null track record for this race
# (0x004ABC58), so the order lines are withheld and the run fails, but the
# listing still shows the slot and says the track is not listed.
nfsu_case("a race names a track the track table does not hold" 3
          "event table issue at offset 1848: event 6 race 1 names track 1999, which is not in the track table;  race 1 @1848 track 1999 laps 2 @1850 direction reverse (1) @1851 track-not-listed\n;frontb bytes: stored 3108 image 3108 jdlz 0\n"
          "underground order;winning event;race 1: event"
          "${globalb}" "${broken}")

# The two files in the wrong order: each table is missing from the file it
# is looked for in, and each says so by its own chunk id.
nfsu_case("the two files swapped" 3
          "track table issue at offset 0: expected exactly one chunk 0x00034201, found 0;event table issue at offset 0: expected exactly one chunk 0x80034A00, found 0"
          "track table: chunk;event table: container;race 1:"
          "${frontb}" "${globalb}")

# A valid event table is still listed when the track table is missing, but
# nothing that needs a track - the stream names, the order - is printed.
nfsu_case("the event table without its track table" 3
          "event table: container offset 48 chunk offset 56 records at 64 count 9\n;  race 0 @184 track 1003 laps 2 @186 direction forward (0) @187 track-not-listed\n"
          "region L1RA;race 1:;winning event"
          "${work}/missing_globalb.bun" "${frontb}")

# --write-fixture never replaces a file, the rule the track-probe fixture
# writer keeps; every case above removed its files first, which is exactly
# what would hide a writer that did replace one.
set(protected "${work}/protected.bin")
file(WRITE "${protected}" "keep-me-untouched")
execute_process(
    COMMAND "${TESTS}" --write-fixture "${work}/new_a.bin" "${protected}" "${work}/new_b.bin"
    OUTPUT_VARIABLE protect_text
    ERROR_VARIABLE protect_error
    RESULT_VARIABLE protect_status)
file(READ "${protected}" protected_after)
if(protect_status EQUAL 0 OR NOT protected_after STREQUAL "keep-me-untouched"
   OR EXISTS "${work}/new_a.bin" OR EXISTS "${work}/new_b.bin")
    nfsu_fail("--write-fixture over an existing file must refuse and write "
              "nothing: status ${protect_status}\n${protect_text}${protect_error}")
endif()
string(FIND "${protect_error}" "already exists; this command never replaces a file" named)
if(named LESS 0)
    nfsu_fail("--write-fixture over an existing file must say why:\n${protect_error}")
endif()

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} race-events guard case(s) failed")
endif()
message(STATUS "race-events guards: every case passed")
