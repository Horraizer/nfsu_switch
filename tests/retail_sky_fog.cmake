# nfsu_sky_fog_retail under CTest (issues #184 and #233): the event 1 sky
# inventory, the overview and street frame pins with the sky on and off, the
# unchanged world depth and the complete CPU/GPU draw trace. The executable's
# assertions are its own; this wrapper adds none and relaxes none.
#
#   -DSKY_FOG=<nfsu_sky_fog_retail executable> -DROOT=<retail root>
#   -DOUT_PARENT=<this build directory's nfsu_retail directory>
#
# The executable takes only a new directory under a relative out/, so it runs
# in a directory of this process's own under OUT_PARENT, named with a random
# suffix (issue #122). That directory is removed by its exact name after a
# pass and kept after a failure, for its frames.

cmake_minimum_required(VERSION 3.20)

foreach(required SKY_FOG ROOT OUT_PARENT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "FAIL: retail_sky_fog.cmake needs -D${required}")
    endif()
endforeach()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(WORK "${OUT_PARENT}/sky-fog-${run_id}")
if(EXISTS "${WORK}")
    message(FATAL_ERROR "FAIL: the work directory ${WORK} already exists")
endif()
file(MAKE_DIRECTORY "${WORK}/out")

execute_process(
    COMMAND "${SKY_FOG}" "${ROOT}" out/frames
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
message("${output}")
if(NOT errors STREQUAL "")
    message("${errors}")
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: nfsu_sky_fog_retail exited ${result}")
endif()
# An exit status of 0 without the PASS line is not a pass.
if(NOT output MATCHES "(^|\n)PASS: sky frames, unchanged world depth, complete CPU/GPU draw trace")
    message(FATAL_ERROR "FAIL: nfsu_sky_fog_retail exited 0 without its PASS line")
endif()
file(REMOVE_RECURSE "${WORK}")
