# tools/race_replay_check.py under CTest (issue #233): the 12 pinned race
# frames, the result lines, the per-step state file, the 120 Hz trajectory
# and, with --check-driver, the regenerated recording, for one nfsu_race.
# The script's assertions are its own; this wrapper adds none and relaxes
# none. It only gives the script the fresh output directory it insists on.
#
#   -DPYTHON=<interpreter> -DSCRIPT=<tools/race_replay_check.py>
#   -DRACE=<nfsu_race executable> -DROOT=<retail root>
#   -DOUT_PARENT=<this build directory's nfsu_retail directory>
#
# The script refuses an output directory that already exists, so that an
# earlier run cannot pass for this one. OUT is a new directory of this
# process's own under OUT_PARENT, named with a random suffix, so neither a
# second build directory nor a second ctest run in this one shares it (issue
# #122). It is removed by its exact name after a pass and kept after a
# failure, for its logs.

cmake_minimum_required(VERSION 3.20)

foreach(required PYTHON SCRIPT RACE ROOT OUT_PARENT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "FAIL: retail_replay_check.cmake needs -D${required}")
    endif()
endforeach()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(OUT "${OUT_PARENT}/race-replay-check-${run_id}")
if(EXISTS "${OUT}")
    message(FATAL_ERROR "FAIL: the output ${OUT} already exists")
endif()
file(MAKE_DIRECTORY "${OUT_PARENT}")

set(effect_arguments)
if(DEFINED GLOWS AND GLOWS)
    list(APPEND effect_arguments --glows)
endif()
if(CAR_SHADING)
    list(APPEND effect_arguments --car-shading)
    set(expected_frames 1)
else()
    set(expected_frames 12)
endif()
set(probe_arguments)
if(DEFINED PROBE AND NOT PROBE STREQUAL "")
    list(APPEND probe_arguments --probe "${PROBE}")
endif()

execute_process(
    COMMAND "${PYTHON}" "${SCRIPT}" --exe "${RACE}" --root "${ROOT}"
            --out "${OUT}" --check-driver ${probe_arguments} ${effect_arguments}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
message("${output}")
if(NOT errors STREQUAL "")
    message("${errors}")
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: race_replay_check.py exited ${result}")
endif()
# The script prints one PASS line per executable when every pin held; an
# exit status of 0 without it is not a pass.
if(NOT output MATCHES "(^|\n)PASS [^\n]*48336 step hashes, result, ${expected_frames} frames, clean 120 Hz trajectory, generated recording")
    message(FATAL_ERROR "FAIL: race_replay_check.py exited 0 without its PASS line")
endif()
if(NOT output MATCHES "PASS wet replay:")
    message(FATAL_ERROR "FAIL: missing paired wet replay checks")
endif()
if(DEFINED PROBE AND NOT PROBE STREQUAL "" AND
   NOT output MATCHES "PASS wet hand path:")
    message(FATAL_ERROR "FAIL: missing paired wet hand path checks")
endif()
file(REMOVE_RECURSE "${OUT}")
