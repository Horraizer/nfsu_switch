# Decode UTF-8 path diagnostics explicitly instead of using the Windows
# console code page selected by AUTO.
# What `nfsu_probe asset-check` refuses, prints and exits with (issue #47).
#
# The first nine cases need NO INPUT FILES: the argument shapes, the set
# name, a root that is not there, a root that is a file and an empty root.
# The rest drive the command over directories this script writes: a mixed
# install of the first-lap set - one file right, two of the right size and
# the wrong content, one cut short, the rest missing, some names in another
# case - and a stock-car install whose six files are all the right size and
# all the wrong content, then with one of them cut short and one replaced by
# a directory. No retail byte is written: every file is one repeated
# letter, so every hash below that is not a retail one is computed here from
# the file the script wrote.
#
# What no case here can reach is exit 0: it needs every file of a set to be
# the retail file, and the only retail file whose bytes are known without
# the game is the empty Tracks/STREAML1RA_1003.BUN, which no set holds alone.
# nfsu_asset_tests reaches the complete verdict over synthetic manifests, and
# docs/SWITCH_ASSETS.md pins the command's complete report over the retail
# installation.
#
# A script rather than add_test properties for the reason
# tests/track_render_guards.cmake gives: PASS_REGULAR_EXPRESSION makes CTest
# ignore the exit status, and here the status is the machine-readable half
# of the verdict.
#
# Run from the build directory. PROBE is the built program.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE must be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_asset_check_guards")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

