# nfsu_probe registers a command in exactly one place: the kCommands table in
# src/main.cpp. An entry holds the dispatch name, the rest of the usage line
# and the handler, and print_usage() composes the usage line out of the same
# `name` field main() dispatches on, so a registered command is listed under
# its own name whether or not its author wrote any usage text at all.
#
# That construction is what this check measures, rather than trusting it. The
# check takes three inventories from three places that a half-finished
# command would leave disagreeing:
#
#   registered  the names in the kCommands table, read out of src/main.cpp
#   listed      the names in the usage text, read out of the built program
#   documented  the names in README.md, read out of the "build/nfsu_probe"
#               lines under "## Run"
#
# and requires all three to be the same set, in both directions: no inventory
# may be a subset of another. A command added to the table and nowhere else
# fails here, and the failure names it. There is deliberately no hand-written
# list of expected commands in this file, because a hand-written list only
# constrains the commands already on it: a command absent from every list is
# what "dispatched but not listed" looks like, and that is the defect this
# check exists for.
#
# Two further checks give the sets their meaning:
#
#   * every listed name is dispatched, not merely printed. A command that is
#     listed but not dispatched falls through to the same usage text a
#     command that refuses its arguments prints, so the two are
#     indistinguishable until the unknown case says which one it is;
#   * a name that is not registered is refused, by name, as unknown. That is
#     what makes the check above able to tell those two apart.
#
# A name that this file's pattern cannot represent is a hard error rather
# than something quietly trimmed to the part that does match, because a
# trimmed name compares equal to nothing and would otherwise be reported as
# an unrelated mismatch.
#
# Run as:
#   cmake -DPROBE=<nfsu_probe> -DSOURCE=<src/main.cpp> -DREADME=<README.md>
#         -P usage_commands.cmake

cmake_minimum_required(VERSION 3.20)

foreach(required PROBE SOURCE README)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR
            "${required} is not set: pass -D${required}=<path>")
    endif()
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR
            "${required} does not name an existing file: ${${required}}")
    endif()
endforeach()

# The spelling a command name is allowed to have. Both the table and the
# usage text are held to it, so a name outside it cannot reach the set
# comparison in a form that no longer matches the name it was written as.
set(name_pattern "^[a-z0-9][a-z0-9-]*$")

