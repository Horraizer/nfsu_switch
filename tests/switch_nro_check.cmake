# The Switch application as elf2nro packaged it, read back.
#
# There is no console or emulator in the build, so the application is never
# run here; this is what can be checked without one. The ELF must be an
# AArch64 executable, and the NRO must be what the homebrew loader reads: the
# NRO0 header, an image size inside the file, and after the image the ASET
# section holding the icon and a 0x4000-byte NACP whose first title is the
# one switch/CMakeLists.txt gave nx_generate_nacp.
#
# The reader is then pointed at the ELF, which is not an NRO, and must refuse
# it: a reader that has only ever accepted a file is not evidence.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS NRO ELF TITLE)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

# A little-endian unsigned integer of <bytes> bytes, as a decimal.
function(nfsu_le hex offset bytes out)
    math(EXPR start "${offset} * 2")
    math(EXPR length "${bytes} * 2")
    string(LENGTH "${hex}" hex_length)
    math(EXPR end "${start} + ${length}")
    if(end GREATER hex_length)
        set(${out} -1 PARENT_SCOPE)
        return()
    endif()
    set(value "")
    foreach(index RANGE 1 ${bytes})
        math(EXPR at "${start} + (${bytes} - ${index}) * 2")
        string(SUBSTRING "${hex}" ${at} 2 byte)
        string(APPEND value "${byte}")
    endforeach()
    # math(EXPR) is signed 64-bit, so a full 8-byte read of 0x8000000000000000
    # (2^63) or more cannot be parsed at all: it stops the whole script with
    # "cannot evaluate the expression" rather than returning to this
    # function's caller, and no check below this point runs (review comment
    # 2856, N3). Route it to the same -1 this function already returns for a
    # read past the end of the file, which every caller already treats as
    # out of range, instead of ever handing that hex string to math().
    if(bytes EQUAL 8)
        string(SUBSTRING "${value}" 0 1 top_nibble)
        if(top_nibble MATCHES "[89a-fA-F]")
            set(${out} -1 PARENT_SCOPE)
            return()
        endif()
    endif()
    math(EXPR value "0x${value}" OUTPUT_FORMAT DECIMAL)
    set(${out} ${value} PARENT_SCOPE)
endfunction()

