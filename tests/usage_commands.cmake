# nfsu_probe registers a command in exactly one place: the kCommands table in
# src/main.cpp. An entry holds the dispatch name, the rest of the usage line
# and the handler, and print_usage() composes the usage line out of the same
# `name` field main() dispatches on, so a registered command is listed under
# its own name whether or not its author wrote any usage text at all. It also
# terminates a `tail` that does not terminate itself, so that usage line
# starts a line of its own whatever the entry before it printed.
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
# The three sets prove that a listed name is dispatched. They cannot prove
# the converse, and that gap is the second half of this file. A dispatch
# written outside the table has no entry, so print_usage() never lists it and
# nobody had a reason to document it: it is missing from all three
# inventories at once, all three still agree at N, and an N+1th command
# works. Integrating PR #79 auto-merged that branch's pre-table
#
#     if (command == "track-objects") { ... }
#
# into main() after the table dispatch loop with no conflict reported. It
# compiled, it ran, and the three inventories agreed. Two more checks close
# it from opposite sides:
#
#   the shape of main()  argv[1] is read once, in main(), into one named
#                        variable; main() loops over kCommands; the only
#                        line in main() that selects on a value is that
#                        loop's comparison against an entry's own `name`;
#                        and no command name is spelled inside main() at
#                        all;
#   the name literals    every command-shaped string literal anywhere in
#                        src/main.cpp is either a registered command or is
#                        refused, by name, by the built program.
#
# The first is the cheap one and catches the shape above where it landed.
# The second does not care how the dispatch is written - a switch, a helper,
# a lambda, a map at namespace scope - only that the name it answers to is
# spelled somewhere in the file, and it asks the program itself rather than
# this file's idea of C++.
#
# Between them they catch every shape tried against them but one: a helper
# outside main() that compares the name against a string assembled at run
# time, called from main() by a line that compares nothing. main() then has
# no comparison to find and the name is spelled nowhere, so both checks pass
# on a binary that answers to it. That mutant was built and it dispatches.
# Closing it needs the dispatch itself to be derivable from the table, which
# is a change to src/main.cpp and not to this check.
#
# The candidate names for the second check are read from the source and not
# from the binary, which is the stronger place to read them but not one CMake
# can reach. file(STRINGS) drops a non-printable byte instead of ending the
# string at it, so every NUL-separated literal between two newlines arrives
# glued into one run: measured on this program's nfsu_probe.exe, 40 runs, the
# longest 2017982 characters. Recovering tokens out of those runs takes 23
# seconds, loses every boundary between two adjacent all-letter literals, and
# yields 3222 candidates, which is 3222 process launches against a 60 second
# test timeout. Reading the source costs nothing and keeps every boundary.
# Beside the assembled name above, what it gives up is a literal that lives
# in another translation unit; main() is here, so a dispatch there still has
# to be reached from a line in this file.
#
# A name that this file's pattern cannot represent is a hard error rather
# than something quietly trimmed to the part that does match, because a
# trimmed name compares equal to nothing and would otherwise be reported as
# an unrelated mismatch.
#
# print_usage() needs no assertion of its own. It prints "  nfsu_probe "
# followed by the name for every table entry and for nothing else, so a
# hand-written usage line - written into a `tail`, or into print_usage()
# itself - is read back by the `listed` loop above under whatever name it
# spells, and lands in `listed` without landing in `registered`. The set
# comparison then names it. The failure that needs a new assertion is the
# one that leaves every inventory unchanged, and a printed line always
# changes one.
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
#
# print_usage() opens a line with "  nfsu_probe " for every entry in the table
# and for nothing else, so every such line has to yield a name here. A line
# that opens one and then names nothing - an entry whose `name` is empty, or
# begins with a space - is a hard error rather than a line this loop passes
# over: passing over it would take the entry out of `listed` while leaving it
# dispatched, which is the one defect the three inventories exist to catch and
# the one they cannot catch by comparison, because an entry the table reader
# also cannot see is missing from both sides at once.
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
    elseif(line MATCHES "^  nfsu_probe")
        message(FATAL_ERROR
            "the usage text has a command line that names no command: "
            "'${line}'. An entry in the kCommands table has an empty name, or "
            "one that starts with a space, so nothing can list or document "
            "it under the name it is dispatched by.\n${usage}")
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

# ---------------------------------------------------------------------------
# the shape of main(): one read of argv[1], one comparison, over the table
# ---------------------------------------------------------------------------

