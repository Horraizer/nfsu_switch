# Both programs, against a path argument that is not plain ASCII.
#
# tests/arg_path_tests.cpp pins the conversion. This pins what the programs
# do with it, which is the half a unit test cannot reach: the exit status,
# the text on the console, and the absence of the one outcome issue #87 is
# about - std::terminate from an exception that escaped main() before
# anything was printed.
#
# Four things are checked, and the reasons the first three are written the
# way they are all come from one adversarial review of this file:
#
#   acceptance   a real file under a directory whose name is Cyrillic, and
#                the programs required to read *that* file. An earlier
#                version pointed them at a file that did not exist and
#                required "failed to read file size", which any decoding
#                that merely produced some path satisfied: a build that
#                decoded with the OEM code page instead of the ANSI one
#                passed it while opening a different directory. A byte
#                count and an existing-output refusal cannot be satisfied
#                that way.
#
#                The name is written as UTF-8 byte values so that this file
#                stays ASCII; CMake decodes them and passes the characters
#                to the program through the wide Windows API. What the
#                program then receives depends on the machine: an active
#                code page that can express Cyrillic gives it the letters,
#                one that cannot gives it '?' instead, before the program
#                runs and beyond its control. So this case accepts either
#                of two outcomes, requires the one that matches what
#                arrived, and prints which it got.
#
#   every entry  every command nfsu_probe registers, and every path-valued
#                option nfsu_viewer documents, run with a Cyrillic path and
#                required not to crash. Both inventories are read out of
#                the programs' own usage text rather than listed here, so a
#                command or an option added later is covered without anyone
#                remembering. The review reverted one of the sixteen probe
#                call sites to fs::path(argv[2]) and restored the exact
#                terminate of issue #87 while the whole suite stayed green,
#                because this file drove one command.
#
#   refusal      an argument holding '?'. This is the same on every Windows
#                installation whatever its code page, which is why the
#                refusal is pinned through it and not through a byte
#                sequence whose validity depends on the machine.
#
#                It depends on one thing worth knowing: the C runtime this
#                project builds with expands wildcards in the argument
#                vector before main() runs. "a?b" reaches the program
#                unchanged only because nothing in the fresh working
#                directory below matches it. If anything matching ever
#                lands there, the runtime rewrites the argument and this
#                case stops testing the guard - so the checks below require
#                the '?' to appear in the diagnostic, which fails loudly
#                instead of passing quietly.
#
#   prefix       "\\?\C:\dir\out.tga", the Windows extended-length prefix,
#                accepted rather than caught by the '?' rule. The project
#                supports that spelling (strip_extended_prefix in
#                src/render.cpp, and docs/RUNTIME_SKIN.md records a review
#                that fixed --skin-dump for it), so a rule that refused it
#                would quietly take away a supported spelling.
#
# Run from the build directory. VIEWER and PROBE are the built programs.

# Not decoration: a script run with -P starts with no policies set, and
# IN_LIST below is an unknown argument rather than an operator without
# CMP0057, which this sets.
cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED VIEWER OR NOT DEFINED PROBE)
    message(FATAL_ERROR "VIEWER and PROBE must both be defined")
endif()

set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_path_arguments")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# "Raz" in Cyrillic, spelled as the UTF-8 bytes of U+0420 U+0430 U+0437.
string(ASCII 208 160 208 176 208 183 cyrillic_name)
set(cyrillic_dir "${work}/${cyrillic_name}")
file(MAKE_DIRECTORY "${cyrillic_dir}")
if(NOT IS_DIRECTORY "${cyrillic_dir}")
    message(FATAL_ERROR
        "the directory with a Cyrillic name was not created, so the cases "
        "below would be testing nothing")
endif()

