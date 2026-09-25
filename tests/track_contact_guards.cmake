# What `nfsu_probe track-contact` refuses, prints and exits with.
#
# The first cases need NO INPUT FILE: the argument count, coordinates that
# are not finite numbers, and a path that does not exist. The rest drive the
# command over three files nfsu_track_contact_tests writes on request - the
# four-section collision image its own cases parse, the same image packed
# with JDLZ, and the image cut three bytes short - which is the only way to
# reach the load summary, the memory lines and every query verdict without
# retail data. The numbers pinned below are the fixture's, and the unit
# tests assert the same ones over the same bytes.
#
# A script rather than add_test properties for the reason
# tests/track_render_guards.cmake gives: PASS_REGULAR_EXPRESSION makes CTest
# ignore the exit status, and the status is half of what is pinned here.
# Exit 5 and exit 0 differ by whether a query ran at all.
#
# One exit no case here reaches: 4, the Switch plan refusing the world. The
# fixture's 4696 resident bytes cannot fill the 64 MiB persistent
# placeholder, and the refusal path itself is covered by the
# test_memory_plan cases of nfsu_track_contact_tests.
#
# Run from the build directory. PROBE is the built program and TESTS the
# built nfsu_track_contact_tests.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE OR NOT DEFINED TESTS)
    message(FATAL_ERROR "PROBE and TESTS must both be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_track_contact_guards")
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
# it must not contain; both are semicolon-separated CMake lists. Everything
# after them is the command line after "track-contact". The standard output,
# with CR LF folded to LF, is left in ${last_stdout}.
function(nfsu_case label code needles banned)
    execute_process(
        COMMAND "${PROBE}" track-contact ${ARGN}
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

set(usage_line "nfsu_probe track-contact <TRACKB####.lzc> [px pz | px pz py px pz py]")

# ---------------------------------------------------------------------------
# No input file
# ---------------------------------------------------------------------------

# Every refusal below happens before the file is opened: none of them may
# print a load line.
nfsu_case("one coordinate is a usage error" 1
          "${usage_line}"
          "file:"
          "${work}/missing.lzc" "1")
nfsu_case("three coordinates are a usage error" 1
          "${usage_line}"
          "file:"
          "${work}/missing.lzc" "1" "2" "3")
nfsu_case("five coordinates are a usage error" 1
          "${usage_line}"
          "file:"
          "${work}/missing.lzc" "1" "2" "3" "4" "5")
nfsu_case("a word is not a coordinate" 1
          "error: every coordinate must be a finite number, got 'north'"
          "file:"
          "${work}/missing.lzc" "north" "2")
nfsu_case("a number with text after it is not a coordinate" 1
          "error: every coordinate must be a finite number, got '2m'"
          "file:"
          "${work}/missing.lzc" "1" "2m")
nfsu_case("a number past float range is not a coordinate" 1
          "error: every coordinate must be a finite number, got '1e39'"
          "file:"
          "${work}/missing.lzc" "1e39" "2")
nfsu_case("not-a-number is not a coordinate" 1
          "error: every coordinate must be a finite number, got 'nan'"
          "file:"
          "${work}/missing.lzc" "1" "2" "3" "4" "5" "nan")
nfsu_case("a missing file is a refused collision" 3
          "file: ${work}/missing.lzc;collision: refused polygons 0 cells 0 indices 0 nodes 0;collision issue at 0: cannot read the size of"
          "memory resident;switch plan;probe"
          "${work}/missing.lzc")

# ---------------------------------------------------------------------------
# The fixture
# ---------------------------------------------------------------------------

set(image "${work}/fixture_image.bin")
set(packed "${work}/fixture_packed.lzc")
set(broken "${work}/fixture_broken.bin")
file(REMOVE "${image}" "${packed}" "${broken}")
execute_process(
    COMMAND "${TESTS}" --write-fixture "${image}" "${packed}" "${broken}"
    OUTPUT_VARIABLE fixture_text
    ERROR_VARIABLE fixture_error
    RESULT_VARIABLE fixture_status)
if(NOT fixture_status EQUAL 0 OR NOT EXISTS "${image}" OR NOT EXISTS "${packed}"
   OR NOT EXISTS "${broken}")
    message(FATAL_ERROR
        "the fixture could not be written: ${fixture_status}\n"
        "${fixture_text}${fixture_error}")
endif()

# The four sections as one image, the whole summary: counts, triangle kinds,
# the frame, every memory figure, and the Switch plan.
set(summary
    "collision: valid polygons 13 cells 11 indices 20 nodes 52\n"
    "bounds: px -830 .. 1682 pz -411 .. 734 py -127 .. 127\n"
    "triangles: road 15 wall 10 degenerate 1 (road when |normal py| >= 0.5)\n"
    "frame: raw slot order (px, pz, py), py up, world units"
    "render x = px, y = py, z = -pz\n"
    "memory resident: 4696 bytes in 5 allocations: polygons 1248 cells 352 indices 80 nodes 2496 triangles 520\n"
    "memory stored: 1048 bytes as the file holds the same arrays\n"
    "memory ceiling: 30932992 resident bytes at the default collision limits\n")
nfsu_case("the packed fixture loads" 0
          "file size: 1313 image bytes 1152 unused packed bytes 0\n;${summary};memory load: packed 1313 image 1152 peak 6641\n;switch plan: persistent 4696 of 67108864 bytes (0 per mille) extent 4696;streaming load peak 2465 of 50331648 bytes (0 per mille), released\n;probe: no point given"
          "issue;probe column;probe segment"
          "${packed}")
set(packed_listing "${last_stdout}")

# The same run again: nothing in the listing may depend on the run.
nfsu_case("the packed fixture loads twice" 0 "probe: no point given" "issue" "${packed}")
if(NOT last_stdout STREQUAL packed_listing)
    nfsu_fail("two runs over the same file printed different listings:\n${packed_listing}\n---\n${last_stdout}")
endif()

# Uncompressed, the same image is the same world; only the load buffers
# differ, because the file is the image.
nfsu_case("the uncompressed fixture loads" 0
          "file size: 1152 image bytes 1152 unused packed bytes 0\n;${summary};memory load: packed 1152 image 1152 peak 6480\n"
          "issue"
          "${image}")

# A column onto the street, from above: road, facing up.
nfsu_case("a column onto the street" 0
          "probe column: px 980 pz -395 from py 1000 down 2000\n;probe column: hit road polygon 0 triangle 1 distance 949.75 point (980, -395, 50.25) normal (0, 0, 1) up 1\n"
          "probe segment;refused"
          "${image}" "980" "-395")
# The deck over the bridge is met before the road beneath it.
nfsu_case("a column onto the bridge deck" 0
          "probe column: hit road polygon 9 triangle 0 distance 1010 point (-810, 300, -10) normal (0, 0, 1) up 1\n"
          "refused"
          "${image}" "-810" "300")
# The gap in the street is covered by a cell and has no surface: airborne.
nfsu_case("a column over the gap is clear" 0
          "probe column: clear\n"
          "hit;outside;refused"
          "${image}" "992" "-400")
# No cell covers it: out of bounds.
nfsu_case("a column no cell covers is outside" 0
          "probe column: outside\n"
          "hit;clear;refused"
          "${image}" "500" "500")
# A sweep past the corner of the origin section's cells, which end at
# (32, 32) with nothing beyond: its bounding box reaches cell (16, 16), the
# segment meets no cell, so it is out of bounds and not airborne. Through
# the corner itself the closed square is met and nothing on it is: clear.
nfsu_case("a sweep past every cell's corner is outside" 0
          "probe segment: (30, 40, 3) to (40, 30, 3)\n;probe segment: outside\n"
          "hit;clear;refused"
          "${image}" "30" "40" "3" "40" "30" "3")
nfsu_case("a sweep through a cell's corner is clear" 0
          "probe segment: clear\n"
          "hit;outside;refused"
          "${image}" "28" "36" "3" "36" "28" "3")
# A sweep into a barrier from inside the street, and from outside it: the
# same polygon, the normal turned to face each sweep.
nfsu_case("a sweep into the barrier from the street" 0
          "probe segment: (1000, -400, 51) to (1000, -420, 51)\n;probe segment: hit wall polygon 2 triangle 0 distance 11 point (1000, -411, 51) normal (0, 1, 0) up 0\n"
          "probe column;refused"
          "${image}" "1000" "-400" "51" "1000" "-420" "51")
nfsu_case("a sweep into the barrier from outside" 0
          "probe segment: hit wall polygon 2 triangle 0 distance 4 point (1000, -411, 51) normal (0, -1, 0) up 0\n"
          "refused"
          "${image}" "1000" "-415" "51" "1000" "-405" "51")
# A 70-degree face is met as wall even though it is not vertical.
nfsu_case("a sweep into the steep face" 0
          "probe segment: hit wall polygon 7 triangle 1 distance 13.2727 point (1673.27, 720, 80) normal (-0.939793, 0, 0.341743) up 0.341743\n"
          "refused"
          "${image}" "1660" "720" "80" "1690" "720" "80")
# A segment of no length is refused, and says so with its own exit.
nfsu_case("a segment of no length is refused" 5
          "probe segment: refused\n"
          "hit;clear;outside"
          "${image}" "1000" "-400" "51" "1000" "-400" "51")
# A segment longer than the cap of 4096 world units is refused, not cut.
nfsu_case("a segment over the length cap is refused" 5
          "probe segment: refused\n"
          "hit;clear;outside"
          "${image}" "-3000" "0" "0" "3000" "0" "0")

# The image cut short fails its own checks and answers nothing.
nfsu_case("a broken image is refused" 3
          "file size: 1149 image bytes 1149;collision: refused;collision issue at 1112: chunk 0x00034134 claims a payload of 32 bytes, which reaches past the end of the image"
          "memory resident;switch plan;probe column"
          "${broken}" "980" "-395")

# --write-fixture never replaces a file.
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
    message(FATAL_ERROR "${failures} track-contact guard case(s) failed")
endif()
message(STATUS "track-contact guards: every case passed")