# The line that binds argv[1] to a name. Pinned by shape and not by exact
# text: the variable may be renamed, and the capture is what names it in the
# comparison pattern below. A binding this pattern cannot represent is a hard
# error, because the alternative is to fall back to a comparison pattern with
# an empty variable name in it, which matches nothing and would report a
# correct main() as having no dispatch at all.
set(dispatch_binding
    "^ +const std::string_view ([A-Za-z_][A-Za-z0-9_]*) = argv\\[1\\];$")

# Reading one value and taking one of several branches from it. Anything
# spelled this way inside main() is a dispatch until it is shown to be the
# table's own.
set(selection_markers
    "==|!=|switch|case|compare|strcmp|strncmp|memcmp|strstr|starts_with|ends_with|\\.find|\\.rfind|\\.count|\\.contains")

set(argv1_reads 0)
set(dispatch_variable "")
set(in_main FALSE)
set(main_opened FALSE)
set(main_closed FALSE)
set(iterates_table FALSE)
set(table_comparisons 0)
set(literals "")
foreach(line IN LISTS source_lines)
    if(NOT main_opened AND line MATCHES "^int main\\(")
        set(in_main TRUE)
        set(main_opened TRUE)
    elseif(in_main AND line MATCHES "^\\};?$")
        set(in_main FALSE)
        set(main_closed TRUE)
    endif()

    # Command names are taken from the line as written. Stripping the comment
    # first would take a name inside a string literal that contains "//" out
    # of the inventory along with the comment that is not there.
    # MATCHALL hands its matches back as a list, so a literal that contains a
    # semicolon arrives split across several elements. Requiring both quotes
    # back drops those fragments: a literal with a semicolon in it is not a
    # command name under name_pattern either way, and a fragment carrying one
    # quote would otherwise be trimmed into a name nothing ever spelled.
    string(REGEX MATCHALL "\"[^\"]*\"" quoted "${line}")
    foreach(quoted_text IN LISTS quoted)
        if(NOT quoted_text MATCHES "^\"([^\"]*)\"$")
            continue()
        endif()
        set(literal "${CMAKE_MATCH_1}")
        if(literal MATCHES "${name_pattern}")
            if(in_main)
                message(FATAL_ERROR
                    "main() in ${SOURCE} names the command '${literal}' "
                    "directly:\n  ${line}\nmain() compares the command "
                    "against the kCommands table and against nothing else, "
                    "so a command name spelled inside it is a second "
                    "dispatch path. Register '${literal}' in the table "
                    "instead.")
            endif()
            if(NOT literal IN_LIST literals)
                list(APPEND literals "${literal}")
            endif()
        endif()
    endforeach()

    # Everything below reads the line as code: its line comment removed, so
    # that prose about argv[1] or about a switch statement is prose, and its
    # string literals removed, so that a message that happens to contain "=="
    # is a message. Neither removal can hide a dispatch. A comparison operator
    # is a token and is never inside the literal it compares against, and the
    # name inventory above already read this line whole, so the name a
    # dispatch answers to is collected before either removal happens. A "//"
    # inside a string literal truncates the line here, and that is the same
    # trade: the name survives in the inventory.
    string(REGEX REPLACE "//.*$" "" statement "${line}")
    string(REGEX REPLACE "\"[^\"]*\"" "" statement "${statement}")

    if(statement MATCHES "argv\\[1\\]")
        math(EXPR argv1_reads "${argv1_reads}+1")
        if(NOT in_main)
            message(FATAL_ERROR
                "${SOURCE} reads argv[1] outside main(), or before the line "
                "this check reads as main()'s opening:\n  ${line}\nThe "
                "command name is read once, in main(), and compared against "
                "the kCommands table there. A second reader of argv[1] is a "
                "second dispatch path, and appears in none of the three "
                "inventories above. If main() is still where it was, check "
                "that its definition opens a line with 'int main('.")
        endif()
        if(statement MATCHES "${dispatch_binding}")
            set(dispatch_variable "${CMAKE_MATCH_1}")
        else()
            message(FATAL_ERROR
                "main() in ${SOURCE} reads argv[1] in a form this check "
                "cannot represent:\n  ${line}\nIt must be bound by a line of "
                "the form\n"
                "      const std::string_view <name> = argv[1];\n"
                "so that the one comparison main() is allowed to make can be "
                "recognized by the name it compares.")
        endif()
    endif()

    if(in_main AND statement MATCHES "^ *for \\(.*kCommands")
        set(iterates_table TRUE)
    endif()

    if(in_main AND statement MATCHES "${selection_markers}")
        # STREQUAL "", not if(NOT dispatch_variable): a bare if() on a
        # variable's value reads "n", "no", "off" and "0" as false, and all
        # four are names a C++ variable can have.
        if(dispatch_variable STREQUAL "")
            message(FATAL_ERROR
                "main() in ${SOURCE} selects on a value before it binds "
                "argv[1]:\n  ${line}\nThe binding comes first, so that every "
                "later comparison can be read against the name it binds.")
        endif()
        if(statement MATCHES
           "^ +if \\(${dispatch_variable} == [A-Za-z_][A-Za-z0-9_]*(\\[[A-Za-z0-9_]+\\])?\\.name\\) \\{$")
            math(EXPR table_comparisons "${table_comparisons}+1")
        else()
            message(FATAL_ERROR
                "main() in ${SOURCE} has a second dispatch path:\n  ${line}\n"
                "The only line in main() that may select on a value is the "
                "kCommands loop's comparison, spelled\n"
                "      if (${dispatch_variable} == <entry>.name) {\n"
                "or with the entry subscripted out of the table. "
                "A command dispatched from any other branch is registered "
                "nowhere, listed nowhere and documented nowhere, so the three "
                "inventories above agree while it works.")
        endif()
    endif()
