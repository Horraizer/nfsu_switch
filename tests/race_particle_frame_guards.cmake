# Original40DD00 projection and original wheel-slip admission.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(work "${WORK_ROOT}/race-particle-frame-${suffix}")
if(EXISTS "${work}")
    message(FATAL_ERROR "Unique particle frame output already exists")
endif()
execute_process(COMMAND "${RACE_EXE}" --root "${ROOT}"
    --input "${REPLAY}" --particles=on --particle-events --out "${work}" --frames 295
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Retail particle replay failed: ${result}\n${output}\n${error}")
endif()
# This is a real controller-only parking stroke, not injected particle state.
# Particle-on/off harvesting verifies a visible effect with identical simulation.
if(NOT output MATCHES "particle tick 295 physics 528 smoke 4 sparks 0 impulses 0")
    message(FATAL_ERROR "Retail smoke witness lost actual live-particle coverage\n${output}")
endif()
if(NOT EXISTS "${work}/frame-295.tga")
    message(FATAL_ERROR "Retail smoke frame was not written\n${output}")
endif()
file(SHA256 "${work}/frame-295.tga" frame_hash)
if(NOT frame_hash STREQUAL "c73f1808727569bd7c4c21e8f9a61f9443ba6360caa1ede34633f2b75302deb7")
    message(FATAL_ERROR "Retail smoke frame changed: ${frame_hash}\n${output}")
endif()
if(NOT output MATCHES "state-sha256 70D6FA38CCAED185EA55C4482E249C163152B05885E055BF1B7CB965BDAE06AD")
    message(FATAL_ERROR "Retail replay state changed\n${output}")
endif()
file(REMOVE_RECURSE "${work}")
message("PASS: retail smoke frame and replay state hashes")
