# The fused multiply-add census of issue #91, as a test over the objects a
# Switch build wrote: every object named in OBJECTS is disassembled with
# OBJDUMP, and the run fails if any holds a fused instruction - fmadd,
# fmsub, fnmadd, fnmsub, or the vector fmla and fmls. It also fails when the
# objects hold no floating-point multiply at all, so that an empty or wrong
# object list cannot pass by having nothing to fuse.
#
# docs/SWITCH_BUILD.md explains why a fused instruction matters: this
# project pins byte-identical output across the desktop and the console,
# the desktop's x86-64 target has no FMA to fuse with, and devkitA64's GCC
# fuses at -O2 unless contraction is disabled. docs/VEHICLE.md gives the
# counts for nfsu_vehicle with and without its options.
#
#   cmake -DOBJDUMP=<aarch64 objdump> -DOBJECTS=<a;b;...> -DLABEL=<name>
#         -P tests/switch_fma_census.cmake

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

set(total_fused 0)
set(total_multiplies 0)
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
        message(FATAL_ERROR "${LABEL}: ${OBJDUMP} -d ${object} failed (${status}):\n${listing_error}")
    endif()
    string(REGEX MATCHALL "\t(fmadd|fmsub|fnmadd|fnmsub|fmla|fmls)[ \t]" fused "${listing}")
    string(REGEX MATCHALL "\tfmul[ \t]" multiplies "${listing}")
    list(LENGTH fused fused_count)
    list(LENGTH multiplies multiply_count)
    get_filename_component(name "${object}" NAME)
    message(STATUS "${LABEL}: ${name}: ${multiply_count} fmul, ${fused_count} fused")
    math(EXPR total_fused "${total_fused}+${fused_count}")
    math(EXPR total_multiplies "${total_multiplies}+${multiply_count}")
endforeach()

if(total_multiplies EQUAL 0)
    message(FATAL_ERROR
        "${LABEL}: the objects hold no fmul at all, so this census would pass "
        "them without having looked at any floating-point code")
endif()

if(total_fused GREATER 0)
    message(FATAL_ERROR
        "${LABEL}: ${total_fused} fused multiply-add instruction(s); the library "
        "must be compiled with -ffp-contract=off -fno-fast-math")
endif()
message(STATUS "${LABEL}: ${object_count} object(s), no fused instruction")