endforeach()

if(NOT main_opened)
    message(FATAL_ERROR
        "no 'int main(' was found in ${SOURCE}, so the dispatch could not be "
        "read")
endif()
if(NOT main_closed)
    message(FATAL_ERROR
        "main() in ${SOURCE} was not closed by a '}' line, so the dispatch "
        "could not be read")
endif()
if(NOT argv1_reads EQUAL 1)
    message(FATAL_ERROR
        "${SOURCE} reads argv[1] ${argv1_reads} times, expected exactly once. "
        "The command name is read once, in main(), and compared against the "
        "kCommands table there.")
endif()
if(NOT iterates_table)
    message(FATAL_ERROR
        "main() in ${SOURCE} does not loop over kCommands, so the table is "
        "not what it dispatches on")
endif()
if(NOT table_comparisons EQUAL 1)
    message(FATAL_ERROR
        "main() in ${SOURCE} compares '${dispatch_variable}' against an "
        "entry's name ${table_comparisons} times, expected exactly once")
endif()

# ---------------------------------------------------------------------------
# and no command-shaped name in the source dispatches unless it is registered
# ---------------------------------------------------------------------------

# The reader above has to be able to see the table's own names. If it could
# not - a table written in some other form, a quoting shape this file does
# not match - it would produce an empty candidate list, and the loop below
# would pass without asking the program a single question.
foreach(name IN LISTS registered)
    if(NOT name IN_LIST literals)
        message(FATAL_ERROR
            "the registered command '${name}' was not read back out of "
            "${SOURCE} as a string literal, so the literal reader is not "
            "seeing what the table reader sees and the dispatch check below "
            "would run on an incomplete list of names")
    endif()
endforeach()

set(undeclared "")
set(undeclared_count 0)
foreach(name IN LISTS literals)
    if(NOT name IN_LIST registered)
        run_probe(out code "${name}" "${missing_input}")
        if(NOT out MATCHES "unknown command '${name}'")
            list(APPEND undeclared "${name}")
            math(EXPR undeclared_count "${undeclared_count}+1")
        endif()
    endif()
endforeach()
# The count, not if(undeclared). name_pattern admits "n", "no", "off", "0"
# and "ignore", and a one-element list holding any of them is a value if()
# reads as false, so a single stray dispatch under one of those names would
# be reported as nothing to report. That is measured, not theoretical:
# spelled if(undeclared), this loop passed a binary that answers to "off",
# and the summary line below then reported 36 names refused as unknown when
# 35 were refused and one had answered. Do not shorten it back.
if(undeclared_count GREATER 0)
    message(FATAL_ERROR
        "nfsu_probe dispatches ${undeclared}, which the kCommands table does "
        "not register. A command dispatched outside the table is listed in no "
        "usage text and documented in no README line, so the three "
        "inventories above agree while it works. Register it in the table, or "
        "remove the branch that dispatches it.")
endif()

list(LENGTH literals literal_count)
list(LENGTH registered registered_count)
math(EXPR probed_count "${literal_count}-${registered_count}")

list(LENGTH listed listed_count)
message(STATUS
    "nfsu_probe: ${listed_count} commands registered, listed, documented and "
    "dispatched: ${listed}")
message(STATUS
    "nfsu_probe: one read of argv[1] and one comparison in main(), and "
    "${probed_count} further command-shaped names in ${SOURCE} refused as "
    "unknown")
