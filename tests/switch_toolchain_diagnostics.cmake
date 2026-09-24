# What a Switch configure says when the SDK is not all there.
#
# cmake/switch-sdk.cmake checks every piece of devkitPro the Switch build
# uses and names the missing one: what it is, where it was looked for, the
# package that provides it and the variable that points the build at another
# SDK. This configures the real project with the Switch toolchain against SDK
# trees that each lack something, and requires each configure to fail with
# that message, reported once, at the check the top-level CMakeLists.txt
# makes before project(): no call stack, and none of the errors CMake adds
# after a failed toolchain file ("CMAKE_CXX_COMPILER not set", "unable to
# find a build program"), which would bury the one line that matters.
#
# The SDK trees are empty files at the paths the module checks, read out of
# the module itself so the two lists cannot disagree. No case gets as far as
# running a compiler, so this needs no SDK and runs in the desktop suite.
# Every configure runs with DEVKITPRO and DEVKITA64 removed from its
# environment, so the machine's own SDK cannot answer for a missing piece.
#
# The last case uses the toolchain file from a project that has no check of
# its own, which is the file's own fallback; CMake's follow-on errors are
# expected there and not asserted either way.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS SOURCE_DIR GENERATOR)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" token)
set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_toolchain_diagnostics_${token}")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
set(toolchain "${SOURCE_DIR}/cmake/switch-toolchain.cmake")
set(report "")
set(passed 0)
set(failed 0)
set(case_number 0)

include("${SOURCE_DIR}/cmake/switch-sdk.cmake")

# A fake SDK under <root>, with devkitA64 at <root>/devkitA64, holding every
# piece except those whose path is in ARGN.
function(nfsu_fake_sdk root)
    foreach(piece IN LISTS _nfsu_switch_sdk_pieces)
        string(REPLACE "|" ";" fields "${piece}")
        list(GET fields 1 path)
        list(GET fields 3 variable)
        if(path IN_LIST ARGN)
            continue()
        endif()
        if(variable STREQUAL "DEVKITA64")
            file(WRITE "${root}/devkitA64/${path}" "")
        else()
            file(WRITE "${root}/${path}" "")
        endif()
    endforeach()
endfunction()

