# Shader interface manifests: the parser and the checks over it.
#
# A manifest (shaders/*.manifest) states the whole interface of one shader
# program: the vertex buffer layout, the values the vertex stage hands the
# fragment stage, every uniform block with its std140 member offsets, every
# texture binding, the colour outputs, and the fixed-function state of each
# variant the program is drawn with. docs/SHADER_PIPELINE.md defines the
# format, and says what each check below can and cannot catch.
#
# This file is a module and a script.
#
# include()d, it provides nfsu_shader_manifest_stages(), which the Switch
# build uses to compile exactly the stage sources each manifest names.
#
# Run with cmake -P, it performs one operation:
#
#   -DMODE=source -DMANIFEST=<file>
#       The manifest against the GLSL of its two stages. It needs no SDK and
#       runs in the desktop CTest suite.
#
#   -DMODE=blob -DMANIFEST=<file>
#           -DVERTEX_DKSH=<file> -DVERTEX_TGSI=<file>
#           -DFRAGMENT_DKSH=<file> -DFRAGMENT_TGSI=<file> [-DSTAMP=<file>]
#       The manifest against what uam compiled from those stages: the shader
#       program header inside each .dksh, which is what the GPU reads, and
#       the TGSI the same uam invocation wrote beside it. STAMP is written
#       only when nothing drifted, so a build step that failed runs again.
#
#   -DMODE=header -DMANIFEST_DIR=<dir> -DOUTPUT=<file>
#       Every manifest in the directory, as a C++ header of constant tables.
#       Each vertex attribute carries the offsetof and sizeof of the C++
#       members it names, so the header does not compile against a vertex
#       struct that lost one, and tests/shader_manifest_tests.cpp compares
#       the rest.
#
# A check prints one line per finding and then a line that counts them, and
# fails when that count is not zero. A manifest it cannot read is a failure
# too, reported with its file and line number: nothing is skipped because it
# could not be parsed.

if(CMAKE_SCRIPT_MODE_FILE)
    cmake_minimum_required(VERSION 3.20)
endif()

# ---------------------------------------------------------------------------
# Value kinds a manifest field may hold. Every field is checked against one
# of these before anything reads it, so no later step sees a semicolon, an
# unbalanced bracket or a byte outside printable ASCII.

# A number is a decimal without a leading zero, which GLSL and C++ would
# read as octal and CMake as decimal, and of at most nine digits, so that
# no sum below leaves CMake's arithmetic. The GLSL reader holds a stage
# source to the same syntax.
set(_nfsu_sm_digits "[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?[0-9]?")
set(_nfsu_sm_re_uint "^(0|[1-9]${_nfsu_sm_digits})$")
set(_nfsu_sm_re_count "^[1-9]${_nfsu_sm_digits}$")
set(_nfsu_sm_re_ident "^[A-Za-z_][A-Za-z0-9_]*$")
set(_nfsu_sm_re_file "^[A-Za-z0-9_][A-Za-z0-9_.-]*$")
set(_nfsu_sm_re_struct "^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")
set(_nfsu_sm_re_header "^[A-Za-z0-9_][A-Za-z0-9_./-]*[.]h$")
set(_nfsu_sm_re_members "^[A-Za-z_][A-Za-z0-9_]*(,[A-Za-z_][A-Za-z0-9_]*)*$")
set(_nfsu_sm_re_stagekind "^(vertex|fragment)$")
set(_nfsu_sm_re_stages "^(vertex|fragment)(,(vertex|fragment))?$")
set(_nfsu_sm_re_vtype "^(float|int|uint|vec[234]|ivec[234]|uvec[234])$")
set(_nfsu_sm_re_mtype
    "^(float|int|uint|vec[234]|ivec[234]|uvec[234]|mat4)(\\[[1-9]${_nfsu_sm_digits}\\])?$")
set(_nfsu_sm_re_vformat
    "^(float32x[1234]|unorm8x4|snorm8x4|uint8x4|unorm16x2|snorm16x2)$")
set(_nfsu_sm_re_oformat "^rgba8$")
set(_nfsu_sm_re_interp "^(smooth|flat|noperspective)$")
set(_nfsu_sm_re_textype "^sampler2D$")
set(_nfsu_sm_re_filter "^(nearest|linear)$")
set(_nfsu_sm_re_wrap "^(repeat|clamp_to_edge|mirrored_repeat)$")
set(_nfsu_sm_re_compare
    "^(never|less|equal|less_equal|greater|not_equal|greater_equal|always)$")
set(_nfsu_sm_re_onoff "^(on|off)$")
set(_nfsu_sm_re_factor
    "^(zero|one|src_color|one_minus_src_color|src_alpha|one_minus_src_alpha|dst_alpha|one_minus_dst_alpha|dst_color|one_minus_dst_color)$")
set(_nfsu_sm_re_blendop "^(add|subtract|reverse_subtract|min|max)$")
set(_nfsu_sm_re_colorwrite "^(rgb|rgba)$")
set(_nfsu_sm_re_cull "^(none|back|front)$")

# The keys of each record kind as key:value-kind. A leading ? marks a key
# whose presence depends on another key; the rule is checked after parsing.
set(_nfsu_sm_keys_stage "kind:stagekind source:file")
set(_nfsu_sm_keys_buffer "index:uint stride:uint struct:struct header:header")
set(_nfsu_sm_keys_attribute
    "location:uint name:ident type:vtype format:vformat buffer:uint offset:uint members:members")
set(_nfsu_sm_keys_varying "location:uint name:ident type:vtype interpolation:interp")
set(_nfsu_sm_keys_block "name:ident binding:uint stages:stages size:uint")
set(_nfsu_sm_keys_member "block:ident name:ident type:mtype offset:uint")
set(_nfsu_sm_keys_texture
    "name:ident binding:uint stage:stagekind type:textype filter:filter wrap:wrap")
set(_nfsu_sm_keys_output "location:uint name:ident type:vtype format:oformat")
set(_nfsu_sm_keys_variant
    "name:ident alpha_test:compare ?alpha_ref:uint blend:onoff ?blend_src:factor ?blend_dst:factor ?blend_op:blendop depth_test:compare depth_write:onoff color_write:colorwrite cull:cull")

# ---------------------------------------------------------------------------
# Small helpers.

# The value of `key` in a record "kind|key=value|key=value", or "".
function(_nfsu_sm_field record key out)
    if("|${record}|" MATCHES "\\|${key}=([^|]*)\\|")
        set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    else()
        set(${out} "" PARENT_SCOPE)
    endif()
endfunction()

# The record in `records` whose `key` is `value`, or "".
function(_nfsu_sm_find records key value out)
    set(found "")
    foreach(record IN LISTS records)
        _nfsu_sm_field("${record}" "${key}" got)
        if("${got}" STREQUAL "${value}")
            set(found "${record}")
            break()
        endif()
    endforeach()
    set(${out} "${found}" PARENT_SCOPE)
endfunction()

# Components of a GLSL scalar or vector type.
function(_nfsu_sm_components type out)
    if(type MATCHES "^[iu]?vec([234])$")
        set(${out} ${CMAKE_MATCH_1} PARENT_SCOPE)
    else()
        set(${out} 1 PARENT_SCOPE)
    endif()
endfunction()

# The component letters of the first `count` components.
function(_nfsu_sm_letters count out)
    string(SUBSTRING "xyzw" 0 ${count} letters)
    set(${out} "${letters}" PARENT_SCOPE)
endfunction()

# A component mask (x = 1, y = 2, z = 4, w = 8) as letters, "-" for none.
function(_nfsu_sm_mask_letters mask out)
    set(letters "")
    foreach(bit IN ITEMS 0 1 2 3)
        math(EXPR set_bit "(${mask} >> ${bit}) & 1")
        if(set_bit EQUAL 1)
            string(SUBSTRING "xyzw" ${bit} 1 letter)
            string(APPEND letters "${letter}")
        endif()
    endforeach()
    if(letters STREQUAL "")
        set(letters "-")
    endif()
    set(${out} "${letters}" PARENT_SCOPE)
endfunction()

# Components and bytes of a vertex buffer format.
function(_nfsu_sm_format format out_components out_bytes)
    if(format MATCHES "^float32x([1234])$")
        set(components ${CMAKE_MATCH_1})
        math(EXPR bytes "4 * ${components}")
    elseif(format MATCHES "8x4$")
        set(components 4)
        set(bytes 4)
    else()
        set(components 2)
        set(bytes 4)
    endif()
    set(${out_components} ${components} PARENT_SCOPE)
    set(${out_bytes} ${bytes} PARENT_SCOPE)
endfunction()

# std140 base alignment and size of a uniform block member type.
function(_nfsu_sm_std140 type out_align out_size)
    set(count "")
    set(base "${type}")
    if(type MATCHES "^(.+)\\[([0-9]+)\\]$")
        set(base "${CMAKE_MATCH_1}")
        set(count "${CMAKE_MATCH_2}")
    endif()
    if(base STREQUAL "mat4")
        set(align 16)
        set(size 64)
    else()
        _nfsu_sm_components("${base}" n)
        if(n EQUAL 1)
            set(align 4)
            set(size 4)
        elseif(n EQUAL 2)
            set(align 8)
            set(size 8)
        elseif(n EQUAL 3)
            set(align 16)
            set(size 12)
        else()
            set(align 16)
            set(size 16)
        endif()
    endif()
    if(NOT count STREQUAL "")
        # An array element is rounded up to a vec4, whatever it holds.
        math(EXPR stride "((${size} + 15) / 16) * 16")
        math(EXPR size "${stride} * ${count}")
        set(align 16)
    endif()
    set(${out_align} ${align} PARENT_SCOPE)
    set(${out_size} ${size} PARENT_SCOPE)
endfunction()

