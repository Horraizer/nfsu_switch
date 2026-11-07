# Original40DD00 projection; same live smoke counts and simulation state.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(work "${WORK_ROOT}/race-particle-frame-${suffix}")
if(EXISTS "${work}")
    message(FATAL_ERROR "Unique particle frame output already exists")
endif()
execute_process(COMMAND "${RACE_EXE}" --root "${ROOT}"
    --input "${REPLAY}" --particles=on --particle-events --out "${work}" --frames 286
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Retail particle replay failed: ${result}\n${output}\n${error}")
endif()
# This is a real controller-only parking stroke, not injected particle state.
# Particle-on/off harvesting verifies a visible effect with identical simulation.
if(NOT output MATCHES "particle tick 286 physics 492 smoke 4 sparks 0 impulses 0")
    message(FATAL_ERROR "Retail smoke witness lost actual live-particle coverage\n${output}")
endif()
if(NOT EXISTS "${work}/frame-286.tga")
    message(FATAL_ERROR "Retail smoke frame was not written\n${output}")
endif()
file(SHA256 "${work}/frame-286.tga" frame_hash)
if(NOT frame_hash STREQUAL "e284202aadfe744e06eb81a18ba7538a2b887c31f1cef2fd0970026d3b9bb593")
    message(FATAL_ERROR "Retail smoke frame changed: ${frame_hash}\n${output}")
endif()
if(NOT output MATCHES "state-sha256 70D6FA38CCAED185EA55C4482E249C163152B05885E055BF1B7CB965BDAE06AD")
    message(FATAL_ERROR "Retail replay state changed\n${output}")
endif()
file(REMOVE_RECURSE "${work}")
message("PASS: retail smoke frame and replay state hashes")
