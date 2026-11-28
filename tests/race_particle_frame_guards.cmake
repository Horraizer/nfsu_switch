# Original40DD00 projection and controller-only wheel-slip admission.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(work "${WORK_ROOT}/race-particle-frame-${suffix}")
if(EXISTS "${work}")
    message(FATAL_ERROR "Unique particle frame output already exists")
endif()
file(MAKE_DIRECTORY "${work}")
foreach(effects on off)
    execute_process(COMMAND "${RACE_EXE}" --root "${ROOT}"
        --input "${REPLAY}" --particles=${effects} --particle-events
        --out "${work}/${effects}" --frames 295 --frame-witness
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Retail particle witness failed (${effects}): ${result}\n${output}\n${error}")
    endif()
    if(NOT output MATCHES "frame-witness-complete 295" OR
       NOT output MATCHES "outcome running steps 295 resets 0" OR
       NOT EXISTS "${work}/${effects}/frame-295.tga")
        message(FATAL_ERROR "Retail particle witness did not complete its exact fresh frame\n${output}")
    endif()
    # Both executions must preserve the independently checked base prefix.
    if(NOT output MATCHES "state-sha256 CB7148E446DE0805510B973E05867F1CD99CE587C58FE9EFD0B8BF5F4DB09BA7")
        message(FATAL_ERROR "Retail controller prefix changed (${effects})\n${output}")
    endif()
    if(effects STREQUAL "on" AND NOT output MATCHES "particle tick 295 physics 528 smoke 10 sparks 0 impulses 0")
        message(FATAL_ERROR "Retail smoke witness lost actual live-particle coverage\n${output}")
    endif()
    file(SHA256 "${work}/${effects}/frame-295.tga" frame_${effects})
endforeach()
# Original53E290 direction gate removes backwards STARTLINE instance2603:109.
# Restoring only that instance recreates the previous R6 frames exactly.
# The30 changed background pixels do not overlap the4001 smoke pixels;
# signed RGB(on-off) and the controller/live-particle checks above are unchanged.
if(NOT frame_on STREQUAL "e573dea757a56b0ec7183a2eb32887e3ae267c06cb17b270020b62a7b641d567")
    message(FATAL_ERROR "Retail smoke frame changed: ${frame_on}")
endif()
if(NOT frame_off STREQUAL "eb154eeb31e71d21d1e26b9be425412e69c1d7f7b93138c2825effc28cbdb981")
    message(FATAL_ERROR "Retail no-particle scene changed: ${frame_off}")
endif()
if(frame_on STREQUAL frame_off)
    message(FATAL_ERROR "Retail smoke frame has no visible particle contribution")
endif()
file(REAL_PATH "${WORK_ROOT}" root_real)
file(REAL_PATH "${work}" work_real)
cmake_path(IS_PREFIX root_real "${work_real}" NORMALIZE inside)
if(NOT inside OR work_real STREQUAL root_real)
    message(FATAL_ERROR "Particle witness cleanup escaped its own work root")
endif()
file(REMOVE_RECURSE "${work_real}")
message("PASS: exact retail smoke witness, visible on/off difference and unchanged controller prefix")
