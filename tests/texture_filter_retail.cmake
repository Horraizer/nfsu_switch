# Four fresh reference runs: both filter modes, shading off/on, fixed poses.
cmake_minimum_required(VERSION 3.20)
foreach(required PYTHON SCRIPT RACE ROOT WORK EXPECTED RECORDING COMPARE REFERENCES)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "FAIL: texture_filter_retail requires ${required}")
    endif()
endforeach()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(output "${WORK}/texture-filter-${run_id}")
execute_process(COMMAND "${PYTHON}" "${SCRIPT}" "${ROOT}" "${output}"
    --compare "${COMPARE}" --references "${REFERENCES}"
    --race "${RACE}" --recording "${RECORDING}" --frames 180,6000 --expected "${EXPECTED}"
    RESULT_VARIABLE result OUTPUT_VARIABLE text ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT error STREQUAL "" OR
   NOT text MATCHES "PASS: four fresh replays" OR
   NOT text MATCHES "PASS: twelve native reference images" OR
   NOT text MATCHES "PASS: before/after retail file manifests equal")
    message(FATAL_ERROR "FAIL: texture filter comparison ${result}\n${text}\n${error}")
endif()
message(STATUS "${text}")
file(REMOVE_RECURSE "${output}")