set(failures 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case, run in ${work}. `code` is the exact exit status required,
# `needles` a list of texts the combined output must each contain and
# `banned` a list it must not; both are semicolon-separated CMake lists, so
# no needle here holds a semicolon. Everything after them is the command line
# after "asset-check".
function(nfsu_case label code needles banned)
    execute_process(
        ENCODING UTF-8
        COMMAND "${PROBE}" asset-check ${ARGN}
        WORKING_DIRECTORY "${work}"
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    string(REPLACE "\r\n" "\n" stdout_text "${stdout_text}")
    string(REPLACE "\r\n" "\n" stderr_text "${stderr_text}")
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

# A file of `size` bytes, every one the letter `letter`.
function(nfsu_write_letters path letter size)
    get_filename_component(parent "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    if(size EQUAL 0)
        file(WRITE "${path}" "")
    else()
        string(REPEAT "${letter}" ${size} content)
        file(WRITE "${path}" "${content}")
    endif()
    file(SIZE "${path}" written)
    if(NOT written EQUAL size)
        message(FATAL_ERROR "${path} is ${written} bytes, expected ${size}")
    endif()
endfunction()

function(nfsu_upper_hash path out_var)
    file(SHA256 "${path}" hash)
    string(TOUPPER "${hash}" hash)
    set(${out_var} "${hash}" PARENT_SCOPE)
endfunction()

set(usage_line "nfsu_probe asset-check <root> [--set <first-lap|first-lap-1005|stock-car|event-1>]")

# ---------------------------------------------------------------------------
# No input files
# ---------------------------------------------------------------------------

nfsu_case("no root is a usage error" 1
          "${usage_line}"
          "asset-check: root")

nfsu_case("a second positional is a usage error" 1
          "${usage_line}"
          "asset-check: root"
          "${work}/absent" "extra")

nfsu_case("--set with no value is a usage error" 1
          "${usage_line}"
          "asset-check: root"
          "${work}/absent" "--set")

nfsu_case("an option that is not --set" 1
          "error: unknown option --sets"
          "asset-check: root;${usage_line}"
          "${work}/absent" "--sets" "first-lap")

nfsu_case("a set that is not one" 1
          "error: --set must be one of first-lap first-lap-1005 stock-car event-1\n"
          "asset-check: root;${usage_line}"
          "${work}/absent" "--set" "second-lap")

# The root is asked for once and every file is missing because of it; the
# copy list is the whole set, under the root as it was given.
nfsu_case("a root that is not there" 2
          "asset-check: root ${work}/absent\n;set first-lap: one timed lap of event 1 in the stock 240SX (issue #159), 14 files, 114102502 bytes\n;root ${work}/absent cannot be listed: it does not exist\n;MISSING       Cars/240SX/GEOMETRY.BIN  the root does not exist\n;MISSING       Tracks/TRACKB1003.lzc  the root does not exist\n;install: MISSING FILES - 0 of 14 files ok: 14 missing, 0 wrong size, 0 wrong version, 0 unreadable\n;copy these 14 files from the game's installation folder, keeping the folders:\n;  Tracks/STREAML1RA.BUN -> ${work}/absent/Tracks/STREAML1RA.BUN\n;  FrontEnd/FrontB.lzc -> ${work}/absent/FrontEnd/FrontB.lzc\n;read 0 bytes in 0 reads and 1 directory listings\n"
          "ok  ;WRONG"
          "${work}/absent")

file(WRITE "${work}/a-file" "not a directory")
nfsu_case("a root that is a file" 2
          "root ${work}/a-file cannot be listed: it is not a directory\n;install: MISSING FILES - 0 of 14"
          "ok  "
          "${work}/a-file")

file(MAKE_DIRECTORY "${work}/empty")
nfsu_case("an empty root" 3
          "MISSING       Cars/240SX/GEOMETRY.BIN  no Cars in the root\n;MISSING       FrontEnd/FrontB.lzc  no FrontEnd in the root\n;MISSING       Tracks/STREAML1RA_1003.BUN  no Tracks in the root\n;install: MISSING FILES - 0 of 14 files ok: 14 missing;read 0 bytes in 0 reads and 1 directory listings\n"
          "cannot be listed"
          "${work}/empty")

nfsu_case("an empty root, one set" 3
          "set stock-car: the stock 240SX nfsu_switch_car draws, 6 files, 9140732 bytes\n;install: MISSING FILES - 0 of 6 files ok: 6 missing;  Global/GLOBALB.BUN -> ${work}/empty/Global/GLOBALB.BUN\n"
          "Tracks/;FrontEnd/"
          "${work}/empty" "--set" "stock-car")

# ---------------------------------------------------------------------------
# A mixed first-lap install
# ---------------------------------------------------------------------------

# The Tracks directory is written in lower case and two names in it in
# another case again, so every line below that says "found as" is the
# naming rule at work. STREAML1RA_1003.BUN is the one file here that is the
# retail file: the retail one is empty.
set(mixed "${work}/mixed")
nfsu_write_letters("${mixed}/tracks/STREAML1RA_1003.bun" "e" 0)
nfsu_write_letters("${mixed}/tracks/trackb1003.LZC" "t" 433352)
nfsu_write_letters("${mixed}/Global/GLOBALB.BUN" "g" 1000)
nfsu_write_letters("${mixed}/Cars/PLATES/GEOMETRY.BIN" "p" 1184)
nfsu_upper_hash("${mixed}/tracks/trackb1003.LZC" track_hash)
nfsu_upper_hash("${mixed}/Cars/PLATES/GEOMETRY.BIN" plate_hash)

# Two files hashed, one probed: 1 + 1 chunks and 3 probes.
nfsu_case("a mixed install" 3
          "ok            Tracks/STREAML1RA_1003.BUN  0 bytes, sha256 E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 (found as tracks/STREAML1RA_1003.bun)\n;WRONG VERSION Tracks/TRACKB1003.lzc  433352 bytes, sha256 ${track_hash} expected ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA (found as tracks/trackb1003.LZC)\n;WRONG SIZE    Global/GLOBALB.BUN  1000 bytes, expected 2361176: shorter than the retail file: an interrupted copy?\n;WRONG VERSION Cars/PLATES/GEOMETRY.BIN  1184 bytes, sha256 ${plate_hash} expected 0EBCFD14DD303613F5DA3FF5D7A6DF57BE3CDE915F458BA243FE5D089B64BD8A\n;MISSING       Cars/240SX/GEOMETRY.BIN  no 240SX in Cars\n;MISSING       Global/INGAMEB.BUN  no INGAMEB.BUN in Global\n;MISSING       Tracks/STREAML1RA.BUN  no STREAML1RA.BUN in tracks\n;install: MISSING FILES - 1 of 14 files ok: 10 missing, 1 wrong size, 2 wrong version, 0 unreadable\n;copy these 13 files from the game's installation folder, keeping the folders:\n;  Tracks/TRACKB1003.lzc -> ${mixed}/Tracks/TRACKB1003.lzc\n;  Global/GLOBALB.BUN -> ${mixed}/Global/GLOBALB.BUN\n;read 434536 bytes in 5 reads and 5 directory listings\n"
          "-> ${mixed}/Tracks/STREAML1RA_1003.BUN;cannot be listed"
          "${mixed}")

# The same, with the root given relative to the directory the command runs
# in: the same verdict, and the root echoed as it was given.
nfsu_case("a relative root" 3
          "asset-check: root mixed\n;install: MISSING FILES - 1 of 14 files ok: 10 missing, 1 wrong size, 2 wrong version, 0 unreadable\n;  Global/GLOBALB.BUN -> mixed/Global/GLOBALB.BUN\n"
          "cannot be listed"
          "mixed")

nfsu_case("the mixed install, event-1" 3
          "set event-1: race 1 of Underground mode: event 1, track 1003, stream L1RA, 7 files, 102128090 bytes\n;install: MISSING FILES - 1 of 7 files ok: 4 missing, 1 wrong size, 1 wrong version, 0 unreadable\n"
          "Cars/"
          "${mixed}" "--set" "event-1")

# ---------------------------------------------------------------------------
# A stock-car install of the right sizes and the wrong bytes
# ---------------------------------------------------------------------------

set(car "${work}/car")
nfsu_write_letters("${car}/Cars/240SX/GEOMETRY.BIN" "a" 4780816)
nfsu_write_letters("${car}/Cars/240SX/TEXTURES.BIN" "b" 697340)
nfsu_write_letters("${car}/Cars/BRAKES/GEOMETRY.BIN" "c" 69408)
nfsu_write_letters("${car}/Cars/PLATES/GEOMETRY.BIN" "d" 1184)
nfsu_write_letters("${car}/Cars/TEXTURES.BIN" "e" 1230808)
nfsu_write_letters("${car}/Global/GLOBALB.BUN" "f" 2361176)
nfsu_upper_hash("${car}/Cars/240SX/GEOMETRY.BIN" geometry_hash)

# Every byte of every file read, in chunks of at most 1 MiB, and one probe
# each: 5 + 1 + 1 + 1 + 2 + 3 chunks and 6 probes.
nfsu_case("every file the right size, none the right bytes" 3
          "WRONG VERSION Cars/240SX/GEOMETRY.BIN  4780816 bytes, sha256 ${geometry_hash} expected 002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619\n;install: WRONG VERSION - 0 of 6 files ok: 0 missing, 0 wrong size, 6 wrong version, 0 unreadable\n;copy these 6 files;read 9140732 bytes in 19 reads and 6 directory listings\n"
          "MISSING;WRONG SIZE;ok  "
          "${car}" "--set" "stock-car")

# One file cut short makes the install partial, and that file is not read.
nfsu_write_letters("${car}/Cars/TEXTURES.BIN" "e" 10)
nfsu_case("one file cut short" 3
          "WRONG SIZE    Cars/TEXTURES.BIN  10 bytes, expected 1230808: shorter than the retail file: an interrupted copy?\n;install: PARTIAL - 0 of 6 files ok: 0 missing, 1 wrong size, 5 wrong version, 0 unreadable\n;read 7909924 bytes in 16 reads and 6 directory listings\n"
          "MISSING"
          "${car}" "--set" "stock-car")

# A directory where a file should be is unreadable, and is read not at all.
file(REMOVE "${car}/Cars/PLATES/GEOMETRY.BIN")
file(MAKE_DIRECTORY "${car}/Cars/PLATES/GEOMETRY.BIN")
nfsu_case("a directory in place of a file" 3
          "UNREADABLE    Cars/PLATES/GEOMETRY.BIN  is a directory, not a file\n;install: PARTIAL - 0 of 6 files ok: 0 missing, 1 wrong size, 4 wrong version, 1 unreadable\n;read 7908740 bytes in 14 reads and 6 directory listings\n"
          "MISSING"
          "${car}" "--set" "stock-car")

# The command wrote nothing: the only files under the work directory are the
# ones this script wrote.
file(GLOB_RECURSE left LIST_DIRECTORIES false RELATIVE "${work}" "${work}/*")
list(SORT left)
set(expected_left
    "a-file"
    "car/Cars/240SX/GEOMETRY.BIN" "car/Cars/240SX/TEXTURES.BIN"
    "car/Cars/BRAKES/GEOMETRY.BIN" "car/Cars/TEXTURES.BIN" "car/Global/GLOBALB.BUN"
    "mixed/Cars/PLATES/GEOMETRY.BIN" "mixed/Global/GLOBALB.BUN"
    "mixed/tracks/STREAML1RA_1003.bun" "mixed/tracks/trackb1003.LZC")
list(SORT expected_left)
if(NOT left STREQUAL expected_left)
    nfsu_fail("the work directory holds files this script did not write:\n${left}")
endif()

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} asset-check guard case(s) failed")
endif()
message(STATUS "asset-check guards: every case passed")
