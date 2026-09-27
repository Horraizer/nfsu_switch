# Optional retail decode pin. The files are read only, and both manifests
# cover all files in the three audio directories, including companions.
if(NOT DEFINED ENV{NFSU_RETAIL_ROOT} OR "$ENV{NFSU_RETAIL_ROOT}" STREQUAL "")
    message("SKIPPED: NFSU_RETAIL_ROOT is unset")
    return()
endif()

set(retail "$ENV{NFSU_RETAIL_ROOT}")
file(GLOB_RECURSE retail_files LIST_DIRECTORIES false
    "${retail}/Sound/*" "${retail}/SNDSTREAMS/*"
    "${retail}/SpeechStreams/*")
list(SORT retail_files)
if(NOT retail_files)
    message(FATAL_ERROR "No retail audio files found")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789ABCDEF runid)
set(run_dir "${WORK}/audio-retail-${runid}")
file(MAKE_DIRECTORY "${run_dir}")

function(write_manifest target)
    set(lines "")
    foreach(path IN LISTS retail_files)
        file(SHA256 "${path}" digest)
        file(SIZE "${path}" size)
        file(RELATIVE_PATH relative "${retail}" "${path}")
        string(APPEND lines "${relative} ${size} ${digest}\n")
    endforeach()
    file(WRITE "${target}" "${lines}")
endfunction()

write_manifest("${run_dir}/before.txt")
execute_process(
    COMMAND "${AUDIO_DUMP}" "${retail}" "${run_dir}/wav-output"
            --verify "${EXPECTED}"
    RESULT_VARIABLE decoded_result
    OUTPUT_VARIABLE decoded_output
    ERROR_VARIABLE decoded_error)
write_manifest("${run_dir}/after.txt")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files
            "${run_dir}/before.txt" "${run_dir}/after.txt"
    RESULT_VARIABLE manifest_result)
if(NOT manifest_result EQUAL 0)
    message(FATAL_ERROR "Retail audio files changed during decode")
endif()
if(NOT decoded_result EQUAL 0)
    message(FATAL_ERROR "Retail decode failed: ${decoded_error}")
endif()
message("Retail PCM pin and before/after manifest passed: ${decoded_output}")