# The GLSL identifier a manifest name stands for: the prefix, then each
# underscore-separated word capitalised. texcoord is inTexcoord as an input
# and outTexcoord as an output; see docs/SHADER_PIPELINE.md.
function(_nfsu_sm_glsl_name prefix name out)
    string(REPLACE "_" ";" words "${name}")
    set(result "${prefix}")
    foreach(word IN LISTS words)
        string(SUBSTRING "${word}" 0 1 first)
        string(SUBSTRING "${word}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        string(APPEND result "${first}${rest}")
    endforeach()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

# One little-endian u32 of a HEX-read file, as a decimal, or "" past the end.
function(_nfsu_sm_u32 hex offset out)
    math(EXPR start "${offset} * 2")
    string(LENGTH "${hex}" length)
    math(EXPR end "${start} + 8")
    if(end GREATER length)
        set(${out} "" PARENT_SCOPE)
        return()
    endif()
    string(SUBSTRING "${hex}" ${start} 8 word)
    string(SUBSTRING "${word}" 0 2 b0)
    string(SUBSTRING "${word}" 2 2 b1)
    string(SUBSTRING "${word}" 4 2 b2)
    string(SUBSTRING "${word}" 6 2 b3)
    math(EXPR value "0x${b3}${b2}${b1}${b0}" OUTPUT_FORMAT DECIMAL)
    set(${out} ${value} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# The manifest parser.
#
# _nfsu_sm_parse(<file>) sets, in the caller's scope:
#   _sm_program                            the program name
#   _sm_vertex_source _sm_fragment_source  the stage sources, beside the file
#   _sm_buffers _sm_attributes _sm_varyings _sm_blocks _sm_members
#   _sm_textures _sm_outputs _sm_variants  one record per line of that kind
#   _sm_errors                             "file:line: message" per problem
function(_nfsu_sm_parse manifest)
    set(errors "")
    set(program "")
    set(format_seen FALSE)
    set(vertex_source "")
    set(fragment_source "")
    foreach(kind IN ITEMS buffer attribute varying block member texture output variant)
        set(records_${kind} "")
    endforeach()
    get_filename_component(manifest_name "${manifest}" NAME)

    if(NOT EXISTS "${manifest}")
        list(APPEND errors "${manifest_name}: the manifest does not exist: ${manifest}")
    else()
        file(READ "${manifest}" text)
        string(REPLACE "\r" "" text "${text}")
        # The text is walked with string(FIND) and never turned into a list,
        # so a semicolon in a comment is harmless; outside one, the character
        # check below refuses it before anything could split on it.
        set(line_no 0)
        set(record_no 0)
        while(NOT text STREQUAL "")
            string(FIND "${text}" "\n" newline)
            if(newline EQUAL -1)
                set(line "${text}")
                set(text "")
            else()
                string(SUBSTRING "${text}" 0 ${newline} line)
                math(EXPR next "${newline} + 1")
                string(SUBSTRING "${text}" ${next} -1 text)
            endif()
            math(EXPR line_no "${line_no} + 1")
            set(where "${manifest_name}:${line_no}")

            string(FIND "${line}" "#" hash)
            if(NOT hash EQUAL -1)
                string(SUBSTRING "${line}" 0 ${hash} line)
            endif()
            string(STRIP "${line}" line)
            if(line STREQUAL "")
                continue()
            endif()
            if(NOT line MATCHES "^[]A-Za-z0-9_=,.:/ \t[-]+$")
                list(APPEND errors "${where}: a character the format does not use")
                continue()
            endif()
            string(REGEX REPLACE "\\[[0-9]+\\]" "" unbracketed "${line}")
            string(FIND "${unbracketed}" "[" open)
            string(FIND "${unbracketed}" "]" close)
            if(NOT open EQUAL -1 OR NOT close EQUAL -1)
                list(APPEND errors "${where}: [ and ] only enclose an array size, as in vec4[4]")
                continue()
            endif()

            string(REGEX MATCHALL "[^ \t]+" tokens "${line}")
            list(POP_FRONT tokens kind)
            list(LENGTH tokens token_count)
            math(EXPR record_no "${record_no} + 1")
            if(record_no EQUAL 2 AND NOT kind STREQUAL "program")
                list(APPEND errors "${where}: the second record must be `program <name>`")
            endif()

            if(kind STREQUAL "format")
                if(format_seen)
                    list(APPEND errors "${where}: a second format record")
                elseif(NOT token_count EQUAL 1 OR NOT tokens STREQUAL "1")
                    list(APPEND errors "${where}: this parser reads format 1 only")
                endif()
                set(format_seen TRUE)
                continue()
            endif()
            if(NOT format_seen)
                list(APPEND errors "${where}: the first record must be `format 1`")
                set(format_seen TRUE)
            endif()
            if(kind STREQUAL "program")
                if(NOT program STREQUAL "")
                    list(APPEND errors "${where}: a second program record")
                elseif(NOT record_no EQUAL 2)
                    list(APPEND errors "${where}: program must be the second record, after `format 1`")
                elseif(NOT token_count EQUAL 1 OR NOT tokens MATCHES "${_nfsu_sm_re_ident}")
                    list(APPEND errors "${where}: program takes one identifier")
                else()
                    set(program "${tokens}")
                endif()
                continue()
            endif()
            if(NOT DEFINED _nfsu_sm_keys_${kind})
                list(APPEND errors "${where}: `${kind}` is not a record kind")
                continue()
            endif()

            string(REPLACE " " ";" specs "${_nfsu_sm_keys_${kind}}")
            set(record "${kind}")
            set(seen "")
            set(bad FALSE)
            foreach(token IN LISTS tokens)
                if(NOT token MATCHES "^([a-z_0-9]+)=(.+)$")
                    list(APPEND errors "${where}: expected key=value, found `${token}`")
                    set(bad TRUE)
                    continue()
                endif()
                set(key "${CMAKE_MATCH_1}")
                set(value "${CMAKE_MATCH_2}")
                if(key IN_LIST seen)
                    list(APPEND errors "${where}: `${key}` is given twice")
                    set(bad TRUE)
                    continue()
                endif()
                set(value_kind "")
                foreach(spec IN LISTS specs)
                    if(spec MATCHES "^[?]?${key}:(.+)$")
                        set(value_kind "${CMAKE_MATCH_1}")
                    endif()
                endforeach()
                if(value_kind STREQUAL "")
                    list(APPEND errors "${where}: `${key}` is not a key of a ${kind} record")
                    set(bad TRUE)
                    continue()
                endif()
                if(NOT value MATCHES "${_nfsu_sm_re_${value_kind}}")
                    list(APPEND errors "${where}: `${value}` is not a valid ${key}")
                    set(bad TRUE)
                    continue()
                endif()
                list(APPEND seen "${key}")
                string(APPEND record "|${key}=${value}")
            endforeach()
            foreach(spec IN LISTS specs)
                if(spec MATCHES "^([a-z_0-9]+):")
                    if(NOT CMAKE_MATCH_1 IN_LIST seen)
                        list(APPEND errors "${where}: a ${kind} record needs `${CMAKE_MATCH_1}`")
                        set(bad TRUE)
                    endif()
                endif()
            endforeach()
            if(bad)
                continue()
            endif()

            if(kind STREQUAL "stage")
                _nfsu_sm_field("${record}" kind stage_kind)
                _nfsu_sm_field("${record}" source source)
                if(NOT ${stage_kind}_source STREQUAL "")
                    list(APPEND errors "${where}: a second ${stage_kind} stage")
                else()
                    set(${stage_kind}_source "${source}")
                endif()
            else()
                list(APPEND records_${kind} "${record}#${line_no}")
            endif()
        endwhile()
    endif()

    # Everything below reads whole records; the line number rides along
    # after a '#', which no field can contain.
    if(NOT format_seen)
        list(APPEND errors "${manifest_name}: no `format 1` record")
    endif()
    get_filename_component(stem "${manifest}" NAME_WE)
    if(program STREQUAL "")
        list(APPEND errors "${manifest_name}: no program record")
    elseif(NOT program STREQUAL stem)
        list(APPEND errors "${manifest_name}: program `${program}` must be named after its file, `${stem}`")
    endif()
    foreach(stage_kind IN ITEMS vertex fragment)
        if(${stage_kind}_source STREQUAL "")
            list(APPEND errors "${manifest_name}: no ${stage_kind} stage record")
        endif()
    endforeach()

    # Buffers: one per index, and the stride is not zero.
    set(buffer_indices "")
    foreach(entry IN LISTS records_buffer)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" record "${entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" line_no "${entry}")
        _nfsu_sm_field("${record}" index index)
        _nfsu_sm_field("${record}" stride stride)
        if(index IN_LIST buffer_indices)
            list(APPEND errors "${manifest_name}:${line_no}: buffer ${index} is declared twice")
        endif()
        list(APPEND buffer_indices ${index})
        if(stride EQUAL 0)
            list(APPEND errors "${manifest_name}:${line_no}: buffer ${index} has a zero stride")
        endif()
    endforeach()

    # Attributes: locations 0..15, as deko3d and uam allow, once each; a
    # declared buffer; inside its stride; no two over the same bytes; and a
    # type that does not read more components than the format fetches.
    set(locations "")
    set(names "")
    set(ranges "")
    foreach(entry IN LISTS records_attribute)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" record "${entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" line_no "${entry}")
        set(where "${manifest_name}:${line_no}")
        foreach(key IN ITEMS location name type format buffer offset members)
            _nfsu_sm_field("${record}" ${key} ${key})
        endforeach()
        if(location GREATER 15)
            list(APPEND errors "${where}: attribute location ${location} is above 15")
        endif()
        if(location IN_LIST locations)
            list(APPEND errors "${where}: attribute location ${location} is declared twice")
        endif()
        if(name IN_LIST names)
            list(APPEND errors "${where}: attribute `${name}` is declared twice")
        endif()
        list(APPEND locations ${location})
        list(APPEND names ${name})
        _nfsu_sm_components("${type}" type_components)
        _nfsu_sm_format("${format}" format_components format_bytes)
        if(type_components GREATER format_components)
            list(APPEND errors "${where}: ${type} reads ${type_components} components and ${format} fetches ${format_components}")
        endif()
        string(REPLACE "," ";" member_list "${members}")
        list(LENGTH member_list member_count)
        if(member_count GREATER 4)
            list(APPEND errors "${where}: at most four members may be named")
        endif()
        if(NOT buffer IN_LIST buffer_indices)
            list(APPEND errors "${where}: buffer ${buffer} is not declared")
        else()
            _nfsu_sm_find("${records_buffer}" index "${buffer}" buffer_entry)
            string(REGEX REPLACE "#[0-9]+$" "" buffer_record "${buffer_entry}")
            _nfsu_sm_field("${buffer_record}" stride stride)
            math(EXPR end "${offset} + ${format_bytes}")
            if(end GREATER stride)
                list(APPEND errors "${where}: bytes ${offset}..${end} run past the stride ${stride} of buffer ${buffer}")
            endif()
            foreach(range IN LISTS ranges)
                string(REPLACE ":" ";" parts "${range}")
                list(GET parts 0 other_buffer)
                list(GET parts 1 other_start)
                list(GET parts 2 other_end)
                list(GET parts 3 other_name)
                if(other_buffer EQUAL buffer AND offset LESS other_end AND other_start LESS end)
                    list(APPEND errors "${where}: `${name}` overlaps the bytes of `${other_name}`")
                endif()
            endforeach()
            list(APPEND ranges "${buffer}:${offset}:${end}:${name}")
        endif()
    endforeach()

    # Varyings: locations 0..31, once each.
    set(locations "")
    set(names "")
    foreach(entry IN LISTS records_varying)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" record "${entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" line_no "${entry}")
        _nfsu_sm_field("${record}" location location)
        _nfsu_sm_field("${record}" name name)
        if(location GREATER 31)
            list(APPEND errors "${manifest_name}:${line_no}: varying location ${location} is above 31")
        endif()
        if(location IN_LIST locations)
            list(APPEND errors "${manifest_name}:${line_no}: varying location ${location} is declared twice")
        endif()
        if(name IN_LIST names)
            list(APPEND errors "${manifest_name}:${line_no}: varying `${name}` is declared twice")
        endif()
        list(APPEND locations ${location})
        list(APPEND names ${name})
    endforeach()

    # Blocks: a name once, a binding once per stage, and members laid out
    # exactly as std140 lays them out, so a manifest cannot state an offset
    # the compiler would not produce.
    set(block_names "")
    set(bindings "")
    foreach(entry IN LISTS records_block)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" record "${entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" line_no "${entry}")
        set(where "${manifest_name}:${line_no}")
        foreach(key IN ITEMS name binding stages size)
            _nfsu_sm_field("${record}" ${key} ${key})
        endforeach()
        if(name IN_LIST block_names)
            list(APPEND errors "${where}: block `${name}` is declared twice")
        endif()
        list(APPEND block_names ${name})
        if(stages STREQUAL "vertex,vertex" OR stages STREQUAL "fragment,fragment")
            list(APPEND errors "${where}: block `${name}` names a stage twice")
        endif()
        string(REPLACE "," ";" stage_list "${stages}")
        foreach(stage IN LISTS stage_list)
            if("${stage}:${binding}" IN_LIST bindings)
                list(APPEND errors "${where}: ${stage} binding ${binding} holds two blocks")
            endif()
            list(APPEND bindings "${stage}:${binding}")
        endforeach()
        set(expected 0)
        set(member_seen FALSE)
        foreach(member_entry IN LISTS records_member)
            string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" member "${member_entry}")
            string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" member_line "${member_entry}")
            _nfsu_sm_field("${member}" block member_block)
            if(NOT member_block STREQUAL name)
                continue()
            endif()
            set(member_seen TRUE)
            _nfsu_sm_field("${member}" name member_name)
            _nfsu_sm_field("${member}" type member_type)
            _nfsu_sm_field("${member}" offset member_offset)
            _nfsu_sm_std140("${member_type}" align member_size)
            math(EXPR aligned "((${expected} + ${align} - 1) / ${align}) * ${align}")
            if(NOT member_offset EQUAL aligned)
                list(APPEND errors "${manifest_name}:${member_line}: ${name}.${member_name} is at offset ${member_offset}, but std140 puts a ${member_type} after the previous member at ${aligned}")
            endif()
            math(EXPR expected "${aligned} + ${member_size}")
        endforeach()
        if(NOT member_seen)
            list(APPEND errors "${where}: block `${name}` has no member records")
        endif()
        math(EXPR rounded "((${expected} + 15) / 16) * 16")
        if(member_seen AND NOT size EQUAL rounded)
            list(APPEND errors "${where}: block `${name}` has size ${size}, but its std140 members end at ${expected}, which rounds to ${rounded}")
        endif()
    endforeach()
    set(member_keys "")
    foreach(member_entry IN LISTS records_member)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" member "${member_entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" member_line "${member_entry}")
        _nfsu_sm_field("${member}" block member_block)
        _nfsu_sm_field("${member}" name member_name)
        if(NOT member_block IN_LIST block_names)
            list(APPEND errors "${manifest_name}:${member_line}: member of `${member_block}`, which no block record declares")
        endif()
        if("${member_block}.${member_name}" IN_LIST member_keys)
            list(APPEND errors "${manifest_name}:${member_line}: ${member_block}.${member_name} is declared twice")
        endif()
        list(APPEND member_keys "${member_block}.${member_name}")
    endforeach()

    # Textures: a name once, a binding once per stage.
    set(names "")
    set(bindings "")
    foreach(entry IN LISTS records_texture)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" record "${entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" line_no "${entry}")
        foreach(key IN ITEMS name binding stage)
            _nfsu_sm_field("${record}" ${key} ${key})
        endforeach()
        if(name IN_LIST names)
            list(APPEND errors "${manifest_name}:${line_no}: texture `${name}` is declared twice")
        endif()
        if("${stage}:${binding}" IN_LIST bindings)
            list(APPEND errors "${manifest_name}:${line_no}: ${stage} texture binding ${binding} is declared twice")
        endif()
        list(APPEND names ${name})
        list(APPEND bindings "${stage}:${binding}")
    endforeach()

    # Outputs: render targets 0..7, once each.
    set(locations "")
    foreach(entry IN LISTS records_output)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" record "${entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" line_no "${entry}")
        _nfsu_sm_field("${record}" location location)
        if(location GREATER 7)
            list(APPEND errors "${manifest_name}:${line_no}: output location ${location} is above 7")
        endif()
        if(location IN_LIST locations)
            list(APPEND errors "${manifest_name}:${line_no}: output location ${location} is declared twice")
        endif()
        list(APPEND locations ${location})
    endforeach()

    # Variants: a name once, and the keys that only mean something with
    # another one present exactly when it is.
    set(names "")
    foreach(entry IN LISTS records_variant)
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\1" record "${entry}")
        string(REGEX REPLACE "^(.*)#([0-9]+)$" "\\2" line_no "${entry}")
        set(where "${manifest_name}:${line_no}")
        foreach(key IN ITEMS name alpha_test alpha_ref blend blend_src blend_dst blend_op)
            _nfsu_sm_field("${record}" ${key} ${key})
        endforeach()
        if(name IN_LIST names)
            list(APPEND errors "${where}: variant `${name}` is declared twice")
        endif()
        list(APPEND names ${name})
        if(alpha_test STREQUAL "always" OR alpha_test STREQUAL "never")
            if(NOT alpha_ref STREQUAL "")
                list(APPEND errors "${where}: alpha_ref means nothing with alpha_test=${alpha_test}")
            endif()
        elseif(alpha_ref STREQUAL "")
            list(APPEND errors "${where}: alpha_test=${alpha_test} needs alpha_ref")
        elseif(alpha_ref GREATER 255)
            list(APPEND errors "${where}: alpha_ref ${alpha_ref} is above 255")
        endif()
        if(blend STREQUAL "on")
            foreach(key IN ITEMS blend_src blend_dst blend_op)
                if(${key} STREQUAL "")
                    list(APPEND errors "${where}: blend=on needs ${key}")
                endif()
            endforeach()
        else()
            foreach(key IN ITEMS blend_src blend_dst blend_op)
                if(NOT ${key} STREQUAL "")
                    list(APPEND errors "${where}: ${key} means nothing with blend=off")
                endif()
            endforeach()
        endif()
    endforeach()
    if(records_variant STREQUAL "")
        list(APPEND errors "${manifest_name}: no variant record")
    endif()

    # The line numbers are for the messages above only.
    foreach(kind IN ITEMS buffer attribute varying block member texture output variant)
        set(stripped "")
        foreach(entry IN LISTS records_${kind})
            string(REGEX REPLACE "#[0-9]+$" "" record "${entry}")
            list(APPEND stripped "${record}")
        endforeach()
        set(_sm_${kind}s "${stripped}" PARENT_SCOPE)
    endforeach()
    set(_sm_program "${program}" PARENT_SCOPE)
    set(_sm_vertex_source "${vertex_source}" PARENT_SCOPE)
    set(_sm_fragment_source "${fragment_source}" PARENT_SCOPE)
    set(_sm_errors "${errors}" PARENT_SCOPE)
endfunction()

# Prints the parse errors and sets <out> to the line that ends the run, or
# to "" if there are none.
function(_nfsu_sm_parse_failure manifest errors out)
    set(${out} "" PARENT_SCOPE)
    if(errors STREQUAL "")
        return()
    endif()
    list(LENGTH errors count)
    foreach(error IN LISTS errors)
        message(NOTICE "shader manifest error: ${error}")
    endforeach()
    set(${out} "shader interface: ${manifest}: ${count} manifest error(s), so nothing was checked" PARENT_SCOPE)
endfunction()

# nfsu_shader_manifest_stages(<manifest> <program_var> <vertex_var> <fragment_var>)
#
# The program name and the absolute paths of the two stage sources a
# manifest names. A manifest that does not parse stops the configure with
# its errors, so the build never compiles a list it could not read.
function(nfsu_shader_manifest_stages manifest program_var vertex_var fragment_var)
    _nfsu_sm_parse("${manifest}")
    _nfsu_sm_parse_failure("${manifest}" "${_sm_errors}" failure)
    if(NOT failure STREQUAL "")
        message(FATAL_ERROR "${failure}")
    endif()
    get_filename_component(directory "${manifest}" DIRECTORY)
    set(${program_var} "${_sm_program}" PARENT_SCOPE)
    set(${vertex_var} "${directory}/${_sm_vertex_source}" PARENT_SCOPE)
    set(${fragment_var} "${directory}/${_sm_fragment_source}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# The GLSL reader.
#
# It reads the constrained GLSL these shaders are written in, not GLSL in
# general, and it refuses what it does not read rather than skipping it:
# every in, out and uniform at global scope must carry an explicit layout
# qualifier with a literal location or binding, and no preprocessor
# directive other than #version and #extension may appear, because a
# conditional could hide a declaration from a reader that does not
# preprocess. Comments, line ends and line joins it takes apart exactly as
# uam's preprocessor does, for the same reason; see the steps below.
#
# _nfsu_sm_parse_glsl(<file>) sets, in the caller's scope:
#   _sm_glsl_inputs _sm_glsl_outputs  io|location=|name=|type=|interpolation=|qualified=
#   _sm_glsl_blocks                   block|name=|binding=|std140=|members=name:type,...
#   _sm_glsl_samplers                 sampler|name=|binding=|type=
#   _sm_glsl_errors                   messages
function(_nfsu_sm_parse_glsl file)
    set(errors "")
    set(inputs "")
    set(outputs "")
    set(blocks "")
    set(samplers "")
    get_filename_component(name_only "${file}" NAME)
    if(NOT EXISTS "${file}")
        set(_sm_glsl_errors "${name_only}: the stage source does not exist: ${file}" PARENT_SCOPE)
        return()
    endif()
    # The text as uam's preprocessor, Mesa's glcpp, sees it, built in the
    # order glcpp builds it. A reader that splits comments, lines or
    # directives any other way can be shown one declaration while the
    # compiler reads another, so every step here is glcpp's own.
    #
    # 1. Line ends. glcpp ends a line at LF, CR LF, LF CR or a lone CR.
    #    file(READ) is not the bytes: it drops the CR of each CR LF and a CR
    #    that ends the file, and nothing after a NUL reaches a CMake string.
    #    So the bytes are read first, and a NUL, or a CR that does not begin
    #    a CR LF, is refused; what file(READ) then returns is the file with
    #    each line end, LF or CR LF, as one LF.
    file(READ "${file}" hex HEX)
    string(REGEX REPLACE "(..)" " \\1" bytes "${hex}")
    if(bytes MATCHES " 00")
        set(_sm_glsl_errors "${name_only}: has a NUL byte" PARENT_SCOPE)
        return()
    endif()
    if(bytes MATCHES " 0d( [^0].| 0[^a]|$)")
        set(_sm_glsl_errors "${name_only}: has a CR that does not begin a CR LF, and the interface check reads LF and CR LF line ends only" PARENT_SCOPE)
        return()
    endif()
    file(READ "${file}" text)
    # A copy with real line ends, kept only so a later refusal can name the
    # line a bad declaration started on; every parsing step below works on
    # `text`, which loses its line ends once whitespace is collapsed.
    set(raw_lines_text "${text}")
    string(ASCII 11 vt)
    string(ASCII 12 ff)
    if(text MATCHES "[^\t\n${vt}${ff} -~]")
        set(_sm_glsl_errors "${name_only}: has a byte that is not printable ASCII, a tab, a line end, a vertical tab or a form feed" PARENT_SCOPE)
        return()
    endif()
    # 2. A backslash at the end of a line joins the next line to it, before
    #    any comment is seen: a // comment ending in one takes the next line.
    string(REPLACE "\\\n" "" text "${text}")
    # 3. Comments go in one pass from left to right. Whichever of // and /*
    #    comes first opens one; the */ that closes a block is looked for
    #    after its /*, so /*/ opens a comment and does not close it; a block
    #    becomes one space; and inside a comment neither opener means
    #    anything.
    set(kept "")
    while(TRUE)
        string(FIND "${text}" "//" line_at)
        string(FIND "${text}" "/*" block_at)
        if(line_at EQUAL -1 AND block_at EQUAL -1)
            break()
        endif()
        if(block_at EQUAL -1 OR (NOT line_at EQUAL -1 AND line_at LESS block_at))
            string(SUBSTRING "${text}" 0 ${line_at} head)
            string(APPEND kept "${head}")
            string(SUBSTRING "${text}" ${line_at} -1 text)
            string(FIND "${text}" "\n" end)
            if(end EQUAL -1)
                set(text "")
            else()
                string(SUBSTRING "${text}" ${end} -1 text)
            endif()
        else()
            string(SUBSTRING "${text}" 0 ${block_at} head)
            string(APPEND kept "${head} ")
            math(EXPR inside "${block_at} + 2")
            string(SUBSTRING "${text}" ${inside} -1 text)
            string(FIND "${text}" "*/" end)
            if(end EQUAL -1)
                list(APPEND errors "${name_only}: a /* comment is never closed")
                set(text "")
                break()
            endif()
            math(EXPR end "${end} + 2")
            string(SUBSTRING "${text}" ${end} -1 text)
        endif()
    endwhile()
    set(text "\n${kept}${text}\n")

    # From here on a statement ends at @: the reader lists what it reads, and
    # a CMake list splits on the semicolon. A message quoting the source
    # therefore shows its semicolons as @.
    if(text MATCHES "@")
        list(APPEND errors "${name_only}: contains @ outside a comment, which this reader uses as its statement separator")
        set(text "\n")
    endif()
    string(REPLACE ";" "@" text "${text}")

    # 4. A line whose first byte other than a space, tab, vertical tab or
    #    form feed is # is a directive. Only #version and #extension may
    #    appear: a conditional or a macro could change a declaration without
    #    changing its text. Any other # left anywhere is refused too.
    string(REGEX MATCHALL "\n[ \t${vt}${ff}]*#[^\n]*" directives "${text}")
    foreach(directive IN LISTS directives)
        string(REGEX REPLACE "^\n[ \t${vt}${ff}]*" "" directive "${directive}")
        if(NOT directive MATCHES "^#[ \t${vt}${ff}]*(version|extension)[ \t${vt}${ff}]")
            string(REGEX REPLACE "[ \t${vt}${ff}]+$" "" directive "${directive}")
            list(APPEND errors "${name_only}: `${directive}` is a preprocessor directive the interface check does not follow")
        endif()
    endforeach()
    string(REGEX REPLACE "\n[ \t${vt}${ff}]*#[^\n]*" "\n" text "${text}")
    if(text MATCHES "#")
        list(APPEND errors "${name_only}: has a # that does not begin a line, which the interface check does not follow")
    endif()

    string(REGEX REPLACE "[ \t\n${vt}${ff}]+" " " text "${text}")

    string(REGEX MATCHALL
        "layout ?[(][^)]*[)] ?[^@{}]*([{][^}]*[}] ?[A-Za-z0-9_]* ?)?@"
        declarations "${text}")
    set(rest "${text}")
    foreach(declaration IN LISTS declarations)
        string(REPLACE "${declaration}" " @ " rest "${rest}")
        if(NOT declaration MATCHES "^layout ?[(]([^)]*)[)] ?(.*)@$")
            list(APPEND errors "${name_only}: cannot read `${declaration}`")
            continue()
        endif()
        set(qualifiers "${CMAKE_MATCH_1}")
        string(STRIP "${CMAKE_MATCH_2}" body)
        # `layout (early_fragment_tests) in;` declares no interface variable;
        # it only moves a fragment stage's depth test before shading. Any
        # other qualifier beside it falls through and is refused below.
        if(qualifiers MATCHES "^ ?early_fragment_tests ?$" AND body STREQUAL "in")
            if(NOT name_only MATCHES "[.]frag[.]glsl$")
                list(APPEND errors "${name_only}: early_fragment_tests outside a fragment stage")
            endif()
            continue()
        endif()

        set(location "")
        set(binding "")
        set(std140 0)
        string(REPLACE "," ";" qualifier_list "${qualifiers}")
        foreach(qualifier IN LISTS qualifier_list)
            string(STRIP "${qualifier}" qualifier)
            if(qualifier STREQUAL "std140")
                set(std140 1)
            elseif(qualifier MATCHES "^(location|binding) ?= ?([0-9]+)$")
                # The manifest's own number syntax: GLSL reads a leading zero
                # as octal, so 010 is location 8 to uam and 10 to a string
                # compare.
                set(qualifier_key "${CMAKE_MATCH_1}")
                set(qualifier_value "${CMAKE_MATCH_2}")
                if(NOT qualifier_value MATCHES "${_nfsu_sm_re_uint}")
                    list(APPEND errors "${name_only}: `${qualifier}` is not a decimal without a leading zero, of at most nine digits")
                endif()
                set(${qualifier_key} "${qualifier_value}")
            else()
                list(APPEND errors "${name_only}: `${qualifier}` is a layout qualifier the interface check does not read")
            endif()
        endforeach()

        if(body MATCHES "^((flat|smooth|noperspective) )?(in|out) ([A-Za-z_][A-Za-z0-9_]*) ([A-Za-z_][A-Za-z0-9_]*)$")
            set(interpolation "${CMAKE_MATCH_2}")
            set(qualified 1)
            if(interpolation STREQUAL "")
                set(interpolation "smooth")
                set(qualified 0)
            endif()
            set(direction "${CMAKE_MATCH_3}")
            set(type "${CMAKE_MATCH_4}")
            set(variable "${CMAKE_MATCH_5}")
            if(location STREQUAL "" OR NOT binding STREQUAL "" OR std140)
                list(APPEND errors "${name_only}: `${variable}` needs layout (location = N) and nothing else")
                continue()
            endif()
            set(record "io|location=${location}|name=${variable}|type=${type}|interpolation=${interpolation}|qualified=${qualified}")
            if(direction STREQUAL "in")
                list(APPEND inputs "${record}")
            else()
                list(APPEND outputs "${record}")
            endif()
        elseif(body MATCHES "^uniform ([A-Za-z_][A-Za-z0-9_]*) ?[{](.*)[}] ?([A-Za-z_][A-Za-z0-9_]*)?$")
            set(block "${CMAKE_MATCH_1}")
            set(member_text "${CMAKE_MATCH_2}")
            if(binding STREQUAL "" OR NOT location STREQUAL "")
                list(APPEND errors "${name_only}: block `${block}` needs layout (std140, binding = N)")
                continue()
            endif()
            string(REPLACE "@" ";" member_list "${member_text}")
            set(members "")
            foreach(member IN LISTS member_list)
                string(STRIP "${member}" member)
                if(member STREQUAL "")
                    continue()
                endif()
                if(member MATCHES "^([A-Za-z_][A-Za-z0-9_]*) ([A-Za-z_][A-Za-z0-9_]*) ?(\\[([0-9]+)\\])?$")
                    set(member_type "${CMAKE_MATCH_1}")
                    set(member_name "${CMAKE_MATCH_2}")
                    set(member_count "${CMAKE_MATCH_4}")
                    if(NOT member_count STREQUAL "")
                        if(NOT member_count MATCHES "${_nfsu_sm_re_count}")
                            list(APPEND errors "${name_only}: block `${block}` member `${member}` has an array size that is not a decimal above zero without a leading zero, of at most nine digits")
                            continue()
                        endif()
                        set(member_type "${member_type}[${member_count}]")
                    endif()
                    list(APPEND members "${member_name}:${member_type}")
                else()
                    list(APPEND errors "${name_only}: block `${block}` member `${member}` is not one the interface check reads")
                endif()
            endforeach()
            string(REPLACE ";" "," members "${members}")
            list(APPEND blocks "block|name=${block}|binding=${binding}|std140=${std140}|members=${members}")
        elseif(body MATCHES "^uniform ([A-Za-z0-9_]*sampler[A-Za-z0-9_]*) ([A-Za-z_][A-Za-z0-9_]*)$")
            set(type "${CMAKE_MATCH_1}")
            set(variable "${CMAKE_MATCH_2}")
            if(binding STREQUAL "" OR NOT location STREQUAL "" OR std140)
                list(APPEND errors "${name_only}: sampler `${variable}` needs layout (binding = N) and nothing else")
                continue()
            endif()
            list(APPEND samplers "sampler|name=${variable}|binding=${binding}|type=${type}")
        else()
            list(APPEND errors "${name_only}: `${body}` is a declaration the interface check does not read")
        endif()
    endforeach()

    # A qualifier written before layout(...) compiles exactly like one
    # written after it - GLSL 4.60 allows either order, and uam accepted
    # both forms of `flat layout (location = 1) in vec3 inColor;` and
    # `layout (location = 1) flat in vec3 inColor;` alike - but the
    # declaration match above always starts at the word `layout`, so a
    # prefix qualifier is left outside every match, sitting in `rest`. The
    # leftover check below only refuses `<qualifiers> in|out|uniform
    # <identifier>` right after a statement boundary, and a bare qualifier
    # in front of a `layout` that was already consumed there never matches
    # that pattern, so without this check it would be silently dropped: the
    # declaration parses as if unqualified (smooth), matching the manifest
    # by accident, while the compiled shader carries the qualifier that was
    # actually written.
    #
    # `layout` is a GLSL keyword, so `layout(` (or `layout (`) can only ever
    # open a fresh layout-qualifier clause - the sole text that can legally
    # sit between a statement boundary (the start of the file, a `;`, which
    # is `@` from here on, a `{` or a `}`) and a matched `layout(...)` is
    # one prefix qualifier. So this refuses ANY identifier found there, not
    # a fixed list of four: shaders/*.glsl (checked below, in the source
    # every case here reads) writes no qualifier before a layout(...) at
    # all, this reader has no manifest field for any of them, flat and
    # smooth and noperspective included, only for the interpolation it
    # reads after layout(...) two lines below - so every prefix qualifier is
    # refused here rather than read and compared, the same way a body
    # qualifier it cannot place two lines below is refused rather than
    # guessed at.
    set(prefix_scan "@${text}")
    while(TRUE)
        string(REGEX MATCH "[@{}] *([A-Za-z_][A-Za-z0-9_]*) +layout ?[(]" hit "${prefix_scan}")
        if(hit STREQUAL "")
            break()
        endif()
        set(qualifier "${CMAKE_MATCH_1}")
        set(line_no "an unknown line")
        string(FIND "${raw_lines_text}" "${qualifier} layout" raw_at)
        if(NOT raw_at EQUAL -1)
            string(SUBSTRING "${raw_lines_text}" 0 ${raw_at} before_raw)
            string(REGEX MATCHALL "\n" newlines_before "${before_raw}")
            list(LENGTH newlines_before line_no)
            math(EXPR line_no "${line_no} + 1")
        endif()
        list(APPEND errors "${name_only}:${line_no}: \`${qualifier}\` qualifies a declaration before its layout(...) - this reader only reads a qualifier written after layout(...), so it is refused here rather than silently dropped")
        string(FIND "${prefix_scan}" "${hit}" hit_at)
        string(LENGTH "${hit}" hit_len)
        math(EXPR after "${hit_at} + ${hit_len}")
        string(SUBSTRING "${prefix_scan}" ${after} -1 prefix_scan)
    endwhile()

    # Whatever is left must not declare an interface: an in, out or uniform
    # at global scope without a layout qualifier is refused, because the
    # compiler would assign it a location or binding the manifest cannot state.
    set(rest "@ ${rest} ")
    string(REGEX REPLACE "  +" " " rest "${rest}")
    if(rest MATCHES "[@}] ?((flat|smooth|noperspective|centroid|sample|patch|invariant|precise|highp|mediump|lowp|coherent|volatile|restrict|readonly|writeonly|const) )*(in|out|inout|uniform|buffer|attribute|varying|shared) [A-Za-z_]")
        list(APPEND errors "${name_only}: declares an interface variable without a layout qualifier the check reads")
    endif()
    if(rest MATCHES "[^A-Za-z0-9_]layout[^A-Za-z0-9_]")
        list(APPEND errors "${name_only}: has a layout declaration the interface check could not read")
    endif()

    set(_sm_glsl_inputs "${inputs}" PARENT_SCOPE)
    set(_sm_glsl_outputs "${outputs}" PARENT_SCOPE)
    set(_sm_glsl_blocks "${blocks}" PARENT_SCOPE)
    set(_sm_glsl_samplers "${samplers}" PARENT_SCOPE)
    set(_sm_glsl_errors "${errors}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Manifest against source.

# Compares one direction of a stage's variables with what the manifest
# expects there. `expected` holds io records in the manifest's terms (the
# GLSL name already derived); `what` names the kind in messages.
function(_nfsu_sm_compare_io file what expected actual interpolation_matters out)
    set(findings "")
    set(actual_locations "")
    foreach(record IN LISTS actual)
        _nfsu_sm_field("${record}" location location)
        _nfsu_sm_field("${record}" name name)
        _nfsu_sm_field("${record}" type type)
        if(location IN_LIST actual_locations)
            list(APPEND findings "${file} declares two ${what}s at location ${location}")
        endif()
        list(APPEND actual_locations ${location})
        _nfsu_sm_find("${expected}" location "${location}" want)
        if(want STREQUAL "")
            list(APPEND findings "${file} declares ${what} location ${location} (${type} ${name}), which the manifest does not")
        endif()
    endforeach()
    foreach(want IN LISTS expected)
        _nfsu_sm_field("${want}" location location)
        _nfsu_sm_field("${want}" name want_name)
        _nfsu_sm_field("${want}" type want_type)
        _nfsu_sm_field("${want}" interpolation want_interpolation)
        _nfsu_sm_find("${actual}" location "${location}" got)
        if(got STREQUAL "")
            list(APPEND findings "${what} location ${location} (${want_type} ${want_name}) is in the manifest, but ${file} does not declare it")
            continue()
        endif()
        _nfsu_sm_field("${got}" name got_name)
        _nfsu_sm_field("${got}" type got_type)
        _nfsu_sm_field("${got}" interpolation got_interpolation)
        _nfsu_sm_field("${got}" qualified qualified)
        if(NOT got_type STREQUAL want_type)
            list(APPEND findings "${what} location ${location}: the manifest says ${want_type}, ${file} declares ${got_type}")
        endif()
        if(NOT got_name STREQUAL want_name)
            list(APPEND findings "${what} location ${location}: the manifest names it ${want_name}, ${file} declares ${got_name}")
        endif()
        if(interpolation_matters)
            if(NOT got_interpolation STREQUAL want_interpolation)
                list(APPEND findings "${what} location ${location} (${want_name}): the manifest says ${want_interpolation}, ${file} declares ${got_interpolation}")
            endif()
        elseif(qualified)
            list(APPEND findings "${what} location ${location} (${got_name}) in ${file} carries an interpolation qualifier, which only a varying may")
        endif()
    endforeach()
    set(${out} "${findings}" PARENT_SCOPE)
endfunction()

# The findings of one stage source against the manifest parsed in the
# caller's scope.
function(_nfsu_sm_check_source_stage stage file out)
    set(findings "")
    get_filename_component(name_only "${file}" NAME)
    _nfsu_sm_parse_glsl("${file}")
    foreach(error IN LISTS _sm_glsl_errors)
        list(APPEND findings "${error}")
    endforeach()

    # What the manifest says this stage reads and writes, as io records
    # carrying the GLSL names the naming rule gives them.
    set(want_inputs "")
    set(want_outputs "")
    foreach(record IN LISTS _sm_varyings)
        _nfsu_sm_field("${record}" location location)
        _nfsu_sm_field("${record}" name name)
        _nfsu_sm_field("${record}" type type)
        _nfsu_sm_field("${record}" interpolation interpolation)
        if(stage STREQUAL "vertex")
            _nfsu_sm_glsl_name(out "${name}" glsl)
            list(APPEND want_outputs "io|location=${location}|name=${glsl}|type=${type}|interpolation=${interpolation}")
        else()
            _nfsu_sm_glsl_name(in "${name}" glsl)
            list(APPEND want_inputs "io|location=${location}|name=${glsl}|type=${type}|interpolation=${interpolation}")
        endif()
    endforeach()
    if(stage STREQUAL "vertex")
        foreach(record IN LISTS _sm_attributes)
            _nfsu_sm_field("${record}" location location)
            _nfsu_sm_field("${record}" name name)
            _nfsu_sm_field("${record}" type type)
            _nfsu_sm_glsl_name(in "${name}" glsl)
            list(APPEND want_inputs "io|location=${location}|name=${glsl}|type=${type}|interpolation=smooth")
        endforeach()
        _nfsu_sm_compare_io("${name_only}" "vertex attribute" "${want_inputs}" "${_sm_glsl_inputs}" FALSE found)
        list(APPEND findings ${found})
        _nfsu_sm_compare_io("${name_only}" "varying" "${want_outputs}" "${_sm_glsl_outputs}" TRUE found)
        list(APPEND findings ${found})
    else()
        foreach(record IN LISTS _sm_outputs)
            _nfsu_sm_field("${record}" location location)
            _nfsu_sm_field("${record}" name name)
            _nfsu_sm_field("${record}" type type)
            _nfsu_sm_glsl_name(out "${name}" glsl)
            list(APPEND want_outputs "io|location=${location}|name=${glsl}|type=${type}|interpolation=smooth")
        endforeach()
        _nfsu_sm_compare_io("${name_only}" "varying" "${want_inputs}" "${_sm_glsl_inputs}" TRUE found)
        list(APPEND findings ${found})
        _nfsu_sm_compare_io("${name_only}" "output" "${want_outputs}" "${_sm_glsl_outputs}" FALSE found)
        list(APPEND findings ${found})
    endif()

    # Uniform blocks: the ones the manifest gives this stage, with the same
    # binding and the same members in the same order, whose std140 layout
    # then lands every member on the manifest's offset.
    set(want_blocks "")
    foreach(record IN LISTS _sm_blocks)
        _nfsu_sm_field("${record}" stages stages)
        string(REPLACE "," ";" stage_list "${stages}")
        if(stage IN_LIST stage_list)
            list(APPEND want_blocks "${record}")
        endif()
    endforeach()
    foreach(got IN LISTS _sm_glsl_blocks)
        _nfsu_sm_field("${got}" name name)
        _nfsu_sm_field("${got}" binding binding)
        _nfsu_sm_find("${want_blocks}" name "${name}" want)
        if(want STREQUAL "")
            list(APPEND findings "${name_only} declares uniform block ${name} at binding ${binding}, which the manifest does not give the ${stage} stage")
        endif()
    endforeach()
    foreach(want IN LISTS want_blocks)
        _nfsu_sm_field("${want}" name name)
        _nfsu_sm_field("${want}" binding want_binding)
        _nfsu_sm_field("${want}" size want_size)
        _nfsu_sm_find("${_sm_glsl_blocks}" name "${name}" got)
        if(got STREQUAL "")
            list(APPEND findings "uniform block ${name} (binding ${want_binding}) is in the manifest for the ${stage} stage, but ${name_only} does not declare it")
            continue()
        endif()
        _nfsu_sm_field("${got}" binding got_binding)
        _nfsu_sm_field("${got}" std140 std140)
        _nfsu_sm_field("${got}" members got_members)
        if(NOT got_binding STREQUAL want_binding)
            list(APPEND findings "uniform block ${name}: the manifest binds it at ${want_binding}, ${name_only} at ${got_binding}")
        endif()
        if(NOT std140)
            list(APPEND findings "uniform block ${name} in ${name_only} is not std140")
        endif()
        set(want_members "")
        foreach(member IN LISTS _sm_members)
            _nfsu_sm_field("${member}" block member_block)
            if(member_block STREQUAL name)
                list(APPEND want_members "${member}")
            endif()
        endforeach()
        string(REPLACE "," ";" got_list "${got_members}")
        list(LENGTH got_list got_count)
        list(LENGTH want_members want_count)
        if(NOT got_count EQUAL want_count)
            list(APPEND findings "uniform block ${name}: the manifest lists ${want_count} members, ${name_only} declares ${got_count}")
        endif()
        set(offset 0)
        set(index 0)
        foreach(pair IN LISTS got_list)
            string(REPLACE ":" ";" parts "${pair}")
            list(GET parts 0 member_name)
            list(GET parts 1 member_type)
            _nfsu_sm_std140("${member_type}" align size)
            math(EXPR offset "((${offset} + ${align} - 1) / ${align}) * ${align}")
            if(index LESS want_count)
                list(GET want_members ${index} want_member)
                _nfsu_sm_field("${want_member}" name want_name)
                _nfsu_sm_field("${want_member}" type want_type)
                _nfsu_sm_field("${want_member}" offset want_offset)
                if(NOT member_name STREQUAL want_name OR NOT member_type STREQUAL want_type)
                    list(APPEND findings "uniform block ${name} member ${index}: the manifest says ${want_type} ${want_name}, ${name_only} declares ${member_type} ${member_name}")
                elseif(NOT offset EQUAL want_offset)
                    list(APPEND findings "uniform block ${name}.${member_name}: the manifest puts it at offset ${want_offset}, std140 puts the ${name_only} declaration at ${offset}")
                endif()
            endif()
            math(EXPR offset "${offset} + ${size}")
            math(EXPR index "${index} + 1")
        endforeach()
        math(EXPR rounded "((${offset} + 15) / 16) * 16")
        if(NOT rounded EQUAL want_size)
            list(APPEND findings "uniform block ${name}: the manifest says ${want_size} bytes, the std140 layout of ${name_only} is ${rounded}")
        endif()
    endforeach()

    # Samplers: the textures the manifest gives this stage, by name, binding
    # and type.
    set(want_textures "")
    foreach(record IN LISTS _sm_textures)
        _nfsu_sm_field("${record}" stage texture_stage)
        if(texture_stage STREQUAL stage)
            list(APPEND want_textures "${record}")
        endif()
    endforeach()
    foreach(got IN LISTS _sm_glsl_samplers)
        _nfsu_sm_field("${got}" name name)
        _nfsu_sm_field("${got}" binding binding)
        _nfsu_sm_find("${want_textures}" name "${name}" want)
        if(want STREQUAL "")
            list(APPEND findings "${name_only} declares texture ${name} at binding ${binding}, which the manifest does not give the ${stage} stage")
        endif()
    endforeach()
    foreach(want IN LISTS want_textures)
        _nfsu_sm_field("${want}" name name)
        _nfsu_sm_field("${want}" binding want_binding)
        _nfsu_sm_field("${want}" type want_type)
        _nfsu_sm_find("${_sm_glsl_samplers}" name "${name}" got)
        if(got STREQUAL "")
            list(APPEND findings "texture ${name} (binding ${want_binding}) is in the manifest for the ${stage} stage, but ${name_only} does not declare it")
            continue()
        endif()
        _nfsu_sm_field("${got}" binding got_binding)
        _nfsu_sm_field("${got}" type got_type)
        if(NOT got_binding STREQUAL want_binding)
            list(APPEND findings "texture ${name}: the manifest binds it at ${want_binding}, ${name_only} at ${got_binding}")
        endif()
        if(NOT got_type STREQUAL want_type)
            list(APPEND findings "texture ${name}: the manifest says ${want_type}, ${name_only} declares ${got_type}")
        endif()
    endforeach()

    set(${out} "${findings}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Manifest against the compiled shader.

# Reads the one program of a .dksh: its type and the 20 words of its shader
# program header (SPH). Sets <prefix>_type, <prefix>_sph and <prefix>_error.
#
# The container is uam's: a 24-byte header (magic DKSH, header size,
# control size, code size, program table offset, program count), the program
# table inside the control section, and the code section after it. A
# program's entry point is an offset into the code section, and the SPH is
# the 0x50 bytes that start there; the GPU reads the SPH to know which
# attributes a program fetches and which outputs it writes.
function(_nfsu_sm_read_dksh file prefix)
    set(${prefix}_type "" PARENT_SCOPE)
    set(${prefix}_sph "" PARENT_SCOPE)
    get_filename_component(name_only "${file}" NAME)
    if(NOT EXISTS "${file}")
        set(${prefix}_error "${name_only} does not exist" PARENT_SCOPE)
        return()
    endif()
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR size "${hex_length} / 2")
    string(SUBSTRING "${hex}" 0 8 magic)
    if(size LESS 24 OR NOT magic STREQUAL "444b5348")
        set(${prefix}_error "${name_only} is not a DKSH file" PARENT_SCOPE)
        return()
    endif()
    _nfsu_sm_u32("${hex}" 4 header_size)
    _nfsu_sm_u32("${hex}" 8 control_size)
    _nfsu_sm_u32("${hex}" 12 code_size)
    _nfsu_sm_u32("${hex}" 16 programs)
    _nfsu_sm_u32("${hex}" 20 program_count)
    math(EXPR total "${control_size} + ${code_size}")
    if(NOT header_size EQUAL 24 OR NOT total EQUAL size)
        set(${prefix}_error "${name_only}: header size ${header_size} and sections ${control_size} + ${code_size} do not describe its ${size} bytes" PARENT_SCOPE)
        return()
    endif()
    if(NOT program_count EQUAL 1)
        set(${prefix}_error "${name_only} holds ${program_count} programs; uam writes one per stage" PARENT_SCOPE)
        return()
    endif()
    math(EXPR table_end "${programs} + 8")
    if(table_end GREATER control_size)
        set(${prefix}_error "${name_only}: the program table runs past the control section" PARENT_SCOPE)
        return()
    endif()
    _nfsu_sm_u32("${hex}" ${programs} type)
    math(EXPR entry_field "${programs} + 4")
    _nfsu_sm_u32("${hex}" ${entry_field} entry)
    math(EXPR sph_start "${control_size} + ${entry}")
    math(EXPR sph_end "${sph_start} + 80")
    if(sph_end GREATER size)
        set(${prefix}_error "${name_only}: the shader program header at ${sph_start} runs past the file" PARENT_SCOPE)
        return()
    endif()
    set(words "")
    foreach(index RANGE 0 19)
        math(EXPR offset "${sph_start} + 4 * ${index}")
        _nfsu_sm_u32("${hex}" ${offset} word)
        list(APPEND words ${word})
    endforeach()
    set(${prefix}_type "${type}" PARENT_SCOPE)
    set(${prefix}_sph "${words}" PARENT_SCOPE)
    set(${prefix}_error "" PARENT_SCOPE)
endfunction()

# Reads the declarations of uam's TGSI dump. Sets <prefix>_processor,
# <prefix>_ins, <prefix>_outs, <prefix>_consts, <prefix>_samps,
# <prefix>_sviews, <prefix>_unknown and <prefix>_error.
function(_nfsu_sm_read_tgsi file prefix)
    foreach(part IN ITEMS processor ins outs consts samps sviews unknown error)
        set(${part} "")
    endforeach()
    get_filename_component(name_only "${file}" NAME)
    if(NOT EXISTS "${file}")
        set(error "${name_only} does not exist")
    else()
        file(READ "${file}" text)
        if(text MATCHES ";")
            set(error "${name_only} contains a semicolon, which no TGSI declaration does")
        else()
            file(STRINGS "${file}" lines)
            foreach(line IN LISTS lines)
                string(STRIP "${line}" line)
                if(processor STREQUAL "" AND NOT line STREQUAL "")
                    set(processor "${line}")
                    continue()
                endif()
                if(NOT line MATCHES "^DCL ")
                    continue()
                endif()
                set(mask "xyzw")
                if(line MATCHES "^DCL IN\\[([0-9]+)\\](\\.([xyzw]+))?(, ([A-Z]+)(\\[([0-9]+)\\])?)?(.*)$")
                    if(NOT "${CMAKE_MATCH_3}" STREQUAL "")
                        set(mask "${CMAKE_MATCH_3}")
                    endif()
                    set(in_index "${CMAKE_MATCH_1}")
                    set(semantic "${CMAKE_MATCH_5}")
                    set(semantic_index "${CMAKE_MATCH_7}")
                    if(semantic_index STREQUAL "")
                        set(semantic_index 0)
                    endif()
                    # A varying may carry CENTROID after its interpolation
                    # mode (Gallium's TGSI dump syntax): "DCL IN[n], GENERIC[n],
                    # PERSPECTIVE, CENTROID". A single MATCHES call caps out
                    # at nine capture groups (CMAKE_MATCH_9 is the last one
                    # CMake fills in), and the clauses above already use
                    # seven of them, so the fixed single trailing ", WORD"
                    # this pattern used to end on could not also match a
                    # second ", CENTROID" - the whole line fell to `unknown`
                    # below and the cross-check against the compiled .dksh
                    # reported a spurious location mismatch instead of
                    # reading the declaration (review comment 2856, N2). The
                    # rest of the line, after everything already read above,
                    # is matched again here, as its own independent regex
                    # with its own nine-group budget, against the 0, 1 or 2
                    # trailing ", WORD" fields that remain: interpolation
                    # mode, then centroid. centroid is recorded but, like
                    # this file's own interpolation field, never compared:
                    # shaders/*.manifest has no field for it, so the source
                    # check (MODE=source) refuses it outright instead.
                    set(in_tail "${CMAKE_MATCH_8}")
                    if(in_tail MATCHES "^(, ([A-Z]+))?(, ([A-Z]+))?$")
                        list(APPEND ins "in|index=${in_index}|mask=${mask}|semantic=${semantic}|slot=${semantic_index}|interpolation=${CMAKE_MATCH_2}|centroid=${CMAKE_MATCH_4}")
                    else()
                        list(APPEND unknown "${line}")
                    endif()
                elseif(line MATCHES "^DCL OUT\\[([0-9]+)\\](\\.([xyzw]+))?, ([A-Z]+)(\\[([0-9]+)\\])?(, .*)?$")
                    if(NOT "${CMAKE_MATCH_3}" STREQUAL "")
                        set(mask "${CMAKE_MATCH_3}")
                    endif()
                    set(semantic_index "${CMAKE_MATCH_6}")
                    if(semantic_index STREQUAL "")
                        set(semantic_index 0)
                    endif()
                    list(APPEND outs "out|index=${CMAKE_MATCH_1}|mask=${mask}|semantic=${CMAKE_MATCH_4}|slot=${semantic_index}")
                elseif(line MATCHES "^DCL CONST\\[([0-9]+)\\]\\[([0-9]+)(\\.\\.([0-9]+))?\\]$")
                    set(last "${CMAKE_MATCH_4}")
                    if(last STREQUAL "")
                        set(last "${CMAKE_MATCH_2}")
                    endif()
                    list(APPEND consts "const|index=${CMAKE_MATCH_1}|first=${CMAKE_MATCH_2}|last=${last}")
                elseif(line MATCHES "^DCL SAMP\\[([0-9]+)\\]$")
                    list(APPEND samps "samp|index=${CMAKE_MATCH_1}")
                elseif(line MATCHES "^DCL SVIEW\\[([0-9]+)\\], ([A-Z0-9_]+), ([A-Z]+)")
                    list(APPEND sviews "sview|index=${CMAKE_MATCH_1}|target=${CMAKE_MATCH_2}|return=${CMAKE_MATCH_3}")
                elseif(line MATCHES "^DCL (TEMP|ADDR|SV)[[]")
                    # Registers and system values: not part of the interface.
                else()
                    list(APPEND unknown "${line}")
                endif()
            endforeach()
        endif()
    endif()
    foreach(part IN ITEMS processor ins outs consts samps sviews unknown error)
        set(${prefix}_${part} "${${part}}" PARENT_SCOPE)
    endforeach()
endfunction()

# A TGSI component mask as bits.
function(_nfsu_sm_mask_bits letters out)
    set(bits 0)
    foreach(index IN ITEMS 0 1 2 3)
        string(SUBSTRING "xyzw" ${index} 1 letter)
        string(FIND "${letters}" "${letter}" at)
        if(NOT at EQUAL -1)
            math(EXPR bits "${bits} | (1 << ${index})")
        endif()
    endforeach()
    set(${out} ${bits} PARENT_SCOPE)
endfunction()

# Uniform blocks and textures of one stage, from its TGSI. CONST[0] is the
# driver constant buffer: uam refuses a uniform outside a block, so only the
# compiler's own state (the y flip of dFdy, for one) lands there, and it is
# not part of the manifest's interface. CONST[k] for k > 0 is the uniform
# block at binding k - 1, and its declared range is the block's whole size.
function(_nfsu_sm_check_tgsi_bindings stage tgsi_name prefix out)
    set(findings "")
    set(want_blocks "")
    foreach(record IN LISTS _sm_blocks)
        _nfsu_sm_field("${record}" stages stages)
        string(REPLACE "," ";" stage_list "${stages}")
        if(stage IN_LIST stage_list)
            list(APPEND want_blocks "${record}")
        endif()
    endforeach()
    set(seen_bindings "")
    foreach(const IN LISTS ${prefix}_consts)
        _nfsu_sm_field("${const}" index index)
        _nfsu_sm_field("${const}" last last)
        if(index EQUAL 0)
            continue()
        endif()
        math(EXPR binding "${index} - 1")
        math(EXPR size "(${last} + 1) * 16")
        list(APPEND seen_bindings ${binding})
        _nfsu_sm_find("${want_blocks}" binding "${binding}" want)
        if(want STREQUAL "")
            list(APPEND findings "the compiled ${stage} shader reads a ${size}-byte uniform block at binding ${binding} (${tgsi_name}: CONST[${index}]), which the manifest does not give that stage")
            continue()
        endif()
        _nfsu_sm_field("${want}" name name)
        _nfsu_sm_field("${want}" size want_size)
        if(NOT size EQUAL want_size)
            list(APPEND findings "uniform block ${name} at ${stage} binding ${binding}: the manifest says ${want_size} bytes, the compiled shader declares ${size}")
        endif()
    endforeach()
    foreach(want IN LISTS want_blocks)
        _nfsu_sm_field("${want}" name name)
        _nfsu_sm_field("${want}" binding binding)
        if(NOT binding IN_LIST seen_bindings)
            list(APPEND findings "uniform block ${name} is in the manifest at ${stage} binding ${binding}, but the compiled ${stage} shader has no uniform block there")
        endif()
    endforeach()

    set(want_textures "")
    foreach(record IN LISTS _sm_textures)
        _nfsu_sm_field("${record}" stage texture_stage)
        if(texture_stage STREQUAL stage)
            list(APPEND want_textures "${record}")
        endif()
    endforeach()
    set(seen_textures "")
    foreach(samp IN LISTS ${prefix}_samps)
        _nfsu_sm_field("${samp}" index binding)
        list(APPEND seen_textures ${binding})
        _nfsu_sm_find("${${prefix}_sviews}" index "${binding}" view)
        _nfsu_sm_field("${view}" target target)
        _nfsu_sm_field("${view}" return return_type)
        _nfsu_sm_find("${want_textures}" binding "${binding}" want)
        if(want STREQUAL "")
            list(APPEND findings "the compiled ${stage} shader samples a texture at binding ${binding} (${tgsi_name}: SAMP[${binding}]), which the manifest does not give that stage")
            continue()
        endif()
        _nfsu_sm_field("${want}" name name)
        if(NOT target STREQUAL "2D" OR NOT return_type STREQUAL "FLOAT")
            list(APPEND findings "texture ${name} at ${stage} binding ${binding}: the manifest says sampler2D, the compiled shader declares a ${target} ${return_type} view")
        endif()
    endforeach()
    foreach(want IN LISTS want_textures)
        _nfsu_sm_field("${want}" name name)
        _nfsu_sm_field("${want}" binding binding)
        if(NOT binding IN_LIST seen_textures)
            list(APPEND findings "texture ${name} is in the manifest at ${stage} binding ${binding}, but the compiled ${stage} shader samples nothing there")
        endif()
    endforeach()
    foreach(line IN LISTS ${prefix}_unknown)
        list(APPEND findings "${tgsi_name} declares `${line}`, which no manifest record kind describes")
    endforeach()
    set(${out} "${findings}" PARENT_SCOPE)
endfunction()

function(_nfsu_sm_check_vertex_blob dksh tgsi out)
    set(findings "")
    unset(sph_written)
    get_filename_component(dksh_name "${dksh}" NAME)
    get_filename_component(tgsi_name "${tgsi}" NAME)
    _nfsu_sm_read_dksh("${dksh}" blob)
    _nfsu_sm_read_tgsi("${tgsi}" tgsi)
    if(NOT blob_error STREQUAL "")
        list(APPEND findings "${blob_error}")
    elseif(NOT blob_type EQUAL 0)
        list(APPEND findings "${dksh_name} holds a program of type ${blob_type}. A vertex program is type 0")
    else()
        list(GET blob_sph 0 word0)
        math(EXPR sph_type "${word0} & 31")
        math(EXPR shader_type "(${word0} >> 10) & 15")
        if(NOT sph_type EQUAL 1 OR NOT shader_type EQUAL 1)
            list(APPEND findings "${dksh_name}: the shader program header is type ${sph_type}, stage ${shader_type}. A vertex program is 1, 1")
        else()
            # Words 6..9: four bits per generic attribute, one per component,
            # for the attributes the program fetches.
            set(fetched 0)
            foreach(location RANGE 0 31)
                math(EXPR word_index "6 + ${location} / 8")
                list(GET blob_sph ${word_index} word)
                math(EXPR got "(${word} >> (4 * (${location} % 8))) & 15")
                _nfsu_sm_find("${_sm_attributes}" location "${location}" want)
                set(want_mask 0)
                if(NOT want STREQUAL "")
                    _nfsu_sm_field("${want}" name want_name)
                    _nfsu_sm_field("${want}" type want_type)
                    _nfsu_sm_components("${want_type}" components)
                    math(EXPR want_mask "(1 << ${components}) - 1")
                endif()
                if(NOT got EQUAL 0)
                    math(EXPR fetched "${fetched} + 1")
                endif()
                if(got EQUAL want_mask)
                    continue()
                endif()
                _nfsu_sm_mask_letters(${got} got_letters)
                if(want STREQUAL "")
                    list(APPEND findings "${dksh_name} fetches vertex attribute location ${location} (components ${got_letters}), which the manifest does not declare")
                elseif(got EQUAL 0)
                    list(APPEND findings "vertex attribute location ${location} (${want_type} ${want_name}) is in the manifest, but ${dksh_name} does not fetch it")
                else()
                    _nfsu_sm_mask_letters(${want_mask} want_letters)
                    list(APPEND findings "vertex attribute location ${location} (${want_name}): the manifest says ${want_type} (${want_letters}), ${dksh_name} fetches ${got_letters}")
                endif()
            endforeach()
            # The TGSI numbers vertex inputs consecutively, so only its count
            # is comparable; a different count means the two files are not
            # the output of one compile.
            list(LENGTH tgsi_ins tgsi_inputs)
            if(tgsi_error STREQUAL "" AND NOT tgsi_inputs EQUAL fetched)
                list(APPEND findings "${tgsi_name} declares ${tgsi_inputs} vertex inputs and ${dksh_name} fetches ${fetched}: they are not one compile")
            endif()

            # Word 13 from bit 12: four bits for the position, then four per
            # generic output through word 17, the components the program
            # writes. Generic output n is varying location n.
            list(GET blob_sph 13 word)
            math(EXPR sph_position "(${word} >> 12) & 15")
            if(NOT sph_position EQUAL 15)
                _nfsu_sm_mask_letters(${sph_position} got_letters)
                list(APPEND findings "${dksh_name} writes position components ${got_letters}, not xyzw")
            endif()
            set(sph_written "")
            foreach(location RANGE 0 31)
                math(EXPR bit "16 + 4 * ${location}")
                math(EXPR word_index "13 + ${bit} / 32")
                list(GET blob_sph ${word_index} word)
                math(EXPR got "(${word} >> (${bit} % 32)) & 15")
                list(APPEND sph_written ${got})
                _nfsu_sm_find("${_sm_varyings}" location "${location}" want)
                set(want_mask 0)
                if(NOT want STREQUAL "")
                    _nfsu_sm_field("${want}" name want_name)
                    _nfsu_sm_field("${want}" type want_type)
                    _nfsu_sm_components("${want_type}" components)
                    math(EXPR want_mask "(1 << ${components}) - 1")
                endif()
                if(got EQUAL want_mask)
                    continue()
                endif()
                _nfsu_sm_mask_letters(${got} got_letters)
                if(want STREQUAL "")
                    list(APPEND findings "${dksh_name} writes varying location ${location} (${got_letters}), which the manifest does not declare")
                elseif(got EQUAL 0)
                    list(APPEND findings "varying location ${location} (${want_type} ${want_name}) is in the manifest, but ${dksh_name} does not write it")
                else()
                    _nfsu_sm_mask_letters(${want_mask} want_letters)
                    list(APPEND findings "varying location ${location} (${want_name}): the manifest says ${want_type} (${want_letters}), ${dksh_name} writes ${got_letters}")
                endif()
            endforeach()
        endif()
    endif()

    if(NOT tgsi_error STREQUAL "")
        list(APPEND findings "${tgsi_error}")
    elseif(NOT tgsi_processor STREQUAL "VERT")
        list(APPEND findings "${tgsi_name} is a ${tgsi_processor} program, not VERT")
    else()
        # The outputs again, in the TGSI's terms: GENERIC[n] is varying
        # location n. The varyings were held to the manifest from the blob
        # above, so here the TGSI is held to the blob, and an output the SPH
        # does not map at all is reported from here.
        set(tgsi_written "")
        foreach(output IN LISTS tgsi_outs)
            _nfsu_sm_field("${output}" semantic semantic)
            _nfsu_sm_field("${output}" slot slot)
            _nfsu_sm_field("${output}" mask mask)
            if(semantic STREQUAL "POSITION")
                continue()
            endif()
            if(NOT semantic STREQUAL "GENERIC")
                list(APPEND findings "the compiled vertex shader writes ${semantic}[${slot}], which the manifest does not describe")
                continue()
            endif()
            list(APPEND tgsi_written ${slot})
            if(DEFINED sph_written AND slot LESS 32)
                list(GET sph_written ${slot} blob_bits)
                _nfsu_sm_mask_bits("${mask}" mask_bits)
                if(NOT mask_bits EQUAL blob_bits)
                    list(APPEND findings "${tgsi_name} and ${dksh_name} disagree on varying location ${slot}: they are not one compile")
                endif()
            endif()
        endforeach()
        if(DEFINED sph_written)
            foreach(location RANGE 0 31)
                list(GET sph_written ${location} blob_bits)
                if(NOT blob_bits EQUAL 0 AND NOT location IN_LIST tgsi_written)
                    list(APPEND findings "${tgsi_name} and ${dksh_name} disagree on varying location ${location}: they are not one compile")
                endif()
            endforeach()
        endif()
        _nfsu_sm_check_tgsi_bindings(vertex "${tgsi_name}" tgsi found)
        list(APPEND findings ${found})
    endif()
    set(${out} "${findings}" PARENT_SCOPE)
endfunction()

function(_nfsu_sm_check_fragment_blob dksh tgsi out)
    set(findings "")
    get_filename_component(dksh_name "${dksh}" NAME)
    get_filename_component(tgsi_name "${tgsi}" NAME)
    _nfsu_sm_read_dksh("${dksh}" blob)
    _nfsu_sm_read_tgsi("${tgsi}" tgsi)
    set(interpolation_codes "unused;flat;smooth;noperspective")
    if(NOT blob_error STREQUAL "")
        list(APPEND findings "${blob_error}")
    elseif(NOT blob_type EQUAL 1)
        list(APPEND findings "${dksh_name} holds a program of type ${blob_type}. A fragment program is type 1")
    else()
        list(GET blob_sph 0 word0)
        math(EXPR sph_type "${word0} & 31")
        math(EXPR shader_type "(${word0} >> 10) & 15")
        if(NOT sph_type EQUAL 2 OR NOT shader_type EQUAL 5)
            list(APPEND findings "${dksh_name}: the shader program header is type ${sph_type}, stage ${shader_type}. A fragment program is 2, 5")
        else()
            # Words 6..13: two bits per component of each generic input, the
            # interpolation the hardware applies: 1 flat, 2 perspective (GLSL
            # smooth), 3 screen-linear (GLSL noperspective).
            set(sph_read "")
            foreach(location RANGE 0 31)
                math(EXPR word_index "6 + ${location} / 4")
                list(GET blob_sph ${word_index} word)
                set(got_mask 0)
                set(got_modes "")
                foreach(component IN ITEMS 0 1 2 3)
                    math(EXPR mode "(${word} >> (8 * (${location} % 4) + 2 * ${component})) & 3")
                    if(NOT mode EQUAL 0)
                        math(EXPR got_mask "${got_mask} | (1 << ${component})")
                        list(GET interpolation_codes ${mode} mode_name)
                        if(NOT mode_name IN_LIST got_modes)
                            list(APPEND got_modes ${mode_name})
                        endif()
                    endif()
                endforeach()
                list(APPEND sph_read ${got_mask})
                _nfsu_sm_find("${_sm_varyings}" location "${location}" want)
                set(want_mask 0)
                if(NOT want STREQUAL "")
                    _nfsu_sm_field("${want}" name want_name)
                    _nfsu_sm_field("${want}" type want_type)
                    _nfsu_sm_field("${want}" interpolation want_interpolation)
                    _nfsu_sm_components("${want_type}" components)
                    math(EXPR want_mask "(1 << ${components}) - 1")
                endif()
                _nfsu_sm_mask_letters(${got_mask} got_letters)
                string(REPLACE ";" "+" got_modes_text "${got_modes}")
                if(want STREQUAL "")
                    if(NOT got_mask EQUAL 0)
                        list(APPEND findings "${dksh_name} reads varying location ${location} (${got_letters}, ${got_modes_text}), which the manifest does not declare")
                    endif()
                    continue()
                endif()
                if(got_mask EQUAL 0)
                    list(APPEND findings "varying location ${location} (${want_type} ${want_name}) is in the manifest, but ${dksh_name} does not read it")
                    continue()
                endif()
                if(NOT got_mask EQUAL want_mask)
                    _nfsu_sm_mask_letters(${want_mask} want_letters)
                    list(APPEND findings "varying location ${location} (${want_name}): the manifest says ${want_type} (${want_letters}), ${dksh_name} reads ${got_letters}")
                endif()
                if(NOT got_modes STREQUAL want_interpolation)
                    list(APPEND findings "varying location ${location} (${want_name}): the manifest says ${want_interpolation}, ${dksh_name} interpolates it ${got_modes_text}")
                endif()
            endforeach()
            # Word 18: four bits per render target, the components written.
            list(GET blob_sph 18 targets)
            foreach(location RANGE 0 7)
                math(EXPR got "(${targets} >> (4 * ${location})) & 15")
                _nfsu_sm_find("${_sm_outputs}" location "${location}" want)
                set(want_mask 0)
                if(NOT want STREQUAL "")
                    _nfsu_sm_field("${want}" name want_name)
                    _nfsu_sm_field("${want}" type want_type)
                    _nfsu_sm_components("${want_type}" components)
                    math(EXPR want_mask "(1 << ${components}) - 1")
                endif()
                if(got EQUAL want_mask)
                    continue()
                endif()
                _nfsu_sm_mask_letters(${got} got_letters)
                if(want STREQUAL "")
                    list(APPEND findings "${dksh_name} writes render target ${location} (${got_letters}), which the manifest does not declare")
                elseif(got EQUAL 0)
                    list(APPEND findings "output location ${location} (${want_name}) is in the manifest, but ${dksh_name} does not write it")
                else()
                    _nfsu_sm_mask_letters(${want_mask} want_letters)
                    list(APPEND findings "output location ${location} (${want_name}): the manifest says ${want_type} (${want_letters}), ${dksh_name} writes ${got_letters}")
                endif()
            endforeach()
            # The TGSI says the same thing in its own terms; a disagreement
            # either way means the two files are not the output of one
            # compile.
            if(tgsi_error STREQUAL "")
                set(tgsi_read "")
                foreach(input IN LISTS tgsi_ins)
                    _nfsu_sm_field("${input}" semantic semantic)
                    _nfsu_sm_field("${input}" slot slot)
                    _nfsu_sm_field("${input}" mask mask)
                    if(NOT semantic STREQUAL "GENERIC" OR NOT slot LESS 32)
                        continue()
                    endif()
                    list(APPEND tgsi_read ${slot})
                    list(GET sph_read ${slot} blob_bits)
                    _nfsu_sm_mask_bits("${mask}" mask_bits)
                    if(NOT mask_bits EQUAL blob_bits)
                        list(APPEND findings "${tgsi_name} and ${dksh_name} disagree on varying location ${slot}: they are not one compile")
                    endif()
                endforeach()
                foreach(location RANGE 0 31)
                    list(GET sph_read ${location} blob_bits)
                    if(NOT blob_bits EQUAL 0 AND NOT location IN_LIST tgsi_read)
                        list(APPEND findings "${tgsi_name} and ${dksh_name} disagree on varying location ${location}: they are not one compile")
                    endif()
                endforeach()
            endif()
        endif()
    endif()

    if(NOT tgsi_error STREQUAL "")
        list(APPEND findings "${tgsi_error}")
    elseif(NOT tgsi_processor STREQUAL "FRAG")
        list(APPEND findings "${tgsi_name} is a ${tgsi_processor} program, not FRAG")
    else()
        foreach(output IN LISTS tgsi_outs)
            _nfsu_sm_field("${output}" semantic semantic)
            _nfsu_sm_field("${output}" slot slot)
            if(NOT semantic STREQUAL "COLOR")
                list(APPEND findings "the compiled fragment shader writes ${semantic}[${slot}], which the manifest does not describe")
            endif()
        endforeach()
        _nfsu_sm_check_tgsi_bindings(fragment "${tgsi_name}" tgsi found)
        list(APPEND findings ${found})
    endif()
    set(${out} "${findings}" PARENT_SCOPE)
endfunction()

# Prints a check's findings, or its closing line when there are none, and
# sets <out> to the line that fails the run, or to "".
function(_nfsu_sm_report program what count findings out)
    set(${out} "" PARENT_SCOPE)
    list(LENGTH findings found)
    if(found EQUAL 0)
        message(STATUS "shader interface: ${program}: ${what}: ${count} records, no drift")
        return()
    endif()
    foreach(finding IN LISTS findings)
        message(NOTICE "shader interface drift: ${program}: ${finding}")
    endforeach()
    set(${out} "shader interface: ${program}: ${found} drift finding(s) against ${what}" PARENT_SCOPE)
endfunction()

function(_nfsu_sm_record_count out)
    set(count 0)
    foreach(kind IN ITEMS buffers attributes varyings blocks members textures outputs variants)
        list(LENGTH _sm_${kind} n)
        math(EXPR count "${count} + ${n}")
    endforeach()
    set(${out} ${count} PARENT_SCOPE)
endfunction()

# The public checks set <out> to "" when they pass and to the line that
# fails the run otherwise; the script entry point below raises it, so the
# failure is reported once and at the top level.
function(nfsu_shader_check_source manifest out)
    _nfsu_sm_parse("${manifest}")
    _nfsu_sm_parse_failure("${manifest}" "${_sm_errors}" failure)
    if(NOT failure STREQUAL "")
        set(${out} "${failure}" PARENT_SCOPE)
        return()
    endif()
    get_filename_component(directory "${manifest}" DIRECTORY)
    set(findings "")
    foreach(stage IN ITEMS vertex fragment)
        _nfsu_sm_check_source_stage(${stage} "${directory}/${_sm_${stage}_source}" found)
        list(APPEND findings ${found})
    endforeach()
    _nfsu_sm_record_count(count)
    _nfsu_sm_report("${_sm_program}"
        "${_sm_vertex_source} and ${_sm_fragment_source}" ${count} "${findings}" failure)
    set(${out} "${failure}" PARENT_SCOPE)
endfunction()

function(nfsu_shader_check_blob manifest vertex_dksh vertex_tgsi fragment_dksh fragment_tgsi stamp out)
    _nfsu_sm_parse("${manifest}")
    _nfsu_sm_parse_failure("${manifest}" "${_sm_errors}" failure)
    if(NOT failure STREQUAL "")
        set(${out} "${failure}" PARENT_SCOPE)
        return()
    endif()
    _nfsu_sm_check_vertex_blob("${vertex_dksh}" "${vertex_tgsi}" vertex_findings)
    _nfsu_sm_check_fragment_blob("${fragment_dksh}" "${fragment_tgsi}" fragment_findings)
    get_filename_component(vertex_name "${vertex_dksh}" NAME)
    get_filename_component(fragment_name "${fragment_dksh}" NAME)
    _nfsu_sm_record_count(count)
    set(findings ${vertex_findings})
    list(APPEND findings ${fragment_findings})
    _nfsu_sm_report("${_sm_program}" "the compiled ${vertex_name} and ${fragment_name}"
        ${count} "${findings}" failure)
    set(${out} "${failure}" PARENT_SCOPE)
    if(failure STREQUAL "" AND NOT stamp STREQUAL "")
        file(WRITE "${stamp}" "${_sm_program}: no drift\n")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# The C++ header.

function(nfsu_shader_generate_header manifest_dir output out)
    set(${out} "" PARENT_SCOPE)
    file(GLOB manifests "${manifest_dir}/*.manifest")
    list(SORT manifests)
    if(manifests STREQUAL "")
        set(${out} "shader interface: no manifest in ${manifest_dir}" PARENT_SCOPE)
        return()
    endif()
    set(headers "")
    set(body "")
    set(table "")
    foreach(manifest IN LISTS manifests)
        _nfsu_sm_parse("${manifest}")
        _nfsu_sm_parse_failure("${manifest}" "${_sm_errors}" failure)
        if(NOT failure STREQUAL "")
            set(${out} "${failure}" PARENT_SCOPE)
            return()
        endif()
        set(p "${_sm_program}")
        get_filename_component(manifest_name "${manifest}" NAME)
        string(APPEND body "\n// shaders/${manifest_name}\nnamespace ${p} {\n\n")

        set(buffer_structs "")
        string(APPEND body "inline constexpr Buffer kBuffers[] = {\n")
        foreach(record IN LISTS _sm_buffers)
            foreach(key IN ITEMS index stride struct header)
                _nfsu_sm_field("${record}" ${key} ${key})
            endforeach()
            list(APPEND headers "${header}")
            list(APPEND buffer_structs "${index}:${struct}")
            string(APPEND body "    {${index}, ${stride}, \"${struct}\", sizeof(${struct})},\n")
        endforeach()
        string(APPEND body "};\n\n")

        string(APPEND body "inline constexpr Attribute kAttributes[] = {\n")
        foreach(record IN LISTS _sm_attributes)
            foreach(key IN ITEMS location name type format buffer offset members)
                _nfsu_sm_field("${record}" ${key} ${key})
            endforeach()
            _nfsu_sm_components("${type}" components)
            _nfsu_sm_format("${format}" format_components format_bytes)
            set(struct "")
            foreach(pair IN LISTS buffer_structs)
                if(pair MATCHES "^${buffer}:(.+)$")
                    set(struct "${CMAKE_MATCH_1}")
                endif()
            endforeach()
            string(REPLACE "," ";" member_list "${members}")
            list(LENGTH member_list member_count)
            set(offsets "")
            set(sizes "")
            set(elements "")
            set(element_sizes "")
            foreach(member IN LISTS member_list)
                list(APPEND offsets "offsetof(${struct}, ${member})")
                list(APPEND sizes "sizeof(${struct}::${member})")
                list(APPEND elements "element_of<decltype(${struct}::${member})>()")
                list(APPEND element_sizes "sizeof(std::remove_all_extents_t<decltype(${struct}::${member})>)")
            endforeach()
            list(LENGTH offsets padded)
            while(padded LESS 4)
                list(APPEND offsets "0")
                list(APPEND sizes "0")
                list(APPEND elements "Element::none")
                list(APPEND element_sizes "0")
                math(EXPR padded "${padded} + 1")
            endwhile()
            string(REPLACE ";" ", " offsets "${offsets}")
            string(REPLACE ";" ", " sizes "${sizes}")
            string(REPLACE ";" ",\n      " elements "${elements}")
            string(REPLACE ";" ",\n      " element_sizes "${element_sizes}")
            string(APPEND body "    {${location}, \"${name}\", \"${type}\", ${components}, VertexFormat::${format}, ${format_components}, ${format_bytes}, ${buffer}, ${offset}, ${member_count},\n")
            string(APPEND body "     {${offsets}},\n")
            string(APPEND body "     {${sizes}},\n")
            string(APPEND body "     {${elements}},\n")
            string(APPEND body "     {${element_sizes}}},\n")
        endforeach()
        string(APPEND body "};\n\n")

        string(APPEND body "inline constexpr Varying kVaryings[] = {\n")
        foreach(record IN LISTS _sm_varyings)
            foreach(key IN ITEMS location name type interpolation)
                _nfsu_sm_field("${record}" ${key} ${key})
            endforeach()
            _nfsu_sm_components("${type}" components)
            string(APPEND body "    {${location}, \"${name}\", \"${type}\", ${components}, Interpolation::${interpolation}},\n")
        endforeach()
        string(APPEND body "};\n\n")

        string(APPEND body "inline constexpr Block kBlocks[] = {\n")
        foreach(record IN LISTS _sm_blocks)
            foreach(key IN ITEMS name binding stages size)
                _nfsu_sm_field("${record}" ${key} ${key})
            endforeach()
            string(REPLACE "," ";" stage_list "${stages}")
            set(vertex false)
            set(fragment false)
            if("vertex" IN_LIST stage_list)
                set(vertex true)
            endif()
            if("fragment" IN_LIST stage_list)
                set(fragment true)
            endif()
            string(APPEND body "    {\"${name}\", ${binding}, ${vertex}, ${fragment}, ${size}},\n")
        endforeach()
        string(APPEND body "};\n\n")

        string(APPEND body "inline constexpr Member kMembers[] = {\n")
        foreach(record IN LISTS _sm_members)
            foreach(key IN ITEMS block name type offset)
                _nfsu_sm_field("${record}" ${key} ${key})
            endforeach()
            string(APPEND body "    {\"${block}\", \"${name}\", \"${type}\", ${offset}},\n")
        endforeach()
        string(APPEND body "};\n\n")

        set(texture_count 0)
        if(NOT _sm_textures STREQUAL "")
            string(APPEND body "inline constexpr Texture kTextures[] = {\n")
            foreach(record IN LISTS _sm_textures)
                foreach(key IN ITEMS name binding stage type filter wrap)
                    _nfsu_sm_field("${record}" ${key} ${key})
                endforeach()
                string(APPEND body "    {\"${name}\", ${binding}, Stage::${stage}, \"${type}\", Filter::${filter}, Wrap::${wrap}},\n")
                math(EXPR texture_count "${texture_count} + 1")
            endforeach()
            string(APPEND body "};\n\n")
        endif()

        string(APPEND body "inline constexpr Output kOutputs[] = {\n")
        foreach(record IN LISTS _sm_outputs)
            foreach(key IN ITEMS location name type format)
                _nfsu_sm_field("${record}" ${key} ${key})
            endforeach()
            _nfsu_sm_components("${type}" components)
            string(APPEND body "    {${location}, \"${name}\", \"${type}\", ${components}},\n")
        endforeach()
        string(APPEND body "};\n\n")

        string(APPEND body "inline constexpr Variant kVariants[] = {\n")
        foreach(record IN LISTS _sm_variants)
            foreach(key IN ITEMS name alpha_test alpha_ref blend blend_src blend_dst blend_op depth_test depth_write color_write cull)
                _nfsu_sm_field("${record}" ${key} ${key})
            endforeach()
            if(alpha_ref STREQUAL "")
                set(alpha_ref 0)
            endif()
            set(blend_on false)
            if(blend STREQUAL "on")
                set(blend_on true)
            else()
                # Unused with blending off; the pass-through factors.
                set(blend_src one)
                set(blend_dst zero)
                set(blend_op add)
            endif()
            set(depth_write_on false)
            if(depth_write STREQUAL "on")
                set(depth_write_on true)
            endif()
            string(APPEND body "    {\"${name}\", CompareOp::${alpha_test}, ${alpha_ref}, ${blend_on}, BlendFactor::${blend_src}, BlendFactor::${blend_dst}, BlendOp::${blend_op},\n")
            string(APPEND body "     CompareOp::${depth_test}, ${depth_write_on}, ColorWrite::${color_write}, Cull::${cull}},\n")
        endforeach()
        string(APPEND body "};\n\n} // namespace ${p}\n")

        set(textures_ref "nullptr, 0")
        if(texture_count GREATER 0)
            set(textures_ref "${p}::kTextures, std::size(${p}::kTextures)")
        endif()
        string(APPEND table "    {\"${p}\", \"${_sm_vertex_source}\", \"${_sm_fragment_source}\",\n")
        string(APPEND table "     ${p}::kBuffers, std::size(${p}::kBuffers),\n")
        string(APPEND table "     ${p}::kAttributes, std::size(${p}::kAttributes),\n")
        string(APPEND table "     ${p}::kVaryings, std::size(${p}::kVaryings),\n")
        string(APPEND table "     ${p}::kBlocks, std::size(${p}::kBlocks),\n")
        string(APPEND table "     ${p}::kMembers, std::size(${p}::kMembers),\n")
        string(APPEND table "     ${textures_ref},\n")
        string(APPEND table "     ${p}::kOutputs, std::size(${p}::kOutputs),\n")
        string(APPEND table "     ${p}::kVariants, std::size(${p}::kVariants)},\n")
    endforeach()

    list(REMOVE_DUPLICATES headers)
    list(SORT headers)
    set(includes "")
    foreach(header IN LISTS headers)
        string(APPEND includes "#include \"${header}\"\n")
    endforeach()

    set(text "// Generated by cmake/shader_manifest.cmake from shaders/*.manifest.
// Do not edit: edit the manifests. docs/SHADER_PIPELINE.md defines them.
//
// Every table below restates one manifest record. Each vertex attribute
// also carries the offsetof and sizeof of the C++ members it names, and the
// kind and size of their elements, taken from the struct its buffer record
// names, so this header does not compile against a vertex struct that has
// lost one of them, and tests/shader_manifest_tests.cpp compares the rest.
#pragma once

${includes}
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

namespace nfsu::shader_manifest {

enum class Stage : std::uint8_t { vertex, fragment };
enum class VertexFormat : std::uint8_t {
    float32x1, float32x2, float32x3, float32x4,
    unorm8x4, snorm8x4, uint8x4, unorm16x2, snorm16x2
};
enum class Interpolation : std::uint8_t { smooth, flat, noperspective };
enum class Filter : std::uint8_t { nearest, linear };
enum class Wrap : std::uint8_t { repeat, clamp_to_edge, mirrored_repeat };
enum class CompareOp : std::uint8_t {
    never, less, equal, less_equal, greater, not_equal, greater_equal, always
};
enum class BlendFactor : std::uint8_t {
    zero, one, src_color, one_minus_src_color, src_alpha, one_minus_src_alpha,
    dst_alpha, one_minus_dst_alpha, dst_color, one_minus_dst_color
};
enum class BlendOp : std::uint8_t { add, subtract, reverse_subtract, min, max };
enum class ColorWrite : std::uint8_t { rgb, rgba };
enum class Cull : std::uint8_t { none, back, front };

// What one element of a C++ member is: the member itself, or the element
// type of an array member such as MeshVertex::color.
enum class Element : std::uint8_t {
    none, floating, signed_integer, unsigned_integer, other
};

template <typename Member>
constexpr Element element_of() {
    using E = std::remove_cv_t<std::remove_all_extents_t<Member>>;
    if constexpr (std::is_floating_point_v<E>) {
        return Element::floating;
    } else if constexpr (std::is_integral_v<E> && std::is_signed_v<E>) {
        return Element::signed_integer;
    } else if constexpr (std::is_integral_v<E>) {
        return Element::unsigned_integer;
    } else {
        return Element::other;
    }
}

struct Buffer {
    std::uint32_t index;
    std::uint32_t stride;
    const char* type_name;
    std::size_t type_size;
};

struct Attribute {
    std::uint32_t location;
    const char* name;
    const char* type;
    std::uint32_t components;
    VertexFormat format;
    std::uint32_t format_components;
    std::uint32_t format_bytes;
    std::uint32_t buffer;
    std::uint32_t offset;
    // The C++ members the attribute's bytes come from, in order, and the
    // kind and size of each one's elements.
    std::size_t member_count;
    std::size_t member_offset[4];
    std::size_t member_size[4];
    Element member_element[4];
    std::size_t member_element_size[4];
};

struct Varying {
    std::uint32_t location;
    const char* name;
    const char* type;
    std::uint32_t components;
    Interpolation interpolation;
};

struct Block {
    const char* name;
    std::uint32_t binding;
    bool vertex;
    bool fragment;
    std::uint32_t size;
};

struct Member {
    const char* block;
    const char* name;
    const char* type;
    std::uint32_t offset;
};

struct Texture {
    const char* name;
    std::uint32_t binding;
    Stage stage;
    const char* type;
    Filter filter;
    Wrap wrap;
};

struct Output {
    std::uint32_t location;
    const char* name;
    const char* type;
    std::uint32_t components;
};

struct Variant {
    const char* name;
    CompareOp alpha_test;
    std::uint32_t alpha_ref;
    bool blend;
    BlendFactor blend_src;
    BlendFactor blend_dst;
    BlendOp blend_op;
    CompareOp depth_test;
    bool depth_write;
    ColorWrite color_write;
    Cull cull;
};

struct Program {
    const char* name;
    const char* vertex_source;
    const char* fragment_source;
    const Buffer* buffers;
    std::size_t buffer_count;
    const Attribute* attributes;
    std::size_t attribute_count;
    const Varying* varyings;
    std::size_t varying_count;
    const Block* blocks;
    std::size_t block_count;
    const Member* members;
    std::size_t member_count;
    const Texture* textures;
    std::size_t texture_count;
    const Output* outputs;
    std::size_t output_count;
    const Variant* variants;
    std::size_t variant_count;
};
${body}
inline constexpr Program kPrograms[] = {
${table}};

} // namespace nfsu::shader_manifest
")
    file(WRITE "${output}" "${text}")
    list(LENGTH manifests count)
    message(STATUS "shader interface: wrote ${output} from ${count} manifests")
endfunction()

# ---------------------------------------------------------------------------
# Script entry point.

if(CMAKE_SCRIPT_MODE_FILE AND CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT DEFINED MODE)
        message(FATAL_ERROR "shader_manifest.cmake: pass -DMODE=source, blob or header")
    endif()
    if(MODE STREQUAL "source")
        if(NOT DEFINED MANIFEST)
            message(FATAL_ERROR "shader_manifest.cmake: MODE=source needs -DMANIFEST")
        endif()
        nfsu_shader_check_source("${MANIFEST}" failure)
    elseif(MODE STREQUAL "blob")
        foreach(variable IN ITEMS MANIFEST VERTEX_DKSH VERTEX_TGSI FRAGMENT_DKSH FRAGMENT_TGSI)
            if(NOT DEFINED ${variable})
                message(FATAL_ERROR "shader_manifest.cmake: MODE=blob needs -D${variable}")
            endif()
        endforeach()
        if(NOT DEFINED STAMP)
            set(STAMP "")
        endif()
        nfsu_shader_check_blob("${MANIFEST}" "${VERTEX_DKSH}" "${VERTEX_TGSI}"
            "${FRAGMENT_DKSH}" "${FRAGMENT_TGSI}" "${STAMP}" failure)
    elseif(MODE STREQUAL "header")
        if(NOT DEFINED MANIFEST_DIR OR NOT DEFINED OUTPUT)
            message(FATAL_ERROR "shader_manifest.cmake: MODE=header needs -DMANIFEST_DIR and -DOUTPUT")
        endif()
        nfsu_shader_generate_header("${MANIFEST_DIR}" "${OUTPUT}" failure)
    else()
        message(FATAL_ERROR "shader_manifest.cmake: unknown MODE `${MODE}`")
    endif()
    if(NOT failure STREQUAL "")
        message(FATAL_ERROR "${failure}")
    endif()
endif()