# nfsu_case(<label> <source dir> <environment> <expected> <unexpected>
#           <arguments...>)
#
# <environment> is a |-separated list of NAME=VALUE the configure runs with,
# or "". <expected> and <unexpected> are |-separated lists of text the output
# must and must not contain.
function(nfsu_case label source environment expected unexpected)
    math(EXPR number "${case_number} + 1")
    set(case_number ${number} PARENT_SCOPE)
    set(build "${work}/build_${number}")
    string(REPLACE "|" ";" variables "${environment}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env --unset=DEVKITPRO --unset=DEVKITA64
                ${variables}
                ${CMAKE_COMMAND} -S ${source} -B ${build} -G ${GENERATOR}
                -DCMAKE_TOOLCHAIN_FILE=${toolchain} ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(output "${stdout}${stderr}")
    # Matched with whitespace runs collapsed, so the test does not depend on
    # how a CMake version indents or wraps an error message.
    string(REGEX REPLACE "[ \t\r\n]+" " " flat "${output}")
    set(problems "")
    if(status EQUAL 0)
        string(APPEND problems "  the configure succeeded\n")
    endif()
    string(REPLACE "|" ";" wanted "${expected}")
    foreach(text IN LISTS wanted)
        string(FIND "${flat}" "${text}" at)
        if(at EQUAL -1)
            string(APPEND problems "  missing from the output: ${text}\n")
        endif()
    endforeach()
    string(REPLACE "|" ";" unwanted "${unexpected}")
    foreach(text IN LISTS unwanted)
        string(FIND "${flat}" "${text}" at)
        if(NOT at EQUAL -1)
            string(APPEND problems "  in the output and must not be: ${text}\n")
        endif()
    endforeach()
    if(problems STREQUAL "")
        math(EXPR count "${passed} + 1")
        set(passed ${count} PARENT_SCOPE)
        set(report "${report}ok     ${label}\n" PARENT_SCOPE)
    else()
        math(EXPR count "${failed} + 1")
        set(failed ${count} PARENT_SCOPE)
        set(report "${report}FAILED ${label}\n${problems}  output:\n${output}\n" PARENT_SCOPE)
    endif()
endfunction()

# What every case in this project must also show: one error, raised by the
# check before project(), with no call stack and none of CMake's follow-on
# errors.
set(clean "CMake Error at CMakeLists.txt:")
set(noise "Call Stack|CMAKE_CXX_COMPILER not set|unable to find a build program")

if(CMAKE_HOST_WIN32)
    # devkitPro's CMake files refuse a native Windows CMake, and so does the
    # check, before it looks at any SDK, so every case says the same thing.
    set(windows "this CMake is a native Windows program|pacman -S cmake")
    nfsu_case("a native Windows CMake is sent to devkitPro's MSYS2 shell"
        "${SOURCE_DIR}" "" "${clean}|${windows}" "${noise}"
        -DDEVKITPRO=${work}/absent)
else()
    nfsu_case("DEVKITPRO names a directory that does not exist"
        "${SOURCE_DIR}" ""
        "${clean}|the devkitPro root ${work}/absent does not exist (DEVKITPRO, from the CMake variable DEVKITPRO)|set DEVKITPRO to the directory it is installed in"
        "${noise}"
        -DDEVKITPRO=${work}/absent)

    nfsu_case("DEVKITPRO comes from the environment"
        "${SOURCE_DIR}" "DEVKITPRO=${work}/absent-too"
        "${clean}|the devkitPro root ${work}/absent-too does not exist (DEVKITPRO, from the environment variable DEVKITPRO)"
        "${noise}")

    file(MAKE_DIRECTORY "${work}/empty")
    nfsu_case("an empty devkitPro root"
        "${SOURCE_DIR}" ""
        "${clean}|the devkitPro SDK is incomplete|DEVKITA64 = ${work}/empty/devkitA64 (from DEVKITPRO, because DEVKITA64 is not set)|missing: aarch64-none-elf-g++, the devkitA64 C++ compiler|looked for: ${work}/empty/devkitA64/bin/aarch64-none-elf-g++|set DEVKITA64 to a devkitA64 directory that has it|missing: libnx, the Switch system library|missing: uam, the offline deko3d shader compiler"
        "${noise}"
        -DDEVKITPRO=${work}/empty)

    nfsu_fake_sdk("${work}/no-uam" tools/bin/uam)
    nfsu_case("an SDK without uam"
        "${SOURCE_DIR}" ""
        "${clean}|missing: uam, the offline deko3d shader compiler|looked for: ${work}/no-uam/tools/bin/uam|package: uam (dkp-pacman -S uam)|set DEVKITPRO to a devkitPro root that has it"
        "${noise}|missing: aarch64|missing: libnx|missing: deko3d"
        -DDEVKITPRO=${work}/no-uam)

    nfsu_fake_sdk("${work}/no-deko3d" libnx/lib/libdeko3d.a)
    nfsu_case("an SDK without deko3d"
        "${SOURCE_DIR}" ""
        "${clean}|missing: deko3d, the Switch graphics library|looked for: ${work}/no-deko3d/libnx/lib/libdeko3d.a|package: deko3d (dkp-pacman -S deko3d)"
        "${noise}|missing: uam|missing: the deko3d header"
        -DDEVKITPRO=${work}/no-deko3d)

    nfsu_fake_sdk("${work}/whole")
    nfsu_case("DEVKITA64 names a directory without the compilers"
        "${SOURCE_DIR}" ""
        "${clean}|DEVKITA64 = ${work}/elsewhere (from the CMake variable DEVKITA64)|missing: aarch64-none-elf-gcc, the devkitA64 C compiler|looked for: ${work}/elsewhere/bin/aarch64-none-elf-gcc|set DEVKITA64 to a devkitA64 directory that has it"
        "${noise}|missing: uam|missing: libnx"
        -DDEVKITPRO=${work}/whole -DDEVKITA64=${work}/elsewhere)

    # The toolchain file from a project with no check of its own.
    file(WRITE "${work}/bare/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.20)\nproject(bare LANGUAGES CXX)\n")
    nfsu_case("the toolchain file's own check, in another project"
        "${work}/bare" ""
        "the devkitPro root ${work}/absent does not exist"
        ""
        -DDEVKITPRO=${work}/absent)
endif()

file(REMOVE_RECURSE "${work}")
if(NOT failed EQUAL 0)
    message(FATAL_ERROR "switch toolchain diagnostics: ${failed} failed, ${passed} passed\n${report}")
endif()
message(STATUS "${report}switch toolchain diagnostics: ${passed} cases, each naming "
    "what is missing and the variable that fixes it")
