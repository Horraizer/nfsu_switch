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
# compiled, it ran, and the three inventories agreed.
#
# WHAT THE TWO CHECKS BELOW ARE FOR
#
# That defect was an accident. Git glued a handler into main() and nobody was
# prompted to look, which is what makes an auto-merge the dangerous route.
# The checks below are built for that: a handler that lands in main() carries
# a comparison into main(), and a comparison in main() is what they refuse.
#
#   the shape of main()  argv[1] is read in main() and bound by a line this
#                        file can name the variable from; main() loops over
#                        kCommands; and the only line in main() that selects
#                        on a command name is that loop's comparison against
#                        an entry's own `name`;
#   the name literals    every command-shaped string literal in src/main.cpp
#                        is either a registered command or is refused, by
#                        name, by the built program under each of several
#                        argument counts.
#
# WHAT THEY DO NOT CATCH
#
# They are not an adversarial barrier, and nothing here should be read as
# claiming they are. An earlier draft of this comment said they caught every
# shape tried against them but one. Two reviews then got nine shapes past
# them, and the first needed no obfuscation at all. Seven of those nine are
# closed now. These are not, and each one was built and runs:
#
#   * a helper in another translation unit. The literal is plain and the
#     reaching line in main() compares nothing:
#         const int extra =
#             dispatch_extra(command, argc, argv, &run_track_objects_cli);
#     This file reads src/main.cpp and nothing else, so the comparison is
#     invisible to it and the call is just a call. Measured: the command
#     dispatches while this check exits 0 and reports 15 commands
#     registered, listed, documented and dispatched. Splitting a helper into
#     its own .cpp is ordinary refactoring, which makes this the most
#     reachable of the four.
#   * a dispatch gated on something the probe below cannot produce. The
#     probe offers a name with several counts of a path that does not exist;
#     a handler guarded by `fs::exists(argv[2])`, or by an environment
#     variable, answers every question it knows how to ask.
#   * a name spelled by adjacent literal concatenation, `"dump" "-state"`.
#     The two literals are collected separately, both are refused, and the
#     compiler still sees one name.
#   * a name assembled at run time, compared in a helper outside main().
#
# The marker list this file uses is an enumeration of the ways someone
# thought of. A map lookup through .at() and a three-way comparison were
# both missing from the first version and both carried a dispatch through
# main() untouched. Read a shape that is not on that list as uncovered
# rather than as impossible.
#
# So: this catches the accident it was built for, and a fair number of
# deliberate shapes besides. It does not establish that the table is the
# only dispatch path, and a reviewer should not treat a green run as though
# it had.
#
# WHY THE CANDIDATE NAMES COME FROM THE SOURCE AND NOT THE BINARY
#
# Reading them out of the built program is the stronger idea - it is how the
# #79 review closed this by hand - but it is not one CMake can afford.
# file(STRINGS) drops a non-printable byte instead of ending the string at
# it, so NUL-separated literals arrive glued into long runs and the token
# boundaries have to be recovered by regex. On one build of this program
# that yielded 3222 candidate names and took 23 seconds before a single
# question was asked. The launches are the decisive part: this check
# measured 210 launches inside a 1.1 second run here, and an independent
# measurement on another machine took 2.6 seconds for 200, which puts 3222
# launches between 17 and 42 seconds on top of the 23. The test's budget is
# the TIMEOUT 60 in CMakeLists.txt, so the faster machine has little margin
# and the slower one has none.
#
# Numbers that describe the binary's layout - how many runs file(STRINGS)
# returns, how long the longest one is - are deliberately not quoted here.
# An earlier draft did quote them, and they did not reproduce: the same
# measurement gives 40 runs in one configuration and 109 or 182 in others,
# because the layout is a property of the build and not of this argument.
# The launch count is what the argument rests on.
#
# Restricting the scan to hyphenated tokens brings the count down to about a
# hundred, and gives up every command whose name has no hyphen, of which
# this program has five.
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
    # A backslash at the end of a line splices it onto the next one before
    # the compiler sees either, so this joins them the same way and for the
    # same reason. It also has to happen here rather than in a reader: the
    # separator written below is a semicolon, a trailing backslash escapes
    # it, and the two lines then arrive as one element with a "\;" in the
    # middle. Doubling the backslash first does not help, because the list
    # parser resolves the pair back into an escape for the separator. A
    # legal C++ line splice above any line this file pins would otherwise
    # read as that line missing entirely.
    string(REPLACE "\\\n" "" text "${text}")
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
# the shape of main(): one binding, one comparison, over the table
# ---------------------------------------------------------------------------

