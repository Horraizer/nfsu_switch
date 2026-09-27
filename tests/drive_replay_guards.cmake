# What `nfsu_probe drive-replay` refuses, prints and exits with, and the frames
# it renders.
#
# The argument cases need no input file. The stream cases drive the command
# over the built-in demonstration and over files nfsu_drive_tests writes on
# request - the demonstration in the recorded form of docs/INPUT.md, and the
# default controls in the remapping file's form - and over files this script
# writes itself, because both forms are text. The frame cases run over the
# six synthetic stock-frame inputs nfsu_stock_frame_tests writes, which is
# the only way to reach --root without retail data; the retail frames are
# pinned in docs/CONTROLS_CAMERA.md instead.
#
# What is pinned: the stream hash of the demonstration, which
# nfsu_drive_tests also requires, reached three ways (built in, the recorded
# file, the recorded file with the default controls given as a file); the
# two synthetic frames, each hash equal to the SHA-256 CMake's own
# file(SHA256) takes of the TGA the command wrote; and every exit status and
# diagnostic. This test runs in every desktop build, so the pins hold in each
# of them.
#
# Exit statuses: 0 a run, 1 an argument the command refuses, 2 a recording
# or controls file that cannot be read or parsed, 3 a root that cannot give
# the stock frame, 4 a step refused mid-run. 4 has no case: no recording the
# parser accepts reaches it, which nfsu_drive_tests' session cases stand for.
#
# A script rather than add_test properties for the reason
# tests/track_render_guards.cmake gives: PASS_REGULAR_EXPRESSION makes CTest
# ignore the exit status, and the status is half of what is pinned here.
#
#   cmake -DPROBE=<nfsu_probe> -DFIXTURE=<nfsu_stock_frame_tests>
#         -DWORK=<a directory this test owns> -P tests/drive_replay_guards.cmake
#
# nfsu_drive_tests is found beside the probe, in the same build directory.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS PROBE FIXTURE WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()
get_filename_component(probe_dir "${PROBE}" DIRECTORY)
get_filename_component(probe_ext "${PROBE}" LAST_EXT)
set(TESTS "${probe_dir}/nfsu_drive_tests${probe_ext}")
if(NOT EXISTS "${TESTS}")
    message(FATAL_ERROR "nfsu_drive_tests was not found beside ${PROBE}")
endif()

# The pins.
set(demo_stream "7030E676B13DD4F09525EFEF5D7A076C395F401C3692AD2F72D89B1871D275D9")
set(fixture_frame_690 "ED60C57AF156E5A8E2850A85DAF8998F883DBE1EF34A15465BB1C8E23B1D3FB3")
set(fixture_frame_1380 "F9CAA668F9ADAA7F495B037261586EF4DB3A16BDA695D0CB46C55B3856C1F313")

file(REMOVE_RECURSE "${WORK}")
if(EXISTS "${WORK}")
    message(FATAL_ERROR "${WORK} could not be removed before the run")
endif()
file(MAKE_DIRECTORY "${WORK}")

set(failures 0)

function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()

# One case. `code` is the exact exit status required, `needles` a list of
# texts the combined output must each contain and `banned` a list it must
# not; the rest is the command line after "drive-replay". The run's stream
# hash, if it printed one, is left in ${last_stream} and its standard output
# in ${last_output}.
function(nfsu_case label code needles banned)
    execute_process(
        COMMAND "${PROBE}" drive-replay ${ARGN}
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE status)
    string(REPLACE "\r\n" "\n" stdout_text "${stdout_text}")
    string(REPLACE "\r\n" "\n" stderr_text "${stderr_text}")
    set(text "${stdout_text}${stderr_text}")
    set(last_output "${stdout_text}" PARENT_SCOPE)
    set(last_stream "" PARENT_SCOPE)
    if(stdout_text MATCHES "stream: frames [0-9]+ bytes [0-9]+ sha256 ([0-9A-F]+)\n")
        set(last_stream "${CMAKE_MATCH_1}" PARENT_SCOPE)
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
    if(code EQUAL 0 AND NOT stderr_text STREQUAL "")
        nfsu_fail("${label}: a run that succeeded wrote to standard error:\n${stderr_text}")
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
set(ran "drive-replay: recording;stream: frames")

# ---------------------------------------------------------------------------
# Arguments, no input files
# ---------------------------------------------------------------------------

nfsu_case("no arguments: the usage" 1 "nfsu_probe drive-replay --demo | --input <FILE>" "${ran}")
nfsu_case("no stream at all" 1 "error: give either --demo or --input <FILE>" "${ran}"
          --every 0)