# Split text into a list of lines. Semicolons are escaped first: a line that
# contains one would otherwise split into two list elements.
function(split_lines text out_var)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(REPLACE ";" "\;" text "${text}")
    string(REPLACE "\n" ";" text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Fail unless two inventories hold the same names, and say which side each
# missing name is on.
function(require_same_set left_label left_in right_label right_in)
    set(left ${left_in})
    set(right ${right_in})
    list(SORT left)
    list(SORT right)
    if("${left}" STREQUAL "${right}")
        return()
    endif()
    set(only_left ${left})
    if(right)
        list(REMOVE_ITEM only_left ${right})
    endif()
    set(only_right ${right})
    if(left)
        list(REMOVE_ITEM only_right ${left})
    endif()
    message(FATAL_ERROR
        "${left_label} and ${right_label} do not hold the same commands.\n"
        "  in ${left_label} but not in ${right_label}: ${only_left}\n"
        "  in ${right_label} but not in ${left_label}: ${only_right}\n"
        "  ${left_label}: ${left}\n"
        "  ${right_label}: ${right}")
endfunction()

# ---------------------------------------------------------------------------
# registered: the kCommands table, read out of the source
# ---------------------------------------------------------------------------

file(READ "${SOURCE}" source_text)
split_lines("${source_text}" source_lines)

set(in_table FALSE)
set(table_opened FALSE)
set(table_closed FALSE)
set(registered "")
foreach(line IN LISTS source_lines)
    if(line MATCHES "^const Command kCommands")
        set(in_table TRUE)
        set(table_opened TRUE)
    elseif(in_table AND line MATCHES "^\\};")
        set(in_table FALSE)
        set(table_closed TRUE)
    elseif(in_table AND line MATCHES "^ +\\{\"([^\"]*)\"")
        set(name "${CMAKE_MATCH_1}")
        if(NOT name MATCHES "${name_pattern}")
            message(FATAL_ERROR
                "the kCommands table in ${SOURCE} registers '${name}', which "
                "is not a command name this check can represent. Command "
                "names must match ${name_pattern}.")
        endif()
        if(name IN_LIST registered)
            message(FATAL_ERROR
                "the kCommands table in ${SOURCE} registers '${name}' more "
                "than once; only the first entry is ever dispatched")
        endif()
        list(APPEND registered "${name}")
    endif()
endforeach()

if(NOT table_opened)
    message(FATAL_ERROR
        "no 'const Command kCommands' table was found in ${SOURCE}, so the "
        "registered commands could not be read")
endif()
if(NOT table_closed)
    message(FATAL_ERROR
        "the 'const Command kCommands' table in ${SOURCE} was not closed by a "
        "'};' line, so the registered commands could not be read")
endif()
if(NOT registered)
    message(FATAL_ERROR
        "the 'const Command kCommands' table in ${SOURCE} was found but no "
        "entry in it was recognized, so the registered commands could not be "
        "read")
endif()

# ---------------------------------------------------------------------------
# listed: the usage text, read out of the built program
# ---------------------------------------------------------------------------

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

split_lines("${usage}" usage_lines)

# The whole first word after the prefix is captured, not just the part of it
# that looks like a name, so a name outside name_pattern is reported instead
# of being silently trimmed to a prefix of itself.
set(listed "")
foreach(line IN LISTS usage_lines)
    if(line MATCHES "^  nfsu_probe ([^ ]+)")
        set(name "${CMAKE_MATCH_1}")
        if(NOT name MATCHES "${name_pattern}")
            message(FATAL_ERROR
                "the usage text lists '${name}', which is not a command name "
                "this check can represent. Command names must match "
                "${name_pattern}.\n${usage}")
        endif()
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

# ---------------------------------------------------------------------------
# documented: the README, read out of its runnable command lines
# ---------------------------------------------------------------------------

file(READ "${README}" readme_text)
split_lines("${readme_text}" readme_lines)

# The README shows one or more example invocations per command, all spelled
# "build/nfsu_probe <name> ...", so more than one line can name the same
# command. Prose mentions are written "nfsu_probe <name>" inside backticks
# and are not runnable lines, so they are not counted here.
set(documented "")
foreach(line IN LISTS readme_lines)
    if(line MATCHES "^build/nfsu_probe ([^ ]+)")
        set(name "${CMAKE_MATCH_1}")
        if(NOT name MATCHES "${name_pattern}")
            message(FATAL_ERROR
                "${README} documents 'build/nfsu_probe ${name}', which is not "
                "a command name this check can represent. Command names must "
                "match ${name_pattern}.")
        endif()
        if(NOT name IN_LIST documented)
            list(APPEND documented "${name}")
        endif()
    endif()
endforeach()

if(NOT documented)
    message(FATAL_ERROR
        "no 'build/nfsu_probe <command>' line was found in ${README}, so the "
        "documented commands could not be read")
endif()

# ---------------------------------------------------------------------------
# the three inventories must be one inventory
# ---------------------------------------------------------------------------

require_same_set(
    "the kCommands table" "${registered}"
    "the usage text" "${listed}")
require_same_set(
    "the kCommands table" "${registered}"
    "the README run lines" "${documented}")

# ---------------------------------------------------------------------------
# and the names in it must be the names the program answers to
# ---------------------------------------------------------------------------

foreach(name IN LISTS listed)
    run_probe(out code "${name}" "${missing_input}")
    if(out MATCHES "unknown command")
        message(FATAL_ERROR
            "the usage text lists '${name}' but nfsu_probe does not dispatch it:\n${out}")
    endif()
endforeach()

set(unregistered "nfsu-probe-no-such-command")
if(unregistered IN_LIST registered)
    message(FATAL_ERROR
        "'${unregistered}' is a registered command, so it cannot be used to "
        "check how an unregistered name is refused")
endif()
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
message(STATUS
    "nfsu_probe: ${listed_count} commands registered, listed, documented and "
    "dispatched: ${listed}")
