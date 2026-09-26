# The fused multiply-add census of the race route's console objects (issue
# #61). Every object in OBJECTS is disassembled with OBJDUMP; the run fails
# on any fused instruction - fmadd, fmsub, fnmadd, fnmsub, and the vector
# fmla and fmls - and also when the objects hold no fmul at all, so that an
# empty or wrong object list cannot pass by having nothing to fuse.
#
# Why it matters: docs/RACE_ROUTE.md pins the route's percents and the
# replay's step verdicts byte for byte, the desktop's x86-64 target has no
# FMA to fuse with, and devkitA64's GCC fuses at -O2 unless contraction is
# disabled - which cmake/race_libraries.cmake does for both libraries.
#
# A local copy, written for this branch: PR #169 carries a census of its
# own for nfsu_vehicle, and this file does not depend on it.
#
#   cmake -DOBJDUMP=<aarch64 objdump> -DOBJECTS=<a;b;...> -DLABEL=<name>
#         -P tests/race_route_fma_census.cmake

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED OBJDUMP OR NOT DEFINED OBJECTS OR NOT DEFINED LABEL)
    message(FATAL_ERROR "OBJDUMP, OBJECTS and LABEL must all be defined")
endif()
if(NOT EXISTS "${OBJDUMP}")
    message(FATAL_ERROR "${LABEL}: the objdump ${OBJDUMP} does not exist")
endif()

set(objects ${OBJECTS})
list(LENGTH objects object_count)
if(object_count LESS 1)
    message(FATAL_ERROR "${LABEL}: no object to census")
endif()

set(fused_total 0)
set(multiply_total 0)
foreach(object IN LISTS objects)
    if(NOT EXISTS "${object}")
        message(FATAL_ERROR "${LABEL}: the object ${object} does not exist")
    endif()
    execute_process(
        COMMAND "${OBJDUMP}" -d "${object}"
        OUTPUT_VARIABLE listing
        ERROR_VARIABLE listing_error
        RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${LABEL}: ${OBJDUMP} -d ${object} failed (${status}):\n${listing_error}")
    endif()
    string(REGEX MATCHALL "\t(fmadd|fmsub|fnmadd|fnmsub|fmla|fmls)[ \t]" fused "${listing}")
    string(REGEX MATCHALL "\tfmul[ \t]" multiplies "${listing}")
    list(LENGTH fused fused_count)
    list(LENGTH multiplies multiply_count)
    get_filename_component(name "${object}" NAME)
    message(STATUS "${LABEL}: ${name}: ${multiply_count} fmul, ${fused_count} fused")
    math(EXPR fused_total "${fused_total}+${fused_count}")
    math(EXPR multiply_total "${multiply_total}+${multiply_count}")
endforeach()

if(multiply_total EQUAL 0)
    message(FATAL_ERROR
        "${LABEL}: the objects hold no fmul at all, so this census would pass "
        "them without having looked at any floating-point code")
endif()
if(fused_total GREATER 0)
    message(FATAL_ERROR
        "${LABEL}: ${fused_total} fused multiply-add instruction(s); compile "
        "with -ffp-contract=off -fno-fast-math")
endif()
message(STATUS "${LABEL}: ${object_count} object(s), ${multiply_total} fmul, no fused instruction")
