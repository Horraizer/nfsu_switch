# What `nfsu_probe vehicle-track` refuses, prints and exits with.
#
# Every case needs NO RETAIL FILE: the argument cases need no input at all,
# the course cases drive the proving ground, which nfsu_vehicle_track builds
# in code, and the script cases drive it over text files this script writes
# itself. The circuit form's retail run is pinned in docs/VEHICLE_TRACK.md
# and needs the retail files; here it is only refused, over files that do
# not exist.
#
# Exit statuses: 0 a run, 1 an argument the command refuses, 2 a script file
# that cannot be read or parsed, 3 a course that cannot be built (missing or
# malformed tables and bundle). 4, the model or the session refusing a step,
# has no case: no input the command accepts reaches it.
#
# A script rather than add_test properties for the reason
# tests/track_render_guards.cmake gives: PASS_REGULAR_EXPRESSION makes CTest
# ignore the exit status, and the status is half of what is pinned here.
#
# Run from the build directory. PROBE is the built program.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE must be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_vehicle_track_guards")
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
# after them is the command line after "vehicle-track". The telemetry hash
# the run printed, if any, is left in ${last_hash}, and the number of lines
# that begin with "at step " in ${last_at}.
function(nfsu_case label code needles banned)
    execute_process(
        COMMAND "${PROBE}" vehicle-track ${ARGN}
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    string(REPLACE "\r\n" "\n" stdout_text "${stdout_text}")
    string(REPLACE "\r\n" "\n" stderr_text "${stderr_text}")
    set(text "${stdout_text}${stderr_text}")
    set(last_hash "" PARENT_SCOPE)
    if(stdout_text MATCHES "telemetry: records [0-9]+ bytes [0-9]+ sha256 ([0-9A-F]+)\n")
        set(last_hash "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
    string(REGEX MATCHALL "\nat step " at_lines "\n${stdout_text}")
    list(LENGTH at_lines at_count)
    set(last_at "${at_count}" PARENT_SCOPE)
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

# What a run that got past its arguments prints; no refusal may print it.
set(ran "telemetry: records;scenario start:")

# ---------------------------------------------------------------------------
# Arguments, no input files
# ---------------------------------------------------------------------------

set(too_few "error: vehicle-track needs <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event> and a scenario or --script <FILE>, or --proving-ground and a scenario or --script <FILE>")
nfsu_case("no arguments at all" 1
          "nfsu_probe vehicle-track <GLOBALB.BUN> <FrontB.lzc> <TRACKB####.lzc> <event>"
          "${ran}")
nfsu_case("the circuit form one short" 1 "${too_few}" "${ran}"
          a.bun b.lzc c.lzc 1)
nfsu_case("an event that is not a number" 1 "error: the event must be a number" "${ran}"
          a.bun b.lzc c.lzc one grid)
nfsu_case("the proving ground without a scenario" 1
          "error: give either a built-in scenario name or --script <FILE>" "${ran}"
          --proving-ground)
nfsu_case("an unknown scenario" 1
          "no built-in scenario is named 'spin'; the names are rest drive slope wall wall-glance wall-fast wall-square wall-oblique drop leave zone zone-point far"
          "${ran}"
          --proving-ground spin)
nfsu_case("a circuit scenario on the proving ground" 1
          "no built-in scenario is named 'grid'" "${ran}"
          --proving-ground grid)
nfsu_case("two scenario names" 1 "error: give one scenario name only" "${ran}"
          --proving-ground rest drive)
nfsu_case("a name and a file" 1
          "error: give either a built-in scenario name or --script <FILE>" "${ran}"
          --proving-ground rest --script "${work}/never_read.txt")
nfsu_case("--script without its file" 1 "error: --script needs a file" "${ran}"
          --proving-ground --script)
nfsu_case("--script twice" 1 "error: --script is given twice" "${ran}"
          --proving-ground --script a.txt --script b.txt)
nfsu_case("--steps without its value" 1 "error: --steps needs a value" "${ran}"
          --proving-ground rest --steps)
nfsu_case("--steps that is not a number" 1
          "error: --steps must be a decimal or 0x integer no larger than 12000000" "${ran}"
          --proving-ground rest --steps ten)
nfsu_case("--every too large" 1
          "error: --every must be a decimal or 0x integer no larger than 12000000" "${ran}"
          --proving-ground rest --every 12000001)
nfsu_case("--steps twice" 1 "error: --steps is given twice" "${ran}"
          --proving-ground rest --steps 1 --steps 2)
nfsu_case("an unknown option" 1 "error: unknown option --fast" "${ran}"
          --proving-ground rest --fast)

# ---------------------------------------------------------------------------
# The circuit form over files that do not exist
# ---------------------------------------------------------------------------

nfsu_case("missing tables" 3 "error: the track or event table is not valid" "${ran}"
          "${work}/missing_GLOBALB.BUN" "${work}/missing_FrontB.lzc"
          "${work}/missing_TRACKB1003.lzc" 1 grid)

# ---------------------------------------------------------------------------
# Every proving-ground scenario, pinned
# ---------------------------------------------------------------------------

set(pins
    "rest=C67E6C0839F2CF29F89CA1381BC16A17A7DB2F9BF04944BE23A7313B814158C9"
    "drive=2D61E7FC354F7394BA4DC2721298041E7799D276BF3F9075C18DBB114D3BB79C"
    "slope=F568FFB6B4F99A9735E661FAEB508ABF151CA5A22E800773D16503B64DB34D5A"
    "wall=1B4E823836D8A5C89A4F83D469A93716C8215CCF6105FA5B1EB73C52A590183F"
    "wall-glance=A8CBD1B047CDB1AB1DEE241E6AE0F08C8C64452A25020A2C5D6970D6EF9FA23A"
    "wall-fast=5290BE387D92655B91ABEB16311F4ACEFFBEE5377795FD3F97EA70451DAD6517"
    "wall-square=EE7FCBFC75A22E6FD93E94A2333E5102CC9C9FEA059F00BE0A0333FED3938B5B"
    "wall-oblique=0A7ABFBAF1E1CCF928FA4A4AF4CF47D1FAFBD494944BAEEB9403999ED5683407"
    "drop=42F487ED449BAC275708B0B3368C22C76BD8C3B7D55D502A3F5BB9C2F29D4D5D"
    "leave=CAD2A27BD41870D8E1D310EE83D041B439D1F5B4CB6874E29BC26176410C7D92"
    "zone=7B063440CD041607AB943115A820FBCFC437D1ACA765A773B2A3B202F6291491"
    "zone-point=1F01302B371D1F019746930FC80EFB2D7A58866B3B894A583B2DD434A009589B"
    "far=2AA0045CB95A3CA615A140A86AA32909321D6B4265B06BFBB59D82AE5C862F27")
foreach(pin IN LISTS pins)
    string(REPLACE "=" ";" parts "${pin}")
    list(GET parts 0 name)
    list(GET parts 1 hash)
    nfsu_case("the ${name} scenario" 0
              "course: proving ground, built in code, 51 polygons, route of 24 nodes, 2 zones;start: grid slot 0 marker 0 x 32 y 48 z 10 angle 0x00000000 ground 10.0000;sha256 ${hash};pinned: EQUAL to docs/VEHICLE_TRACK.md ${hash}"
              "DIFFERENT"
              --proving-ground ${name} --every 0)
endforeach()

# What the pinned runs print on the way: a contact streak, the resets and
# the checks that asked for them.
nfsu_case("a wall is met" 0
          "contact step 331 polygon 32 wall depth 0.0631" ""
          --proving-ground wall --every 0)
nfsu_case("out of bounds, then reset" 0
          "reset-requested step 370 out-of-bounds;bounds step 370: no collision under the car nor one unit along +x;reset step 372 out-of-bounds node 10 ground 176.000 40.000 10.0000 heading 0x000002F4 speed 0.000"
          "" --proving-ground leave --every 0)
nfsu_case("a RESET zone" 0
          "race step 508 reset-zone index 0;reset step 510 reset-zone node 15 ground 128.000 72.000 10.0000 heading 0x00008000 speed 0.000"
          "" --proving-ground zone --every 0)
nfsu_case("a RESET_TO_POINT zone" 0
          "reset step 510 reset-to-point-zone zone 1 ground 40.000 56.000 10.0000 heading 0x00000000 speed 8.000"
          "" --proving-ground zone-point --every 0)
nfsu_case("off the path" 0
          "reset-requested step 2 off-path;reset step 4 off-path node 0 ground 16.000 40.000 10.0000"
          "" --proving-ground far --every 0)

# --every: a line every N steps and never on the last one; --steps: the
# first N steps and no pin, since a shortened run is not the pinned one.
nfsu_case("--every 60 over 240 steps" 0 "final step 240" "" --proving-ground rest --every 60)
if(NOT last_at EQUAL 3)
    nfsu_fail("--every 60 over 240 steps prints 3 at-lines, got ${last_at}")
endif()
nfsu_case("--steps 10" 0 "telemetry: records 10 bytes 7600 sha256" "pinned:"
          --proving-ground rest --steps 10)

# ---------------------------------------------------------------------------
# Script files
# ---------------------------------------------------------------------------

# The rest scenario's input as a file, from the start: the same telemetry.
file(WRITE "${work}/rest.txt" "# the rest scenario\n240 0 1 0 0 none\n")
nfsu_case("the file form of rest" 0 "scenario rest.txt: a script file, from the start" "pinned:"
          --proving-ground --script "${work}/rest.txt" --every 0)
if(NOT last_hash STREQUAL "C67E6C0839F2CF29F89CA1381BC16A17A7DB2F9BF04944BE23A7313B814158C9")
    nfsu_fail("the file form of rest hashes ${last_hash}, not the rest scenario's")
endif()
file(WRITE "${work}/bad.txt" "240 0 1 0 0 sideways\n")
nfsu_case("a script that does not parse" 2 "error: script bad.txt: line 1" "${ran}"
          --proving-ground --script "${work}/bad.txt")
nfsu_case("a script that does not exist" 2 "error: cannot open the script" "${ran}"
          --proving-ground --script "${work}/absent.txt")

if(failures GREATER 0)
    message(FATAL_ERROR "${failures} vehicle-track guard case(s) failed")
endif()
message(STATUS "vehicle-track guards: every case held")