nfsu_case("both streams" 1 "error: give either --demo or --input <FILE>" "${ran}"
          --demo --input "${WORK}/never_read.txt")
nfsu_case("an unknown option" 1 "error: unknown option --speed" "${ran}" --demo --speed 3)
nfsu_case("an option twice" 1 "error: --every is given twice" "${ran}"
          --demo --every 1 --every 2)
nfsu_case("an option without its value" 1 "error: --frames needs a value" "${ran}"
          --demo --frames)
nfsu_case("a POV that is not a chase view" 1 "error: --pov must be a chase view: 0, 1 or 4"
          "${ran}" --demo --pov 2)
nfsu_case("no frames" 1 "error: --frames must be a whole number from 1 to 10000000" "${ran}"
          --demo --frames 0)
nfsu_case("a render step of zero" 1
          "error: --render-every must be a whole number from 1 to 10000000" "${ran}"
          --demo --root "${WORK}" --render-every 0)
nfsu_case("a width too large" 1 "error: --width needs a size from 1 to 8192" "${ran}"
          --demo --root "${WORK}" --width 9000)
nfsu_case("frames described without a root" 1
          "error: --render-every, --out, --width and --height describe the frames, which need --root"
          "${ran}" --demo --out "${WORK}/never")

# ---------------------------------------------------------------------------
# The stream
# ---------------------------------------------------------------------------

nfsu_case("the demonstration" 0
          "drive-replay: recording the built-in demonstration frames 1380 replayed 1380 of 1/60 s, 2 vehicle steps each;controls: the default Switch layout, 10 bindings;camera: chase POV 0 fov 13020 units, 71.521 degrees across a 4:3 view, its height kept;final frame 1380 t 23.000;stream: frames 1380 bytes 1689120 sha256 ${demo_stream}"
          "frame 120:" --demo --every 0)
nfsu_case("a line every 690 frames" 0
          "at frame 690 t 11.500;final frame 1380 t 23.000;stream: frames 1380 bytes 1689120 sha256 ${demo_stream}"
          "at frame 1380" --demo --every 690)
nfsu_case("the first 600 frames" 0 "stream: frames 600 bytes 734400 sha256" "stream: frames 1380"
          --demo --frames 600 --every 0)
if(last_stream STREQUAL demo_stream OR last_stream STREQUAL "")
    nfsu_fail("the first 600 frames must hash differently from all 1380")
endif()

execute_process(COMMAND "${TESTS}" --write-demo "${WORK}/demo.txt" RESULT_VARIABLE written)
execute_process(COMMAND "${TESTS}" --write-controls "${WORK}/controls.txt"
                RESULT_VARIABLE controls_written)
if(NOT written EQUAL 0 OR NOT controls_written EQUAL 0)
    nfsu_fail("nfsu_drive_tests did not write the demonstration and the controls")
endif()
nfsu_case("the demonstration's recorded file" 0
          "drive-replay: recording demo.txt frames 1380;stream: frames 1380 bytes 1689120 sha256 ${demo_stream}"
          "" --input "${WORK}/demo.txt" --every 0)
nfsu_case("the recorded file with the default controls as a file" 0
          "controls: controls.txt, 10 bindings;stream: frames 1380 bytes 1689120 sha256 ${demo_stream}"
          "" --input "${WORK}/demo.txt" --controls "${WORK}/controls.txt" --every 0)

file(WRITE "${WORK}/remapped.txt"
     "nfsu-controls 1\n# the throttle on A: the demonstration's ZR drives nothing\n"
     "bind throttle face_east\nbind brake trigger_left\nbind steer left_x\n")
nfsu_case("a remapped throttle" 0 "controls: remapped.txt, 3 bindings" "sha256 ${demo_stream}"
          --demo --controls "${WORK}/remapped.txt" --every 0)
nfsu_case("POV 4 at the start" 0 "camera: chase POV 4 fov 13020" "sha256 ${demo_stream}"
          --demo --pov 4 --every 0)

file(WRITE "${WORK}/bad_action.txt" "nfsu-controls 1\nbind jump face_east\n")
nfsu_case("a controls file with an unknown action" 2
          "error: controls bad_action.txt: line 2: bind: unknown action 'jump'" "${ran}"
          --demo --controls "${WORK}/bad_action.txt")