# A real file in it, of a size no other file here has. A program that
# decoded the path with the wrong code page names a directory that does not
# exist and cannot report this number.
string(REPEAT "nfsu" 1024 filler)
set(cyrillic_input "${cyrillic_dir}/GEOMETRY.BIN")
file(WRITE "${cyrillic_input}" "${filler}")
file(SIZE "${cyrillic_input}" input_size)
if(NOT input_size EQUAL 4096)
    message(FATAL_ERROR
        "the probe input is ${input_size} bytes, expected 4096; the byte "
        "count below is what makes this case measure the decoded path")
endif()

# An output file that already exists, so that nfsu_viewer's refusal to
# overwrite one proves it resolved the Cyrillic path to this very file.
set(cyrillic_output "${cyrillic_dir}/out.tga")
file(WRITE "${cyrillic_output}" "not a screenshot")

# Every artifact a run below could write, removed by exact name first. A
# leftover output file would make nfsu_viewer refuse to overwrite it, which
# is a refusal with its own message and its own exit status, and a case
# would then be pinning the wrong one.
set(viewer_output "${work}/out.tga")
file(REMOVE "${viewer_output}" "${viewer_output}.partial")

# An ASCII input pair for the cases that vary one option at a time. Neither
# file exists; the runs below are refused or fail at the first read, which
# is after every argument has been converted.
set(ascii_geometry "${work}/ascii_geometry.bin")
set(ascii_textures "${work}/ascii_textures.bin")

# Runs one program and reports both streams together and the exit status.
function(nfsu_run out_var code_var)
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${work}"
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
        RESULT_VARIABLE result)
    set(${out_var} "${stdout_text}${stderr_text}" PARENT_SCOPE)
    set(${code_var} "${result}" PARENT_SCOPE)
endfunction()

# Fails when `text` does not contain `needle` literally. Not MATCHES: the
# strings here carry '?', '*' and '\', every one of which means something
# else to a regular expression.
function(nfsu_require_text label text needle)
    string(FIND "${text}" "${needle}" found)
    if(found LESS 0)
        message(FATAL_ERROR
            "${label}: expected the output to contain\n  ${needle}\nand it "
            "reads:\n${text}")
    endif()
endfunction()

function(nfsu_forbid_text label text needle)
    string(FIND "${text}" "${needle}" found)
    if(NOT found LESS 0)
        message(FATAL_ERROR
            "${label}: the output must not contain\n  ${needle}\nand it "
            "reads:\n${text}")
    endif()
endfunction()

function(nfsu_require_code label code expected)
    if(NOT code STREQUAL "${expected}")
        message(FATAL_ERROR
            "${label}: expected exit status ${expected}, got ${code}")
    endif()
endfunction()