# A C++ string literal, escapes included, so that a \" inside one does not
# end it and leave the rest of the line paired the wrong way round.
set(string_literal "\"(\\\\.|[^\"\\\\])*\"")

# A character literal. These are removed before the string literals are, so
# that '\"' cannot pair with a later quote and delete the code between them.
set(char_literal "'(\\\\.|[^'])'")

# The line that binds argv[1] to a name. Pinned by shape and not by exact
# text: the variable may be renamed, and the capture is what names it in the
# comparison patterns below. Leading tabs and a trailing comment are fine;
# the comment is gone and the space it left is trimmed before this is tried.
set(dispatch_binding
    "^[ \t]+const[ \t]+std::string_view[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*argv\\[1\\];$")

# Reading one value and taking one of several branches from it. Anything
# spelled this way inside main() is a dispatch until it is shown to be the
# table's own, or to be about something no command could be named.
#
# This is an enumeration, which means it is a list of the ways someone
# thought of. A map lookup through .at() and a three-way comparison were
# both missing from the first version of it and both got a dispatch through
# main() untouched. Treat a shape that is not here as uncovered rather than
# as impossible.
set(selection_markers
    "==|!=|<=>|switch|case|compare|strcmp|strncmp|strcasecmp|_stricmp|memcmp|strstr|starts_with|ends_with|regex|std::equal|\\.find|\\.rfind|\\.count|\\.contains|\\.at\\(|\\.lower_bound|\\.upper_bound|\\.equal_range")

