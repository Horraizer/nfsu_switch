# The Nintendo Switch SDK this project's Switch build needs, located and
# checked before any of devkitPro's own CMake files run.
#
# A Switch configure includes this twice: the top-level CMakeLists.txt calls
# it before project(), so that a missing piece stops the configure with one
# message naming it and nothing after it, and cmake/switch-toolchain.cmake
# calls it again when project() reads the toolchain, because the toolchain
# needs the resolved paths and may be used by a project that has no such
# check. A desktop configure includes neither. docs/SWITCH_BUILD.md shows the
# commands and what these messages look like.

# The pieces the build uses, as label|path under the root|package|variable.
# The path is relative to DEVKITPRO, or to DEVKITA64 when the variable is
# DEVKITA64. A tool is found with or without the .exe that devkitPro's
# Windows packages add.
set(_nfsu_switch_sdk_pieces
    "aarch64-none-elf-gcc, the devkitA64 C compiler|bin/aarch64-none-elf-gcc|devkitA64|DEVKITA64"
    "aarch64-none-elf-g++, the devkitA64 C++ compiler|bin/aarch64-none-elf-g++|devkitA64|DEVKITA64"
    "aarch64-none-elf-gcc-ar, the devkitA64 archiver|bin/aarch64-none-elf-gcc-ar|devkitA64|DEVKITA64"
    "aarch64-none-elf-gcc-ranlib, the devkitA64 archive indexer|bin/aarch64-none-elf-gcc-ranlib|devkitA64|DEVKITA64"
    "aarch64-none-elf-ld, the devkitA64 linker|bin/aarch64-none-elf-ld|devkitA64|DEVKITA64"
    "aarch64-none-elf-strip, the devkitA64 strip tool|bin/aarch64-none-elf-strip|devkitA64|DEVKITA64"
    "aarch64-none-elf-objcopy, the devkitA64 objcopy|bin/aarch64-none-elf-objcopy|devkitA64|DEVKITA64"
    "aarch64-none-elf-nm, the devkitA64 symbol lister|bin/aarch64-none-elf-nm|devkitA64|DEVKITA64"
    "devkitPro's Switch CMake toolchain|cmake/Switch.cmake|switch-cmake|DEVKITPRO"
    "devkitPro's Switch CMake platform|cmake/Platform/NintendoSwitch.cmake|switch-cmake|DEVKITPRO"
    "devkitPro's devkitA64 CMake toolchain|cmake/devkitA64.cmake|devkita64-cmake|DEVKITPRO"
    "devkitPro's shared CMake toolchain code|cmake/dkp-toolchain-common.cmake|dkp-cmake-common-utils|DEVKITPRO"
    "aarch64-none-elf-pkg-config, which devkitPro's Switch toolchain requires|portlibs/switch/bin/aarch64-none-elf-pkg-config|switch-pkg-config|DEVKITPRO"
    "libnx, the Switch system library|libnx/lib/libnx.a|libnx|DEVKITPRO"
    "the libnx link specs|libnx/switch.specs|libnx|DEVKITPRO"
    "the libnx headers|libnx/include/switch.h|libnx|DEVKITPRO"
    "the libnx default application icon|libnx/default_icon.jpg|libnx|DEVKITPRO"
    "deko3d, the Switch graphics library|libnx/lib/libdeko3d.a|deko3d|DEVKITPRO"
    "the deko3d header|libnx/include/deko3d.h|deko3d|DEVKITPRO"
    "uam, the offline deko3d shader compiler|tools/bin/uam|uam|DEVKITPRO"
    "elf2nro, which packages the application|tools/bin/elf2nro|switch-tools|DEVKITPRO"
    "nacptool, which writes the application's metadata|tools/bin/nacptool|switch-tools|DEVKITPRO"
)

