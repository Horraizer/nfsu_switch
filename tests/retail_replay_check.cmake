# tools/race_replay_check.py under CTest (issue #233): the 11 pinned race
# frames, the result lines, the per-step state file, the 120 Hz trajectory
# and, with --check-driver, the regenerated recording, for one nfsu_race.
# The script's assertions are its own; this wrapper adds none and relaxes
# none. It only gives the script the fresh output directory it insists on.
#
#   -DPYTHON=<interpreter> -DSCRIPT=<tools/race_replay_check.py>
#   -DRACE=<nfsu_race executable> -DROOT=<retail root>
#   -DOUT=<output directory inside this build directory>
#
# The script refuses an output directory that already exists, so that an
# earlier run cannot pass for this one. OUT is this test's own directory in
# its own build directory; it is removed by its exact name first, and the
# removal is confirmed before the script runs.

cmake_minimum_required(VERSION 3.20)

foreach(required PYTHON SCRIPT RACE ROOT OUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "FAIL: retail_replay_check.cmake needs -D${required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${OUT}")
if(EXISTS "${OUT}")
    message(FATAL_ERROR "FAIL: could not remove the earlier output ${OUT}")
endif()

execute_process(
    COMMAND "${PYTHON}" "${SCRIPT}" --exe "${RACE}" --root "${ROOT}"
            --out "${OUT}" --check-driver
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
if(NOT output MATCHES "(^|\n)PASS [^\n]*48336 step hashes, result, 11 frames, clean 120 Hz trajectory, generated recording")
    message(FATAL_ERROR "FAIL: race_replay_check.py exited 0 without its PASS line")
endif()