set(bindings 0)
set(dispatch_variable "")
set(in_main FALSE)
set(main_opened FALSE)
set(main_closed FALSE)
set(in_block_comment FALSE)
set(iterates_table FALSE)
set(table_comparisons 0)
set(literals "")
foreach(raw_line IN LISTS source_lines)
    # A raw string literal is not something this reader can represent: its
    # delimiter is arbitrary, a quote inside it is ordinary text, and a "}"
    # inside one at column zero would end main() as far as everything below
    # is concerned and leave the rest of the function unread. Refusing it is
    # the same rule this file already applies to a command name it cannot
    # represent, and it is a refusal rather than a silence.
    if(raw_line MATCHES "R\"")
        message(FATAL_ERROR
            "${SOURCE} contains a raw string literal:\n  ${raw_line}\n"
            "This check reads the file as lines of ordinary C++ and cannot "
            "represent one. A name inside a raw string is invisible to the "
            "command inventory below, and a '}' inside one at column zero "
            "would end main() for this reader while leaving the rest of it "
            "unchecked. Rewrite it as an ordinary string literal, or teach "
            "this reader raw strings and say so here.")
    endif()

    # The candidate names are taken from the line as written, comments
    # included. A name in a comment costs one refused probe and nothing
    # else, while a name this loop does not collect is a name the program is
    # never asked about, so the inclusive reading is the safe one.
    string(REGEX MATCHALL "${string_literal}" quoted "${raw_line}")
    foreach(quoted_text IN LISTS quoted)
        if(quoted_text MATCHES "^\"(.*)\"$")
            set(literal "${CMAKE_MATCH_1}")
            if(literal MATCHES "${name_pattern}" AND NOT literal IN_LIST literals)
                list(APPEND literals "${literal}")
            endif()
        endif()
    endforeach()

    # Everything below reads the same line as code, in this order:
    # character literals, then string literals, then comments. Each step
    # removes something that could otherwise be mistaken for the next one's
    # opening delimiter - a '"' for a string, a "//" or "/*" inside a string
    # for a comment - and taking them in any other order is how a comparison
    # gets deleted out of the middle of a line while looking stripped.
    string(REGEX REPLACE "${char_literal}" "" code "${raw_line}")

    string(REGEX MATCHALL "${string_literal}" code_quoted "${code}")
    set(code_strings 0)
    set(code_names 0)
    foreach(quoted_text IN LISTS code_quoted)
        if(quoted_text MATCHES "^\"(.*)\"$")
            math(EXPR code_strings "${code_strings}+1")
            if(CMAKE_MATCH_1 MATCHES "${name_pattern}")
                math(EXPR code_names "${code_names}+1")
            endif()
        endif()
    endforeach()
    string(REGEX REPLACE "${string_literal}" "" code "${code}")

    # Comments last, and block comments may span lines. Whichever of "//"
    # and "/*" comes first on the line wins, so that a "/*" written inside a
    # line comment does not open a block comment that swallows the rest of
    # the file.
    set(rest "${code}")
    set(code "")
    while(TRUE)
        string(LENGTH "${rest}" rest_length)
        if(rest_length EQUAL 0)
            break()
        endif()
        if(in_block_comment)
            string(FIND "${rest}" "*/" at)
            if(at LESS 0)
                break()
            endif()
            math(EXPR at "${at}+2")
            set(in_block_comment FALSE)
        else()
            string(FIND "${rest}" "/*" at)
            string(FIND "${rest}" "//" line_at)
            if(line_at GREATER_EQUAL 0 AND (at LESS 0 OR line_at LESS at))
                string(SUBSTRING "${rest}" 0 ${line_at} head)
                string(APPEND code "${head}")
                break()
            endif()
            if(at LESS 0)
                string(APPEND code "${rest}")
                break()
            endif()
            string(SUBSTRING "${rest}" 0 ${at} head)
            string(APPEND code "${head}")
            math(EXPR at "${at}+2")
            set(in_block_comment TRUE)
        endif()
        if(at GREATER_EQUAL rest_length)
            break()
        endif()
        string(SUBSTRING "${rest}" ${at} -1 rest)
    endwhile()

    # The space a removed comment leaves behind. Trimming it is what lets
    # every pattern below end at "$" while still accepting a line that
    # carries a comment, and the binding and the comparison are the two most
    # comment-worthy lines in main().
    string(REGEX REPLACE "[ \t]+$" "" code "${code}")

    if(NOT main_opened AND code MATCHES "^int main[ \t]*\\(")
        set(in_main TRUE)
        set(main_opened TRUE)
    elseif(in_main AND code MATCHES "^\\};?$")
        set(in_main FALSE)
        set(main_closed TRUE)
    endif()

    set(statement "${code}")

    if(statement MATCHES "argv\\[1\\]")
        if(NOT in_main)
            message(FATAL_ERROR
                "${SOURCE} reads argv[1] outside main(), or before the line "
                "this check reads as main()'s opening:\n  ${raw_line}\nThe "
                "command name is read in main() and compared against the "
                "kCommands table there. A reader of argv[1] anywhere else is "
                "a second dispatch path, and appears in none of the three "
                "inventories above. If main() is still where it was, check "
                "that its definition opens a line with 'int main('.")
        endif()
        if(statement MATCHES "${dispatch_binding}")
            set(dispatch_variable "${CMAKE_MATCH_1}")
            math(EXPR bindings "${bindings}+1")
        endif()
    endif()

    if(in_main AND statement MATCHES "^[ \t]*for[ \t]*\\(.*kCommands")
        set(iterates_table TRUE)
    endif()

    if(in_main AND statement MATCHES "${selection_markers}")
        if(dispatch_variable STREQUAL "")
            message(FATAL_ERROR
                "main() in ${SOURCE} selects on a value before it binds "
                "argv[1]:\n  ${raw_line}\nThe binding comes first, so that "
                "every later comparison can be read against the name it "
                "binds.")
        endif()
        set(entry_name "[A-Za-z_][A-Za-z0-9_]*(\\[[A-Za-z0-9_]+\\])?\\.name")
        if(statement MATCHES
           "^[ \t]+if[ \t]*\\([ \t]*${dispatch_variable}[ \t]*==[ \t]*${entry_name}[ \t]*\\)[ \t]*\\{$"
           OR statement MATCHES
           "^[ \t]+if[ \t]*\\([ \t]*${entry_name}[ \t]*==[ \t]*${dispatch_variable}[ \t]*\\)[ \t]*\\{$")
            math(EXPR table_comparisons "${table_comparisons}+1")
        elseif(code_strings GREATER 0 AND code_names EQUAL 0)
            # Every string this line compares against is one no table entry
            # could be registered under, so it is an option and not a
            # command. "--help" is the case this exists for: name_pattern
            # forbids a leading hyphen, so it cannot be a table entry, and a
            # rule with no legal way to satisfy it is a rule the next person
            # who needs one deletes.
        else()
            message(FATAL_ERROR
                "main() in ${SOURCE} has a second dispatch path:\n"
                "  ${raw_line}\n"
                "The only line in main() that may select on a command name "
                "is the kCommands loop's comparison, spelled\n"
                "      if (${dispatch_variable} == <entry>.name) {\n"
                "in either operand order, with the entry subscripted out of "
                "the table if you prefer. A line that compares only against "
                "strings no command could be named - an option such as "
                "\"--help\" - is allowed. A command dispatched from any "
                "other branch is registered nowhere, listed nowhere and "
                "documented nowhere, so the three inventories above agree "
                "while it works.")
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
if(bindings EQUAL 0)
    message(FATAL_ERROR
        "main() in ${SOURCE} never binds argv[1] by a line of the form\n"
        "      const std::string_view <name> = argv[1];\n"
        "so the name the one allowed comparison compares could not be read. "
        "argv[1] may be read again for a message; it is the binding that has "
        "to be recognizable.")
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

