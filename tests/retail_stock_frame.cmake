# The stock 240SX frame over the retail files, through exactly the
# application's calls, under CTest (issue #233). docs/SWITCH_BUILD.md used to
# tell a reviewer to run `nfsu_stock_frame_tests --frame <root>` and
# `--painted-frame <root>` by hand and compare the printed hash with the pin;
# this script is that comparison. The pin is not copied here: it is read from
# the constants the application and nfsu_draw_commands_retail compare with,
# kStock240sx[Painted]Frame{Sha256,Bytes} in include/nfsu/stock_frame.h.
#
#   -DTESTS=<nfsu_stock_frame_tests> -DROOT=<retail root>
#   -DHEADER=<include/nfsu/stock_frame.h> -DPAINTED=0|1

cmake_minimum_required(VERSION 3.20)

foreach(required TESTS ROOT HEADER PAINTED)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "FAIL: retail_stock_frame.cmake needs -D${required}")
    endif()
endforeach()

if(PAINTED)
    set(mode --painted-frame)
    set(name kStock240sxPaintedFrame)
else()
    set(mode --frame)
    set(name kStock240sxFrame)
endif()

file(READ "${HEADER}" header)
if(NOT header MATCHES "${name}Sha256 =[ \r\n]*\"([0-9A-F]+)\";")
    message(FATAL_ERROR "FAIL: ${name}Sha256 not found in ${HEADER}")
endif()
set(pinned_sha "${CMAKE_MATCH_1}")
if(NOT header MATCHES "${name}Bytes = ([0-9]+);")
    message(FATAL_ERROR "FAIL: ${name}Bytes not found in ${HEADER}")
endif()
set(pinned_bytes "${CMAKE_MATCH_1}")

execute_process(
    COMMAND "${TESTS}" ${mode} "${ROOT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)
message("${output}")
if(NOT errors STREQUAL "")
    message("${errors}")
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "FAIL: nfsu_stock_frame_tests ${mode} exited ${result}")
endif()
if(NOT output MATCHES "(^|\n)sha256 ([0-9A-F]+)\r?\n")
    message(FATAL_ERROR "FAIL: nfsu_stock_frame_tests ${mode} printed no sha256 line")
endif()
set(sha "${CMAKE_MATCH_2}")
if(NOT output MATCHES "(^|\n)tga_bytes ([0-9]+)\r?\n")
    message(FATAL_ERROR "FAIL: nfsu_stock_frame_tests ${mode} printed no tga_bytes line")
endif()
set(bytes "${CMAKE_MATCH_2}")
if(NOT output MATCHES "(^|\n)view_default 1\r?\n" OR
   NOT output MATCHES "(^|\n)inputs_ok 1\r?\n")
    message(FATAL_ERROR "FAIL: ${mode}: the view is not at the default camera "
                        "or the inputs were not all read")
endif()
if(NOT sha STREQUAL pinned_sha OR NOT bytes STREQUAL pinned_bytes)
    message(FATAL_ERROR "FAIL: ${mode}: frame ${sha} ${bytes} bytes differs "
                        "from ${name} ${pinned_sha} ${pinned_bytes} bytes")
endif()
message("PASS: ${mode} matches ${name}Sha256 and ${name}Bytes")