# The weakest thing worth requiring of a run, and the whole of issue #87: it
# ended by returning from main() rather than by dying. execute_process
# reports an abnormal end as a string - "Exit code 0xc0000409" - rather than
# as a number, so a code that is not decimal is a crash whether or not
# anything was printed.
function(nfsu_forbid_crash label out code)
    nfsu_forbid_text("${label}" "${out}" "terminate called")
    if(NOT code MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "${label}: the run did not exit normally (${code}) and its "
            "output reads:\n${out}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# acceptance: a real file under a directory whose name is Cyrillic
# ---------------------------------------------------------------------------

nfsu_run(out code "${PROBE}" meshes "${cyrillic_input}")
nfsu_forbid_crash("nfsu_probe, Cyrillic directory" "${out}" "${code}")
string(FIND "${out}" "holds '?' at offset" substituted)
if(substituted LESS 0)
    # The active code page expressed the name, so the program had the real
    # path. It has to have read this file and no other: the byte count is
    # the measurement, not the fact that something was opened.
    nfsu_require_text("nfsu_probe, Cyrillic directory" "${out}" "bytes: 4096")
    nfsu_forbid_text("nfsu_probe, Cyrillic directory" "${out}"
                     "failed to read file size")
    nfsu_require_code("nfsu_probe, Cyrillic directory" "${code}" 3)
    set(reached "read the 4096 bytes of the file under it")
else()
    # The active code page could not, so Windows substituted before the
    # program ran and it refused what it was actually handed.
    nfsu_require_code("nfsu_probe, Cyrillic directory" "${code}" 1)
    set(reached "the active code page substituted, and the run was refused")
endif()
message(STATUS "acceptance: nfsu_probe, ${reached}")

# The viewer, through its output argument rather than an input, so that the
# case turns on the decoded path naming a file that is already there.
nfsu_run(out code "${VIEWER}" "${ascii_geometry}" "${ascii_textures}"
         "${cyrillic_output}")
nfsu_forbid_crash("nfsu_viewer, Cyrillic output" "${out}" "${code}")
string(FIND "${out}" "holds '?' at offset" substituted)
if(substituted LESS 0)
    nfsu_require_text("nfsu_viewer, Cyrillic output" "${out}"
                      "refusing to overwrite the output file")
    nfsu_forbid_text("nfsu_viewer, Cyrillic output" "${out}"
                     "failed to read file size")
    nfsu_require_code("nfsu_viewer, Cyrillic output" "${code}" 1)
    set(reached "resolved the output path onto the file that is already there")
else()
    nfsu_require_code("nfsu_viewer, Cyrillic output" "${code}" 1)
    set(reached "the active code page substituted, and the run was refused")
endif()
message(STATUS "acceptance: nfsu_viewer, ${reached}")

# ---------------------------------------------------------------------------
# every entry: each registered command and each path-valued option
# ---------------------------------------------------------------------------

# The command inventory, read out of the program rather than listed here.
nfsu_run(usage usage_code "${PROBE}")
set(commands "")
string(REPLACE ";" "\\;" usage_escaped "${usage}")
string(REGEX REPLACE "\r?\n" ";" usage_lines "${usage_escaped}")
foreach(line IN LISTS usage_lines)
    if(line MATCHES "^  nfsu_probe ([^ ]+)")
        if(NOT CMAKE_MATCH_1 IN_LIST commands)
            list(APPEND commands "${CMAKE_MATCH_1}")
        endif()
    endif()
endforeach()
list(LENGTH commands command_count)
if(command_count LESS 10)
    message(FATAL_ERROR
        "only ${command_count} commands were read out of the nfsu_probe usage "
        "text, so this case would cover almost nothing. The usage text "
        "reads:\n${usage}")
endif()

# Several argument counts, because a command that refuses its own arity
# returns before it builds a path and would be covered in name only. These
# four reach the handler of every command registered today.
foreach(command IN LISTS commands)
    foreach(extra "" "0" "0;0" "0;0;0;0")
        nfsu_run(out code "${PROBE}" "${command}" "${cyrillic_input}" ${extra})
        nfsu_forbid_crash("nfsu_probe ${command} with a Cyrillic path" "${out}"
                          "${code}")
    endforeach()
endforeach()
message(STATUS
    "every entry: ${command_count} nfsu_probe commands took a Cyrillic path "
    "without crashing")

# The viewer's path-valued options, read out of its usage text the same way:
# every option line whose value is spelled F or FILE.
nfsu_run(viewer_usage viewer_usage_code "${VIEWER}")
set(options "")
string(REPLACE ";" "\\;" viewer_escaped "${viewer_usage}")
string(REGEX REPLACE "\r?\n" ";" viewer_lines "${viewer_escaped}")
foreach(line IN LISTS viewer_lines)
    if(line MATCHES "^  (--[a-z-]+) +(F|FILE)( |$)")
        if(NOT CMAKE_MATCH_1 IN_LIST options)
            list(APPEND options "${CMAKE_MATCH_1}")
        endif()
    endif()
endforeach()
list(LENGTH options option_count)
if(option_count LESS 5)
    message(FATAL_ERROR
        "only ${option_count} path-valued options were read out of the "
        "nfsu_viewer usage text, and there were five when this was written. "
        "An option whose value is spelled something other than F or FILE is "
        "not covered here; give it one of those spellings or teach this "
        "reader the new one. The usage text reads:\n${viewer_usage}")
endif()

foreach(option IN LISTS options)
    nfsu_run(out code "${VIEWER}" "${ascii_geometry}" "${ascii_textures}"
             "${viewer_output}" "${option}" "${cyrillic_input}")
    nfsu_forbid_crash("nfsu_viewer ${option} with a Cyrillic path" "${out}"
                      "${code}")
endforeach()
file(REMOVE "${viewer_output}" "${viewer_output}.partial")
message(STATUS
    "every entry: ${option_count} nfsu_viewer path options took a Cyrillic "
    "value without crashing")

# ---------------------------------------------------------------------------
# refusal: an argument holding '?'
# ---------------------------------------------------------------------------

if(CMAKE_HOST_WIN32)
    # Relative, and run from ${work}, so that the reported offset of the
    # '?' is a fixed number this file can name rather than one that moves
    # with the length of the build directory's path. Nothing in ${work}
    # matches "a?b", so the C runtime's wildcard expansion leaves it alone;
    # the requirement that the diagnostic quote the '?' is what would catch
    # it if that ever stopped being true.
    nfsu_run(out code "${VIEWER}" "a?b/GEOMETRY.BIN" TEXTURES.BIN out.tga)
    nfsu_forbid_text("nfsu_viewer, '?' argument" "${out}" "terminate called")
    nfsu_require_text("nfsu_viewer, '?' argument" "${out}" "holds '?' at offset 1")
    nfsu_require_text("nfsu_viewer, '?' argument" "${out}" "argument 1 ")
    nfsu_require_text("nfsu_viewer, '?' argument" "${out}" "\"a?b/GEOMETRY.BIN\"")
    # What the run prints once it is past the guard and reading the files
    # that are not there. A viewer that printed the refusal and carried on
    # would reach this.
    nfsu_forbid_text("nfsu_viewer, '?' argument" "${out}" "failed to read file size")
    nfsu_require_code("nfsu_viewer, '?' argument" "${code}" 1)
    if(EXISTS "${viewer_output}")
        message(FATAL_ERROR
            "nfsu_viewer wrote ${viewer_output} for a run it refused")
    endif()

    nfsu_run(out code "${PROBE}" meshes "a?b/GEOMETRY.BIN")
    nfsu_forbid_text("nfsu_probe, '?' argument" "${out}" "terminate called")
    nfsu_require_text("nfsu_probe, '?' argument" "${out}" "holds '?' at offset 1")
    nfsu_require_text("nfsu_probe, '?' argument" "${out}" "argument 2 ")
    nfsu_require_text("nfsu_probe, '?' argument" "${out}" "\"a?b/GEOMETRY.BIN\"")
    nfsu_forbid_text("nfsu_probe, '?' argument" "${out}" "failed to read file size")
    nfsu_require_code("nfsu_probe, '?' argument" "${code}" 1)
    message(STATUS "refusal: both programs refused a '?' argument with exit 1")

    # ---------------------------------------------------------------------
    # prefix: the same character, where it is the extended-length prefix
    # ---------------------------------------------------------------------

    nfsu_run(out code "${PROBE}" meshes "\\\\?\\${cyrillic_input}")
    nfsu_forbid_crash("nfsu_probe, extended-length prefix" "${out}" "${code}")
    nfsu_forbid_text("nfsu_probe, extended-length prefix" "${out}"
                     "holds '?' at offset")
    message(STATUS
        "prefix: the extended-length spelling was not caught by the '?' rule")
else()
    message(STATUS
        "refusal: not Windows, so '?' is an ordinary file-name character and "
        "there is nothing to refuse")
endif()

file(REMOVE_RECURSE "${work}")
