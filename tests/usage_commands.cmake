# nfsu_probe registers a command in exactly one place: the kCommands table in
# src/main.cpp, which holds the dispatch name, the usage text and the handler
# in one entry. That is what makes "dispatched but not listed", the defect
# this check exists for, something a new command cannot arrive with by
# accident. This check proves the result from outside, against the built
# program: every command the usage text lists is dispatched, the listing is
# the whole expected inventory and lists nothing twice, and a command that is
# not registered is refused as unknown.
#
# The last part is what makes the first part mean anything. A command that is
# listed but not dispatched falls through to the same usage text a command
# that refuses its arguments prints, so the two are indistinguishable until
# the unknown case says which one it is.
#
# Run as: cmake -DPROBE=<path to nfsu_probe> -P usage_commands.cmake

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE is not set: pass -DPROBE=<path to nfsu_probe>")
endif()
if(NOT EXISTS "${PROBE}")
    message(FATAL_ERROR "PROBE does not name an existing file: ${PROBE}")
endif()

# Every command the program is expected to register. The inventory is
# declared here as well as in the table on purpose: a command added to the
# table alone fails this check, and the failure is the reminder to confirm
# that the new command is meant to be part of the published interface.
set(expected
    summary
    chunks
    meshes
    obj
    textures
    pack-set
    export-textures
    texture-bindings
    car-parts
    stock-parts
    track-region
    car-placement
    part-markers
    body-skin-inputs)

# An input path that does not exist, so each command refuses its input
# instead of reading the installation. Every command that writes anything
# needs more arguments than the two passed here and refuses before it writes.
set(missing_input "nfsu_probe_usage_check_missing_input")

# Both streams are captured together: which stream carries a refusal is not
# what this check is about.
function(run_probe out_var code_var)
    execute_process(
        COMMAND "${PROBE}" ${ARGN}
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        RESULT_VARIABLE code)
    set(${out_var} "${out}${err}" PARENT_SCOPE)
    set(${code_var} "${code}" PARENT_SCOPE)
endfunction()

run_probe(usage usage_code)
if(NOT usage_code EQUAL 1)
    message(FATAL_ERROR
        "nfsu_probe with no arguments exited ${usage_code}, expected 1:\n${usage}")
endif()
if(NOT usage MATCHES "Usage:")
    message(FATAL_ERROR "nfsu_probe with no arguments printed no usage text:\n${usage}")
endif()

string(REPLACE "\r\n" "\n" usage_lines "${usage}")
string(REPLACE ";" "\;" usage_lines "${usage_lines}")
string(REPLACE "\n" ";" usage_lines "${usage_lines}")

set(listed "")
foreach(line IN LISTS usage_lines)
    if(line MATCHES "^  nfsu_probe ([a-z0-9-]+)")
        set(name "${CMAKE_MATCH_1}")
        if(name IN_LIST listed)
            message(FATAL_ERROR "the usage text lists '${name}' more than once")
        endif()
        list(APPEND listed "${name}")
    endif()
endforeach()

if(NOT listed)
    message(FATAL_ERROR
        "no command line was recognized in the usage text:\n${usage}")
endif()

set(listed_sorted ${listed})
list(SORT listed_sorted)
set(expected_sorted ${expected})
list(SORT expected_sorted)
if(NOT "${listed_sorted}" STREQUAL "${expected_sorted}")
    set(unlisted ${expected_sorted})
    list(REMOVE_ITEM unlisted ${listed_sorted})
    set(unexpected ${listed_sorted})
    list(REMOVE_ITEM unexpected ${expected_sorted})
    message(FATAL_ERROR
        "the usage text does not list the expected commands.\n"
        "  expected but not listed: ${unlisted}\n"
        "  listed but not expected: ${unexpected}\n"
        "  listed: ${listed}")
endif()

foreach(name IN LISTS listed)
    run_probe(out code "${name}" "${missing_input}")
    if(out MATCHES "unknown command")
        message(FATAL_ERROR
            "the usage text lists '${name}' but nfsu_probe does not dispatch it:\n${out}")
    endif()
endforeach()

set(unregistered "nfsu-probe-no-such-command")
run_probe(out code "${unregistered}" "${missing_input}")
if(NOT out MATCHES "unknown command '${unregistered}'")
    message(FATAL_ERROR
        "nfsu_probe did not report '${unregistered}' as an unknown command, so the "
        "check above cannot tell a dispatched command from a listed one:\n${out}")
endif()
if(NOT code EQUAL 1)
    message(FATAL_ERROR
        "nfsu_probe exited ${code} for the unknown command '${unregistered}', expected 1:\n${out}")
endif()

list(LENGTH listed listed_count)
message(STATUS "nfsu_probe: ${listed_count} commands listed and dispatched: ${listed}")
