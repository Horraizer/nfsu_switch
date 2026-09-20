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
#   every        every path argument of every registered command and every
#   position     path-valued option, each driven with a Cyrillic path in
#                turn while the others stay ASCII. Positions, not commands:
#                the first version of this file drove one position per
#                program and an independent review reverted nine individual
#                call sites, each of which kept the suite green and each of
#                which then reproduced the issue #87 crash. That section
#                carries the detail and the site numbers.
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
#                accepted rather than caught by the '?' rule. It is a
#                legitimate spelling that strip_extended_prefix in
#                src/render.cpp already handles in the overwrite and
#                same-file alias checks, so a rule that refused it would
#                take away what those checks accept today.
#
#                This case asserts the exemption and nothing more, because
#                there is nothing more to assert: the prefix does not open
#                a file through this standard library, identically here and
#                on origin/main, and docs/PATH_ARGUMENTS.md measures that.
#                A case requiring the run to resolve the file would be
#                requiring a fix to something outside issue #87.
#
# Run from the build directory. VIEWER and PROBE are the built programs;
# MAIN_SOURCE and VIEWER_SOURCE are their sources, read only to count how
# many arguments each one converts.

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
# every position: a Cyrillic path in each path argument in turn
# ---------------------------------------------------------------------------

# What this section is for, and the mistake it replaces. The first version
# of this file drove the Cyrillic path through one position per program -
# argv[2] for every probe command, the option value for every viewer option
# - and said in the commit message that it covered every command and every
# option. Both were true and neither was the property that mattered. An
# independent review reverted nine individual call sites, one at a time, to
# the pre-branch fs::path(argv[k]) spelling: all nine kept the suite at
# 100% tests passed, 0 of 20, and all nine then died on a Cyrillic path
# with exit 0xC0000409, six of them printing the issue #87 terminate
# verbatim. The uncovered sites were src/main.cpp 1477, 1984, 1998, 2008,
# 2027, 2059 and 2083, and src/viewer_main.cpp 999 and 1000. Every one of
# them is a second, third or variadic path argument, or a positional the
# option sweep never touched.
#
# So the unit of coverage here is a path *position*, not a command. For each
# command the template below marks every position that becomes a path with
# PATHARG; the loop then runs the command once per marked position, with the
# Cyrillic path in that one and an ASCII placeholder in the others.
#
# Two guards keep this honest, and neither is an adversarial barrier:
#
#   the command set is read out of the program's own usage text, and a
#   command with no template here is a hard error rather than a command
#   quietly skipped. A command added by another branch fails this file and
#   says so;
#
#   the number of nfsu::argument_path call sites in each source file is
#   counted and required to equal the number this file was written against.
#   A new path argument anywhere - a new positional, a new repeatable
#   option, a path reached from something other than argv - changes that
#   count and fails here with a message saying to add a case. It cannot
#   tell whether the case you add actually reaches the new site; it can
#   only refuse to let the site arrive unnoticed.

if(NOT DEFINED MAIN_SOURCE OR NOT DEFINED VIEWER_SOURCE)
    message(FATAL_ERROR "MAIN_SOURCE and VIEWER_SOURCE must both be defined")
endif()

function(nfsu_count_sites path out_var)
    file(READ "${path}" text)
    string(REGEX MATCHALL "nfsu::argument_path\\(" hits "${text}")
    list(LENGTH hits count)
    set(${out_var} "${count}" PARENT_SCOPE)
endfunction()

nfsu_count_sites("${MAIN_SOURCE}" main_sites)
nfsu_count_sites("${VIEWER_SOURCE}" viewer_sites)
if(NOT main_sites EQUAL 22 OR NOT viewer_sites EQUAL 8)
    message(FATAL_ERROR
        "the conversion is called at ${main_sites} places in ${MAIN_SOURCE} "
        "and ${viewer_sites} in ${VIEWER_SOURCE}; this file was written "
        "against 22 and 8. A path argument was added or removed. Drive a "
        "non-ASCII path through the new position - a template entry below "
        "for a probe command, or a viewer case - and then update these two "
        "numbers. Reverting a single call site is how issue #87 comes back, "
        "and a position nothing drives is a site nothing would catch.")
endif()

# One entry per registered command: the command name, then its arguments
# with PATHARG wherever the argument becomes a path. The values are chosen
# to get past each command's own checks so that the deepest site is reached
# - "A" is a LOD, "240SX" a car type, "0" and "16" an offset and a size.
# Slashes separate the tokens, because a CMake list cannot nest.
set(templates
    "summary/PATHARG"
    "chunks/PATHARG/64"
    "meshes/PATHARG"
    "obj/PATHARG/PATHARG"
    "textures/PATHARG"
    "pack-set/PATHARG"
    "export-textures/PATHARG/PATHARG/tga"
    "texture-bindings/PATHARG/PATHARG"
    "car-parts/PATHARG/240SX"
    "stock-parts/PATHARG/240SX/PATHARG/PATHARG"
    "stock-scene/PATHARG/240SX/A/--place-wheels/PATHARG/PATHARG"
    "track-region/PATHARG/0/16/PATHARG/PATHARG"
    "track-objects/PATHARG/0/16"
    "car-placement/PATHARG/240SX"
    "part-markers/PATHARG/BODY"
    "body-skin-inputs/240SX/--source/car-textures/PATHARG"
)