# Each candidate is offered to the program under several argument counts.
# One is not enough, and this is the thing to understand about this check if
# you read nothing else in it: a handler guarded by its own argument count
# falls through to the unknown-command branch when it is asked with too few
# arguments, and a single question records that fall-through as a refusal.
# Thirteen of the sixteen registered commands take two inputs or more, so
# "if (argc >= 4 && name == ...)" is the ordinary shape a command has in
# this program rather than a suspicious one.
#
# Widening the question raises the count a dispatch has to dodge. It does
# not close the hole, and nothing that probes can: a handler gated on an
# input that exists, or on an environment variable, answers every question
# this loop knows how to ask. The header comment says so plainly rather than
# leaving the number of shapes to imply a proof.
set(probe_widths 1 2 3 4 5 6)

set(undeclared "")
set(undeclared_count 0)
set(probed_count 0)
set(probe_calls 0)
foreach(name IN LISTS literals)
    if(NOT name IN_LIST registered)
        math(EXPR probed_count "${probed_count}+1")
        set(arguments "")
        foreach(width IN LISTS probe_widths)
            list(APPEND arguments "${missing_input}")
            run_probe(out code "${name}" ${arguments})
            math(EXPR probe_calls "${probe_calls}+1")
            if(NOT out MATCHES "unknown command '${name}'")
                if(NOT name IN_LIST undeclared)
                    list(APPEND undeclared "${name}")
                    math(EXPR undeclared_count "${undeclared_count}+1")
                endif()
            endif()
        endforeach()
    endif()
endforeach()

# The one thing that would make the loop above prove nothing is an empty
# candidate list, which is what a broken literal reader produces. This is
# reached by breaking that reader and by nothing an author can write in
# src/main.cpp, so it guards this file against itself rather than the
# program against a mistake. It replaces a guard that required every
# registered name to appear in the candidate list, which could not fire at
# all: the table reader's own anchor guarantees the name is the first
# quoted run on its line, so the literal reader sees every name the table
# reader sees, and three earlier failures cover every way that could stop
# being true.
if(probed_count EQUAL 0)
    message(FATAL_ERROR
        "no command-shaped name outside the kCommands table was read out of "
        "${SOURCE}, so the dispatch check below asked nfsu_probe nothing. "
        "The literal reader above is not seeing what the table reader sees.")
endif()

# The count, not if(undeclared). name_pattern admits "n", "no", "off", "0"
# and "ignore", and a one-element list holding any of them is a value if()
# reads as false. That is measured, not theoretical: spelled if(undeclared),
# this loop passed a binary that answers to "off", and the summary line below
# then reported one more name refused as unknown than had been refused. Do
# not shorten it back.
if(undeclared_count GREATER 0)
    message(FATAL_ERROR
        "nfsu_probe dispatches ${undeclared}, which the kCommands table does "
        "not register. A command dispatched outside the table is listed in no "
        "usage text and documented in no README line, so the three "
        "inventories above agree while it works. Register it in the table, or "
        "remove the branch that dispatches it.")
endif()





list(LENGTH listed listed_count)
message(STATUS
    "nfsu_probe: ${listed_count} commands registered, listed, documented and "
    "dispatched: ${listed}")
message(STATUS
    "nfsu_probe: one binding and one table comparison in main(), and "
    "${probed_count} further command-shaped names in ${SOURCE} refused as "
    "unknown under ${probe_calls} launches")
