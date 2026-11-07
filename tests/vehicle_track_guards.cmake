# What `nfsu_probe vehicle-track` refuses, prints and exits with.
#
# Every case needs NO RETAIL FILE: the argument cases need no input at all,
# the course cases drive the proving ground, which nfsu_vehicle_track builds
# in code, and the script cases drive it over text files this script writes
# itself. The circuit form's retail run is pinned in the course fixtures
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
    "rest=2786EEED0625538554ACA9673003D1303B4E3A6602F05F16173F23DF14F35EF6"
    "drive=606CF4053F5719957B63F757A8B78A411B36BC594F2EF9D0DDD5A8450B105DFB"
    "slope=E124F9D34411E5BDED01400D884DFA03378F199CB682A721A0D49559C88E9F28"
    "wall=3E57CAD1C2962B938602A134394B93EB07C958D20B9827F23C0F52046CDB81AF"
    "wall-glance=F10C543374EEBA519020F3036BB5CB765887862F40A3884A8033F39CD380256A"
    "wall-fast=AF1A29DACB2FD3555CA9B0351B8F50BF9BD47AA377D7A1875352C8C5313A9F50"
    "wall-square=3DFC3A3EC1FD84FB56923C86F6715FEA5CAD50EEC7E3FE39B845DA1A17960815"
    "wall-oblique=A66A940CF9AB197AEBB0F7A0974170AC7D8850E44AD37B302C0834412C79F90B"
    "drop=A404CC5C429D971CE2915DA4CEC71B9AD9112C373148751885D11E6F87DAED53"
    "leave=B2D804AA75051CCDBB2E33FA9BDD7A6CC1230AD1C6B9B46BC73B923DD781AE47"
    "zone=DB925A690B40AF8D4ACD7E798E4B30BC299A903D05BD382E43EDA9DF20A1DB0B"
    "zone-point=898196E5FBEFB9C453B795CF138469DA0AB7B1613633ED3A71F75279397C886B"
    "far=86B73FEAB6EA7691A816DE945D683C7EAE826BE40315CD934B4A87D603532831")
foreach(pin IN LISTS pins)
    string(REPLACE "=" ";" parts "${pin}")
    list(GET parts 0 name)
    list(GET parts 1 hash)
    nfsu_case("the ${name} scenario" 0
              "course: proving ground, built in code, 51 polygons, route of 24 nodes, 2 zones;start: grid slot 0 marker 0 x 32 y 48 z 10 angle 0x00000000 ground 10.0000;sha256 ${hash};pinned: EQUAL to stored scenario ${hash}"
              "DIFFERENT"
              --proving-ground ${name} --every 0)
endforeach()

# What the pinned runs print on the way: a contact streak, the resets and
# the checks that asked for them.
nfsu_case("a wall is met" 0
          "contact step 390 polygon 32 wall depth 0.0682" ""
          --proving-ground wall --every 0)
nfsu_case("out of bounds, then reset" 0
          "reset-requested step 434 out-of-bounds;bounds step 434: no collision under the car nor one unit along +x;reset step 436 out-of-bounds node 10 ground 176.000 40.000 10.0000 heading 0x0000030A speed 0.000"
          "" --proving-ground leave --every 0)
nfsu_case("a RESET zone" 0
          "race step 262 reset-zone index 0;reset step 264 reset-zone node 15 ground 128.000 72.000 10.0000 heading 0x00008000 speed 0.000"
          "" --proving-ground zone --every 0)
nfsu_case("a RESET_TO_POINT zone" 0
          "reset step 262 reset-to-point-zone zone 1 ground 40.000 56.000 10.0000 heading 0x00000000 speed 8.000"
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
nfsu_case("--steps 10" 0 "telemetry: records 10 bytes 15640 sha256" "pinned:"
          --proving-ground rest --steps 10)

# ---------------------------------------------------------------------------
# Script files
# ---------------------------------------------------------------------------

# The rest scenario's input as a file, from the start: the same telemetry.
file(WRITE "${work}/rest.txt" "# the rest scenario\n240 0 1 0 0 none\n")
nfsu_case("the file form of rest" 0 "scenario rest.txt: a script file, from the start" "pinned:"
          --proving-ground --script "${work}/rest.txt" --every 0)
if(NOT last_hash STREQUAL "2786EEED0625538554ACA9673003D1303B4E3A6602F05F16173F23DF14F35EF6")
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