# Sets <out> to "" for a well-formed NRO carrying <title>, or to the problem.
function(nfsu_read_nro path title out)
    get_filename_component(name "${path}" NAME)
    if(NOT EXISTS "${path}")
        set(${out} "${name} does not exist" PARENT_SCOPE)
        return()
    endif()
    file(READ "${path}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR size "${hex_length} / 2")
    if(size LESS 128)
        set(${out} "${name} is ${size} bytes, shorter than an NRO header" PARENT_SCOPE)
        return()
    endif()
    string(SUBSTRING "${hex}" 32 8 magic)
    if(NOT magic STREQUAL "4e524f30")
        set(${out} "${name} has no NRO0 magic at 0x10" PARENT_SCOPE)
        return()
    endif()
    nfsu_le("${hex}" 24 4 image)
    if(image LESS 128 OR image GREATER size)
        set(${out} "${name}: the image size ${image} is outside the file's ${size} bytes" PARENT_SCOPE)
        return()
    endif()
    # The segment table (0x20..0x37), the version (0x14) and the MOD0 offset
    # (0x04) were read nowhere above, so a corrupt header in any of those
    # fields still passed as long as the magic and the image size did
    # (review comment 2856, F3). NRO version is always 0 - every NRO this
    # pipeline or any other homebrew tool produces writes it as a plain
    # spec-fixed constant, not a value the checker would need to derive from
    # anything else - and the three segments are always laid out the same
    # way: text starts at file offset 0, ro follows it immediately, data
    # follows ro immediately, and data ends exactly where the image (already
    # read above) ends. MOD0 is the module header, which lives in .text, so
    # its offset must fall inside the text segment. bss has no such
    # invariant to check against (its size is whatever the link produced,
    # with nothing else in the file to compare it to), so it stays unread,
    # same as the review's own suggested fix.
    nfsu_le("${hex}" 20 4 version)
    if(NOT version EQUAL 0)
        set(${out} "${name}: the NRO version is ${version}, not 0" PARENT_SCOPE)
        return()
    endif()
    nfsu_le("${hex}" 4 4 mod_offset)
    nfsu_le("${hex}" 32 4 text_offset)
    nfsu_le("${hex}" 36 4 text_size)
    nfsu_le("${hex}" 40 4 ro_offset)
    nfsu_le("${hex}" 44 4 ro_size)
    nfsu_le("${hex}" 48 4 data_offset)
    nfsu_le("${hex}" 52 4 data_size)
    if(NOT text_offset EQUAL 0)
        set(${out} "${name}: the text segment is at offset ${text_offset}, not 0" PARENT_SCOPE)
        return()
    endif()
    math(EXPR text_end "${text_offset} + ${text_size}")
    if(NOT ro_offset EQUAL text_end)
        set(${out} "${name}: the ro segment is at offset ${ro_offset}, not ${text_end} where the text segment (size ${text_size}) ends" PARENT_SCOPE)
        return()
    endif()
    math(EXPR ro_end "${ro_offset} + ${ro_size}")
    if(NOT data_offset EQUAL ro_end)
        set(${out} "${name}: the data segment is at offset ${data_offset}, not ${ro_end} where the ro segment (size ${ro_size}) ends" PARENT_SCOPE)
        return()
    endif()
    math(EXPR data_end "${data_offset} + ${data_size}")
    if(NOT data_end EQUAL image)
        set(${out} "${name}: the data segment ends at ${data_end}, not the image size ${image}" PARENT_SCOPE)
        return()
    endif()
    if(mod_offset LESS text_offset OR NOT mod_offset LESS text_end)
        set(${out} "${name}: the MOD0 offset ${mod_offset} is outside the text segment (${text_offset}..${text_end})" PARENT_SCOPE)
        return()
    endif()
    math(EXPR at "${image} * 2")
    string(SUBSTRING "${hex}" ${at} 8 asset_magic)
    if(NOT asset_magic STREQUAL "41534554")
        set(${out} "${name}: no ASET section after the ${image}-byte image" PARENT_SCOPE)
        return()
    endif()
    set(ranges "")
    foreach(part IN ITEMS icon nacp romfs)
        if(part STREQUAL "icon")
            set(field 8)
        elseif(part STREQUAL "nacp")
            set(field 24)
        else()
            set(field 40)
        endif()
        math(EXPR offset_at "${image} + ${field}")
        math(EXPR size_at "${image} + ${field} + 8")
        nfsu_le("${hex}" ${offset_at} 8 ${part}_offset)
        nfsu_le("${hex}" ${size_at} 8 ${part}_size)
        math(EXPR end "${image} + ${${part}_offset} + ${${part}_size}")
        if(${part}_offset LESS 0 OR ${part}_size LESS 0 OR end GREATER size)
            set(${out} "${name}: the ${part} asset runs past the file" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    if(icon_size EQUAL 0)
        set(${out} "${name} carries no icon" PARENT_SCOPE)
        return()
    endif()
    if(NOT nacp_size EQUAL 16384)
        set(${out} "${name}: the NACP is ${nacp_size} bytes, not 0x4000" PARENT_SCOPE)
        return()
    endif()
    # The first of the NACP's title entries: a 0x200-byte name and a
    # 0x100-byte author, NUL-padded.
    math(EXPR title_at "(${image} + ${nacp_offset}) * 2")
    string(HEX "${title}" title_hex)
    string(LENGTH "${title_hex}" title_length)
    math(EXPR title_length "${title_length} + 2")
    string(SUBSTRING "${hex}" ${title_at} ${title_length} stored)
    if(NOT stored STREQUAL "${title_hex}00")
        set(${out} "${name}: the NACP's first title is not `${title}`" PARENT_SCOPE)
        return()
    endif()
    set(${out} "" PARENT_SCOPE)
endfunction()

# Sets <out> to "" for a little-endian 64-bit AArch64 ELF, or to the problem.
function(nfsu_read_elf path out)
    get_filename_component(name "${path}" NAME)
    if(NOT EXISTS "${path}")
        set(${out} "${name} does not exist" PARENT_SCOPE)
        return()
    endif()
    file(READ "${path}" hex LIMIT 64 HEX)
    string(SUBSTRING "${hex}" 0 12 ident)
    nfsu_le("${hex}" 18 2 machine)
    if(NOT ident STREQUAL "7f454c460201" OR NOT machine EQUAL 183)
        set(${out} "${name} is not a little-endian 64-bit AArch64 ELF (machine ${machine})" PARENT_SCOPE)
        return()
    endif()
    set(${out} "" PARENT_SCOPE)
endfunction()

set(problems "")
nfsu_read_elf("${ELF}" problem)
if(NOT problem STREQUAL "")
    string(APPEND problems "${problem}\n")
endif()
nfsu_read_nro("${NRO}" "${TITLE}" problem)
if(NOT problem STREQUAL "")
    string(APPEND problems "${problem}\n")
endif()
nfsu_read_nro("${ELF}" "${TITLE}" problem)
if(problem STREQUAL "")
    string(APPEND problems "the NRO reader accepted the ELF, which is not an NRO\n")
endif()
if(NOT problems STREQUAL "")
    message(FATAL_ERROR "switch NRO check failed:\n${problems}")
endif()
message(STATUS "switch NRO check: an AArch64 ELF, and an NRO with its icon and a NACP "
    "titled ${TITLE}; the reader refuses the ELF (${problem})")