# The command inventory, read out of the program rather than listed here, so
# that a command registered and not covered is a failure rather than a
# silence.
nfsu_run(usage usage_code "${PROBE}")
set(commands "")
string(REPLACE ";" "\\;" usage_escaped "${usage}")
string(REGEX REPLACE "\r?\n" ";" usage_lines "${usage_escaped}")
set(usage_line_count 0)
foreach(line IN LISTS usage_lines)
    if(line MATCHES "^  nfsu_probe ")
        math(EXPR usage_line_count "${usage_line_count}+1")
    endif()
    if(line MATCHES "^  nfsu_probe ([^ ]+)")
        if(NOT CMAKE_MATCH_1 IN_LIST commands)
            list(APPEND commands "${CMAKE_MATCH_1}")
        endif()
    endif()
endforeach()
list(LENGTH commands command_count)
# An equality against the text, not a floor against a number someone chose.
# A usage line this reader cannot take a name out of drops out of the
# inventory, and a floor with room in it would let that pass.
if(NOT command_count EQUAL usage_line_count)
    message(FATAL_ERROR
        "${usage_line_count} usage lines name a command and only "
        "${command_count} names came out of them, so at least one command is "
        "listed in a shape this reader cannot take a name from and would be "
        "covered by nothing. The usage text reads:\n${usage}")
endif()
if(command_count LESS 1)
    message(FATAL_ERROR
        "no commands were read out of the nfsu_probe usage text:\n${usage}")
endif()

set(covered_positions 0)
foreach(command IN LISTS commands)
    set(template "")
    foreach(candidate IN LISTS templates)
        if(candidate MATCHES "^${command}(/|$)")
            set(template "${candidate}")
        endif()
    endforeach()
    if(template STREQUAL "")
        message(FATAL_ERROR
            "nfsu_probe registers '${command}' and this file has no template "
            "for it, so no non-ASCII path would ever be driven through its "
            "path arguments. Add an entry to `templates` above with PATHARG "
            "at every position that becomes a path, and update the call-site "
            "counts at the top of this section.")
    endif()
    string(REPLACE "/" ";" tokens "${template}")
    list(LENGTH tokens token_count)
    # Which of this command's arguments are paths. Position 0 is the command
    # name itself and is never one.
    set(slots "")
    math(EXPR last "${token_count}-1")
    foreach(index RANGE ${last})
        list(GET tokens ${index} token)
        if(token STREQUAL "PATHARG")
            list(APPEND slots ${index})
        endif()
    endforeach()
    list(LENGTH slots slot_count)
    if(slot_count LESS 1)
        message(FATAL_ERROR
            "the template for '${command}' marks no path argument, so it "
            "would run and measure nothing")
    endif()
    foreach(target IN LISTS slots)
        set(arguments "")
        foreach(index RANGE ${last})
            list(GET tokens ${index} token)
            if(NOT token STREQUAL "PATHARG")
                list(APPEND arguments "${token}")
            elseif(index EQUAL target)
                list(APPEND arguments "${cyrillic_input}")
            else()
                list(APPEND arguments "${ascii_geometry}")
            endif()
        endforeach()
        nfsu_run(out code "${PROBE}" ${arguments})
        nfsu_forbid_crash(
            "nfsu_probe ${command}, Cyrillic path at argument ${target}"
            "${out}" "${code}")
        math(EXPR covered_positions "${covered_positions}+1")
    endforeach()
endforeach()
message(STATUS
    "every position: ${command_count} nfsu_probe commands, ${covered_positions} "
    "path positions, each driven with a Cyrillic path in turn")

# The viewer. Its three positional arguments, then each path-valued option,
# which is read out of the usage text the same way: every option line whose
# value is spelled F or FILE. argv[1] and argv[2] are what the review found
# uncovered; argv[3] is covered by the acceptance case above and is repeated
# here so that the three read as one set.
foreach(position 1 2 3)
    set(arguments "${ascii_geometry}" "${ascii_textures}" "${viewer_output}")
    math(EXPR index "${position}-1")
    list(REMOVE_AT arguments ${index})
    list(INSERT arguments ${index} "${cyrillic_input}")
    nfsu_run(out code "${VIEWER}" ${arguments})
    nfsu_forbid_crash("nfsu_viewer, Cyrillic path at argument ${position}"
                      "${out}" "${code}")
    file(REMOVE "${viewer_output}" "${viewer_output}.partial")
endforeach()

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
# Three positionals plus one per path-valued option is the whole of what
# nfsu_viewer converts, so this is the same equality the call-site count
# above applies to the file: an option whose usage placeholder stops being
# spelled F or FILE drops out of `options` and lands here rather than
# silently reducing what runs. What it cannot catch is a usage line added
# that looks like a path option and is not; the review demonstrated that,
# and it is an edit rather than drift.
math(EXPR expected_options "${viewer_sites}-3")
if(NOT option_count EQUAL expected_options)
    message(FATAL_ERROR
        "${option_count} path-valued options were read out of the nfsu_viewer "
        "usage text and the source converts ${viewer_sites} arguments, three "
        "of them positional, so ${expected_options} were expected. An option "
        "whose value is spelled something other than F or FILE is covered by "
        "nothing here. Spell it F or FILE, or teach this reader the new "
        "spelling. The usage text reads:\n${viewer_usage}")
endif()

foreach(option IN LISTS options)
    nfsu_run(out code "${VIEWER}" "${ascii_geometry}" "${ascii_textures}"
             "${viewer_output}" "${option}" "${cyrillic_input}")
    nfsu_forbid_crash("nfsu_viewer ${option} with a Cyrillic path" "${out}"
                      "${code}")
endforeach()
file(REMOVE "${viewer_output}" "${viewer_output}.partial")
message(STATUS
    "every position: nfsu_viewer, 3 positional arguments and ${option_count} "
    "path options, each driven with a Cyrillic path in turn")

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