# nfsu_switch_sdk_locate(<problem_var>)
#
# Resolves the SDK and checks every piece above. Each root comes from the
# CMake variable of that name (-DDEVKITPRO=...), else the environment
# variable, else the default: /opt/devkitpro, where devkitPro installs, for
# DEVKITPRO, and DEVKITPRO/devkitA64 for DEVKITA64.
#
# Sets NFSU_SWITCH_DEVKITPRO and NFSU_SWITCH_DEVKITA64 in the caller's scope,
# and NFSU_SWITCH_TOOL_<NAME> to the path of each devkitA64 tool, with any
# .exe; exports DEVKITPRO and DEVKITA64 to the environment of this CMake run,
# which is how devkitPro's files and every try_compile project see the same
# SDK; and sets <problem_var> to a message naming each missing piece, where
# it was looked for, the package that provides it and the variable that
# points the build elsewhere, or to "" when nothing is missing.
function(nfsu_switch_sdk_locate problem_var)
    set(${problem_var} "" PARENT_SCOPE)
    # Every line is indented, which CMake prints as it is instead of
    # re-wrapping it, so a path is never broken across two lines.
    set(docs "  docs/SWITCH_BUILD.md lists what the Switch build needs.")

    if(CMAKE_VERSION VERSION_LESS 3.21)
        set(${problem_var}
            "  Nintendo Switch build: this is CMake ${CMAKE_VERSION}, and the Switch build needs 3.21 or newer.\n    why: libnx's link reads DEVKITPRO from the environment, and the build sets it for every link with a linker launcher, which CMake 3.21 added.\n    fix: configure with CMake 3.21 or newer. The desktop build still needs only 3.20.\n${docs}"
            PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_HOST_WIN32)
        set(${problem_var}
            "  Nintendo Switch build: this CMake is a native Windows program, and devkitPro's CMake files refuse one.\n    fix: configure from devkitPro's MSYS2 shell with the CMake installed there (pacman -S cmake).\n${docs}"
            PARENT_SCOPE)
        return()
    endif()

    if(NOT "${DEVKITPRO}" STREQUAL "")
        set(root "${DEVKITPRO}")
        set(root_from "the CMake variable DEVKITPRO")
    elseif(NOT "$ENV{DEVKITPRO}" STREQUAL "")
        set(root "$ENV{DEVKITPRO}")
        set(root_from "the environment variable DEVKITPRO")
    else()
        set(root "/opt/devkitpro")
        set(root_from "the default, because DEVKITPRO is not set")
    endif()
    file(TO_CMAKE_PATH "${root}" root)
    # A relative root would resolve against whichever directory CMake runs
    # in, which is not the same for a configure the build starts itself, and
    # the compiler paths made from it would not be full paths.
    if(NOT IS_ABSOLUTE "${root}")
        set(${problem_var}
            "  Nintendo Switch build: the devkitPro root ${root} is not an absolute path (DEVKITPRO, from ${root_from}).\n    fix: set DEVKITPRO to the absolute path of the directory devkitPro is installed in.\n${docs}"
            PARENT_SCOPE)
        return()
    endif()
    if(NOT IS_DIRECTORY "${root}")
        set(${problem_var}
            "  Nintendo Switch build: the devkitPro root ${root} does not exist (DEVKITPRO, from ${root_from}).\n    fix: install devkitPro with the switch-dev package group, or set DEVKITPRO to the directory it is installed in.\n${docs}"
            PARENT_SCOPE)
        return()
    endif()

    if(NOT "${DEVKITA64}" STREQUAL "")
        set(a64 "${DEVKITA64}")
        set(a64_from "the CMake variable DEVKITA64")
    elseif(NOT "$ENV{DEVKITA64}" STREQUAL "")
        set(a64 "$ENV{DEVKITA64}")
        set(a64_from "the environment variable DEVKITA64")
    else()
        set(a64 "${root}/devkitA64")
        set(a64_from "DEVKITPRO, because DEVKITA64 is not set")
    endif()
    file(TO_CMAKE_PATH "${a64}" a64)
    if(NOT IS_ABSOLUTE "${a64}")
        set(${problem_var}
            "  Nintendo Switch build: the devkitA64 directory ${a64} is not an absolute path (DEVKITA64, from ${a64_from}).\n    fix: set DEVKITA64 to the absolute path of a devkitA64 directory, or unset it to use DEVKITPRO/devkitA64.\n${docs}"
            PARENT_SCOPE)
        return()
    endif()

    set(missing "")
    foreach(piece IN LISTS _nfsu_switch_sdk_pieces)
        string(REPLACE "|" ";" fields "${piece}")
        list(GET fields 0 label)
        list(GET fields 1 path)
        list(GET fields 2 package)
        list(GET fields 3 variable)
        if(variable STREQUAL "DEVKITA64")
            set(full "${a64}/${path}")
        else()
            set(full "${root}/${path}")
        endif()
        if(EXISTS "${full}" AND NOT IS_DIRECTORY "${full}")
            set(found "${full}")
        elseif(EXISTS "${full}.exe")
            set(found "${full}.exe")
        else()
            if(variable STREQUAL "DEVKITA64")
                set(fix "install the package, or set DEVKITA64 to a devkitA64 directory that has it")
            else()
                set(fix "install the package, or set DEVKITPRO to a devkitPro root that has it")
            endif()
            string(APPEND missing
                "    missing: ${label}\n"
                "      looked for: ${full}\n"
                "      package: ${package} (dkp-pacman -S ${package})\n"
                "      fix: ${fix}\n")
            continue()
        endif()
        if(path MATCHES "^bin/aarch64-none-elf-(.+)$")
            string(TOUPPER "${CMAKE_MATCH_1}" tool)
            string(REPLACE "+" "X" tool "${tool}")
            string(REPLACE "-" "_" tool "${tool}")
            set(NFSU_SWITCH_TOOL_${tool} "${found}" PARENT_SCOPE)
        endif()
    endforeach()

    if(NOT missing STREQUAL "")
        set(${problem_var}
            "  Nintendo Switch build: the devkitPro SDK is incomplete.\n    DEVKITPRO = ${root} (from ${root_from})\n    DEVKITA64 = ${a64} (from ${a64_from})\n${missing}${docs}"
            PARENT_SCOPE)
        return()
    endif()

    set(NFSU_SWITCH_DEVKITPRO "${root}" PARENT_SCOPE)
    set(NFSU_SWITCH_DEVKITA64 "${a64}" PARENT_SCOPE)
    set(ENV{DEVKITPRO} "${root}")
    set(ENV{DEVKITA64} "${a64}")
    # Recorded in the cache, so every later configure of this build directory,
    # including one a build starts by itself, resolves the same SDK whatever
    # its environment holds. A root given with -D is already there and stays.
    set(DEVKITPRO "${root}" CACHE PATH "devkitPro root of this Switch build")
    set(DEVKITA64 "${a64}" CACHE PATH "devkitA64 directory of this Switch build")
endfunction()