file(WRITE "${WORK}/bad_axis.txt" "nfsu-controls 1\nbind handbrake right_x\n")
nfsu_case("a controls file with a stick on the handbrake" 2
          "error: controls bad_axis.txt: line 2: bind: right_x is a stick axis and cannot drive handbrake"
          "${ran}" --demo --controls "${WORK}/bad_axis.txt")
file(WRITE "${WORK}/bad_header.txt" "nfsu-controls 7\n")
nfsu_case("a controls file with the wrong header" 2
          "error: controls bad_header.txt: line 1: the header must be 'nfsu-controls 1'" "${ran}"
          --demo --controls "${WORK}/bad_header.txt")
nfsu_case("a controls file that is not there" 2 "error: cannot open the controls file" "${ran}"
          --demo --controls "${WORK}/absent.txt")
file(WRITE "${WORK}/bad_recording.txt" "nfsu-input 1\n16000 0 - 0 0 0 0\n0 0 - 0 0 0 0\n")
nfsu_case("a recording whose time goes backwards" 2
          "error: recording bad_recording.txt: line 3" "${ran}"
          --input "${WORK}/bad_recording.txt")
nfsu_case("a recording that is not there" 2 "error: cannot open the recording" "${ran}"
          --input "${WORK}/absent.txt")

# ---------------------------------------------------------------------------
# The frames, over the synthetic stock-frame inputs
# ---------------------------------------------------------------------------

nfsu_case("a root without the stock-frame files" 3
          "Cars/240SX/GEOMETRY.BIN: failed to open file" "${ran}"
          --demo --root "${WORK}/empty")

execute_process(COMMAND "${FIXTURE}" --write-fixture "${WORK}/root"
                RESULT_VARIABLE fixture_result OUTPUT_QUIET ERROR_QUIET)
if(NOT fixture_result EQUAL 0)
    nfsu_fail("nfsu_stock_frame_tests did not write the synthetic inputs")
endif()
set(prefix "${WORK}/shots/drive")
file(MAKE_DIRECTORY "${WORK}/shots")
nfsu_case("two synthetic frames" 0
          "stock car: 240SX painted;camera anchors: 240SX record +0xB50 differs from the built-in 240SX table;frames: rendered 2 written 2;stream: frames 1380 bytes 1689120 sha256 ${demo_stream}"
          "" --demo --every 0 --root "${WORK}/root" --render-every 690 --width 160 --height 90
          --out "${prefix}")
set(frames_output "${last_output}")
foreach(frame IN ITEMS 690 1380)
    set(padded "000${frame}")
    string(LENGTH "${padded}" length)
    math(EXPR start "${length}-6")
    string(SUBSTRING "${padded}" ${start} 6 padded)
    set(file "${prefix}-${padded}.tga")
    if(NOT EXISTS "${file}")
        nfsu_fail("frame ${frame} was not written to ${file}")
        continue()
    endif()
    file(SHA256 "${file}" file_hash)
    string(TOUPPER "${file_hash}" file_hash)
    if(NOT frames_output MATCHES
       "frame ${frame}: 160x90 car pixels ([0-9]+) ground triangles [0-9]+ sha256 ([0-9A-F]+) wrote drive-${padded}.tga")
        nfsu_fail("frame ${frame}: no report line in\n${frames_output}")
        continue()
    endif()
    set(car_pixels "${CMAKE_MATCH_1}")
    set(printed "${CMAKE_MATCH_2}")
    if(car_pixels LESS 100)
        nfsu_fail("frame ${frame} shows almost none of the car: ${car_pixels} pixels")
    endif()
    if(NOT printed STREQUAL file_hash)
        nfsu_fail("frame ${frame}: the printed hash ${printed} is not the file's ${file_hash}")
    endif()
    if(NOT printed STREQUAL "${fixture_frame_${frame}}")
        nfsu_fail("frame ${frame}: hash ${printed}, pinned ${fixture_frame_${frame}}")
    endif()
    file(SIZE "${file}" size)
    if(NOT size EQUAL 57618)
        nfsu_fail("frame ${frame}: ${size} bytes, a 160x90 TGA is 57618")
    endif()
endforeach()
nfsu_case("the frames already written" 1
          "already exists; this command never replaces a file" "${ran}"
          --demo --every 0 --root "${WORK}/root" --render-every 690 --width 160 --height 90
          --out "${prefix}")

if(failures GREATER 0)
    message(FATAL_ERROR "${failures} drive-replay guard(s) failed")
endif()
file(REMOVE_RECURSE "${WORK}")
message(STATUS "drive-replay: every guard held")
