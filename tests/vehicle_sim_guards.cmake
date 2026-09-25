# What `nfsu_probe vehicle-sim` refuses, prints and exits with.
#
# The argument cases need NO INPUT FILES AT ALL. The script cases drive the
# command over text files this script writes itself, because a script is
# text; the handling cases over three synthetic GLOBALB.BUN files
# nfsu_vehicle_tests writes on request - the stock 240SX handling, the same
# with one field changed, and one the model must refuse - which is the only
# way to reach --handling without retail data.
#
# Exit statuses: 0 a run, 1 an argument the command refuses, 2 a script file
# that cannot be read or parsed, 3 a handling file that cannot drive the
# model. 4, the model refusing its own state mid-run, has no case: no input
# this command accepts reaches it, which nfsu_vehicle_tests' sanitising
# cases are the evidence for.
#
# A script rather than add_test properties for the reason
# tests/track_render_guards.cmake gives: PASS_REGULAR_EXPRESSION makes CTest
# ignore the exit status, and the status is half of what is pinned here.
#
# Run from the build directory. PROBE is the built program and TESTS the
# built nfsu_vehicle_tests.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE OR NOT DEFINED TESTS)
    message(FATAL_ERROR "PROBE and TESTS must both be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_vehicle_sim_guards")
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
# after them is the command line after "vehicle-sim". The telemetry hash
# the run printed, if any, is left in ${last_hash}.
function(nfsu_case label code needles banned)
    execute_process(
        COMMAND "${PROBE}" vehicle-sim ${ARGN}
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    string(REPLACE "\r\n" "\n" stdout_text "${stdout_text}")
    string(REPLACE "\r\n" "\n" stderr_text "${stderr_text}")
    set(text "${stdout_text}${stderr_text}")
    set(last_hash "" PARENT_SCOPE)
    if(stdout_text MATCHES "sha256 ([0-9A-F]+)\n")
        set(last_hash "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
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
set(ran "vehicle-sim: script;telemetry: records")

# ---------------------------------------------------------------------------
# Arguments, no input files
# ---------------------------------------------------------------------------

nfsu_case("no script at all" 1
          "nfsu_probe vehicle-sim <script-name> | --script <FILE>"
          "${ran}")
nfsu_case("an unknown script name" 1
          "no built-in script is named 'drift'; the names are rest accelerate brake corner slide top-speed manual mixed"
          "${ran}"
          drift)
nfsu_case("two script names" 1
          "error: give one script name only"
          "${ran}"
          rest mixed)
nfsu_case("options without a script" 1
          "error: give either a built-in script name or --script <FILE>"
          "${ran}"
          --steps 10)
nfsu_case("a name and a file" 1
          "error: give either a built-in script name or --script <FILE>"
          "${ran}"
          rest --script "${work}/never_read.txt")
nfsu_case("--script without its file" 1
          "error: --script needs a file"
          "${ran}"
          --script)
nfsu_case("--steps without its value" 1
          "error: --steps needs a value"
          "${ran}"
          rest --steps)
nfsu_case("--steps that is not a number" 1
          "error: --steps must be a decimal or 0x integer no larger than 12000000"
          "${ran}"
          rest --steps 1e3)
nfsu_case("--steps over the limit" 1
          "error: --steps must be a decimal or 0x integer no larger than 12000000"
          "${ran}"
          rest --steps 12000001)
nfsu_case("--steps twice" 1
          "error: --steps is given twice"
          "${ran}"
          rest --steps 5 --steps 6)
nfsu_case("--every that is not a number" 1
          "error: --every must be a decimal or 0x integer"
          "${ran}"
          rest --every -1)
nfsu_case("an unknown option" 1
          "error: unknown option --heading"
          "${ran}"
          rest --heading 90)
nfsu_case("--car without --handling" 1
          "error: --car needs --handling <GLOBALB.BUN>"
          "${ran}"
          rest --car 240SX)
nfsu_case("--car twice" 1
          "error: --car is given twice"
          "${ran}"
          rest --handling "${work}/never_read.bun" --car A --car B)

# ---------------------------------------------------------------------------
# Built-in scripts
# ---------------------------------------------------------------------------

nfsu_case("the rest script" 0
          "handling: built-in 240SX table (RECOVERED, docs/VEHICLE.md)\n;vehicle-sim: script rest segments 1 steps 240 of 1/120 s gearbox automatic speed 0.000000 m/s\n;shift up rpm: 1:7400.00 2:7400.00 3:7150.00 4:6750.00 5:7500.00\n;shift down rpm: 2:4238.12 3:5088.96 4:5466.36 5:6075.00\n;at step 120 t 1.000 ;final step 240 t 2.000 ;telemetry: records 240 bytes 117120 sha256 "
          "at step 240 ;error"
          rest)
set(rest_hash "${last_hash}")
string(LENGTH "${rest_hash}" rest_hash_length)
if(NOT rest_hash_length EQUAL 64)
    nfsu_fail("the rest run printed no 64-digit telemetry hash: '${rest_hash}'")
endif()

# The pinned telemetry of docs/VEHICLE.md, through the probe.
nfsu_case("the mixed script" 0
          "vehicle-sim: script mixed segments 8 steps 2400 ;telemetry: records 2400 bytes 1171200 sha256 20724B57886697F99C9B5FF631CAE0B6797B7E47BA3F21A027E5F528135EC085\n"
          "error"
          mixed --every 0)
set(mixed_hash "${last_hash}")

# --every 0 prints no intermediate line; --steps past the script coasts on.
nfsu_case("--every 0 and --steps past the end" 0
          "steps 300 of 1/120;final step 300 ;telemetry: records 300 bytes 146400 "
          "at step"
          rest --every 0 --steps 300)
# No step at all is still a run: the hash of no records.
nfsu_case("--steps 0" 0
          "steps 0 of 1/120;telemetry: records 0 bytes 0 sha256 E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855\n"
          "final step"
          rest --steps 0)

# ---------------------------------------------------------------------------
# Script files
# ---------------------------------------------------------------------------

nfsu_case("a script file that is not there" 2
          "error: cannot open the script"
          "${ran}"
          --script "${work}/missing_script.txt")

file(WRITE "${work}/bad_throttle.txt" "# one good line\n10 0 0 0 0 none\n10 2 0 0 0 none\n")
nfsu_case("a throttle above 1" 2
          "error: script bad_throttle.txt: line 3: the throttle must be a fixed decimal from 0 to 1"
          "${ran}"
          --script "${work}/bad_throttle.txt")

file(WRITE "${work}/bad_header.txt" "10 0 0 0 0 none\nspeed 5\n")
nfsu_case("a header after a segment" 2
          "error: script bad_header.txt: line 2: 'speed' must come before the first segment"
          "${ran}"
          --script "${work}/bad_header.txt")

file(WRITE "${work}/empty.txt" "# nothing to drive\n")
nfsu_case("a script with no segment" 2
          "error: script empty.txt: the script has no segment"
          "${ran}"
          --script "${work}/empty.txt")

# The built-in rest and mixed scripts written out as text: the file route
# must give the same telemetry, bit for bit, as the built-in one, which is
# what says the parser reads the decimals the built-ins are written with.
file(WRITE "${work}/rest.txt" "240 0 0 0 0 none\n")
nfsu_case("rest as a file" 0
          "vehicle-sim: script rest.txt segments 1 steps 240 ;telemetry: records 240 "
          "error"
          --script "${work}/rest.txt")
if(NOT last_hash STREQUAL rest_hash)
    nfsu_fail("rest as a file hashes ${last_hash}, the built-in ${rest_hash}")
endif()

# file(WRITE) writes "\n" as CR LF on Windows and as LF elsewhere, so this
# also reads a CR LF script where the tests run on Windows.
file(WRITE "${work}/mixed.txt"
     "# the built-in mixed script\n"
     "gearbox automatic\n"
     "120 0 0 0 0 none\n"
     "720 1 0 0 0 none\n"
     "240 0.5 0 0.4 0 none\n"
     "240 0.5 0 -0.4 0 none\n"
     "240 0 0.8 0 0 none\n"
     "120 0 0 0.6 1 none\n"
     "360 0.6 0 0 0 none\n"
     "360 0 0 0 0 none\n")
nfsu_case("mixed as a file" 0
          "vehicle-sim: script mixed.txt segments 8 steps 2400 "
          "error"
          --script "${work}/mixed.txt" --every 0)
if(NOT last_hash STREQUAL mixed_hash)
    nfsu_fail("mixed as a file hashes ${last_hash}, the built-in ${mixed_hash}")
endif()

file(WRITE "${work}/manual.txt" "gearbox manual\nspeed 10\n60 1 0 0 0 up\n")
nfsu_case("a manual script from speed" 0
          "vehicle-sim: script manual.txt segments 1 steps 60 of 1/120 s gearbox manual speed 10.000000 m/s\n;final step 60 ;gear 2 "
          "error"
          --script "${work}/manual.txt")

# ---------------------------------------------------------------------------
# --handling
# ---------------------------------------------------------------------------

nfsu_case("a handling file that is not there" 3
          "handling file: missing_globalb.bun;error: the car type table is not valid"
          "${ran}"
          rest --handling "${work}/missing_globalb.bun")

execute_process(
    COMMAND "${TESTS}" --write-handling-fixture "${work}/fixture"
    OUTPUT_VARIABLE fixture_text
    ERROR_VARIABLE fixture_error
    RESULT_VARIABLE fixture_status)
set(stock "${work}/fixture/vehicle_globalb.bun")
set(other "${work}/fixture/vehicle_globalb_other.bun")
set(broken "${work}/fixture/vehicle_globalb_broken.bun")
if(NOT fixture_status EQUAL 0 OR NOT EXISTS "${stock}" OR NOT EXISTS "${other}"
   OR NOT EXISTS "${broken}")
    message(FATAL_ERROR
        "the fixture could not be written: ${fixture_status}\n"
        "${fixture_text}${fixture_error}")
endif()

# Record 18 of the fixture starts at 16 + 18 * 0xC90 = 57904: the chunk
# header, eight bytes of 0x11 filler, then the records.
nfsu_case("the stock handling from a file" 0
          "handling file: vehicle_globalb.bun bytes 112576\n;handling: car type 18 240SX record @57904 equal to the built-in 240SX table\n"
          "built-in 240SX table (RECOVERED;error"
          rest --handling "${stock}")
if(NOT last_hash STREQUAL rest_hash)
    nfsu_fail("the stock handling read from a file hashes ${last_hash}, the built-in table ${rest_hash}")
endif()

nfsu_case("--car picks the record" 0
          "handling: car type 18 240SX record @57904 equal"
          "error"
          rest --handling "${stock}" --car 240SX --steps 12)

nfsu_case("a car the table does not hold" 3
          "error: no car type is named 'NOPE'"
          "${ran}"
          rest --handling "${stock}" --car NOPE)

# One field changed: the run goes ahead on it, says where it differs, and
# gives other telemetry. 0x370 is the drag; 57904 + 0x370 = 58784.
nfsu_case("a handling that differs" 0
          "handling: car type 18 240SX record @57904 differs from the built-in 240SX table first at record 0x00000370 @58784\n"
          "error"
          accelerate --handling "${other}" --every 0)
set(other_hash "${last_hash}")
nfsu_case("the same script on the built-in table" 0
          "telemetry: records 3660 "
          "error"
          accelerate --every 0)
if(other_hash STREQUAL last_hash)
    nfsu_fail("a changed drag left the accelerate telemetry unchanged: ${last_hash}")
endif()

# Idle above the redline: the table is valid, the model refuses the car by
# the field's absolute offset, and nothing runs.
nfsu_case("a handling the model refuses" 3
          "handling issue: record 18 at offset 57904 +0x304: the redline must be above idle\n;error: the handling of '240SX' cannot drive the model\n"
          "${ran}"
          rest --handling "${broken}")

# --write-handling-fixture never replaces a file, and refuses before it
# writes any.
execute_process(
    COMMAND "${TESTS}" --write-handling-fixture "${work}/fixture"
    OUTPUT_VARIABLE again_text
    ERROR_VARIABLE again_error
    RESULT_VARIABLE again_status)
string(FIND "${again_error}" "already exists" named)
if(again_status EQUAL 0 OR named LESS 0)
    nfsu_fail("--write-handling-fixture over existing files must refuse and say so: "
              "status ${again_status}\n${again_text}${again_error}")
endif()

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} vehicle-sim guard case(s) failed")
endif()
message(STATUS "vehicle-sim guards: every case passed")
