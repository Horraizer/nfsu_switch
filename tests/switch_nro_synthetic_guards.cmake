# tests/switch_nro_check.cmake against a synthetic NRO and ELF, and against
# every header corruption tests/switch_nro_synthetic_tests.cpp knows how to
# write. There is no console or emulator in this build, so this is what runs
# without a real build-switch output (review comment 2856, F3 and N3).
#
# WHY A SCRIPT RATHER THAN A COMPILED TEST LAUNCHING A CHILD PROCESS. This
# file used to be entirely inside tests/switch_nro_synthetic_tests.cpp,
# which ran the checker itself with the Win32 process-launch API under
# _WIN32 and the POSIX pair of process-launch calls under #else - and the
# #else half had never been built anywhere this branch was developed, which
# is exactly the failure class issue #132 is about: code for a platform
# nobody was building broke the first Linux build the moment it was (review
# comment 2942, R2). CMake already has a documented, already-used way to run
# one program and check what it printed and exited with (execute_process,
# as tests/track_render_guards.cmake and tests/shader_source_guards.cmake
# both do), so the child-process work moves here and the compiled test goes
# back to being ordinary, portable C++ that only writes files.
#
# WRITER is the built nfsu_switch_nro_synthetic_tests binary. This script
# runs it once, in --write-fixtures mode, to populate a fresh directory with
# every fixture, then runs CHECKER (tests/switch_nro_check.cmake) against
# each one and asserts both its exit status and the exact line it prints -
# CLAUDE.md (Required validation) is explicit that a non-zero exit with no
# matching failure line is UNKNOWN, not caught, so every case below checks
# both. WRITER's own file names are the contract between it and this script;
# tests/switch_nro_synthetic_tests.cpp documents the patch each one makes
# and has to be kept in step with the expectations named here by hand.

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS WRITER CHECKER)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

# switch/CMakeLists.txt's nx_generate_nacp title, baked into every fixture
# WRITER writes; switch_nro_check.cmake is told to expect it back.
set(title "nfsu_switch")

# A directory no other run shares: several sessions run ctest at once here
# (CLAUDE.md, Shared checkout), each with its own build directory, but two
# ctest invocations from the very same build directory are not impossible,
# and this is the same production code area issue #122 was raised against.
string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" token)
set(work "${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_nro_synthetic_guards_${token}")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

execute_process(
    COMMAND "${WRITER}" --write-fixtures "${work}"
    RESULT_VARIABLE write_status
    OUTPUT_VARIABLE write_out
    ERROR_VARIABLE write_err)
if(NOT write_status EQUAL 0)
    message(FATAL_ERROR "WRITER could not write the fixtures into ${work} (exit ${write_status}):\n${write_out}${write_err}")
endif()

set(elf "${work}/control.elf")
if(NOT EXISTS "${elf}")
    message(FATAL_ERROR "WRITER did not write ${elf}")
endif()

# WRITER must also refuse to overwrite a fixture that already exists
# (review comment 2942, R2), which is exercised here rather than inside
# WRITER's own no-argument self-check, since only this script knows the
# directory a run of it already populated.
execute_process(
    COMMAND "${WRITER}" --write-fixtures "${work}"
    RESULT_VARIABLE rewrite_status
    OUTPUT_VARIABLE rewrite_out
    ERROR_VARIABLE rewrite_err)
set(failures 0)
function(nfsu_fail message)
    message(SEND_ERROR "${message}")
    math(EXPR next "${failures}+1")
    set(failures "${next}" PARENT_SCOPE)
endfunction()
if(rewrite_status EQUAL 0)
    nfsu_fail("a second --write-fixtures into the same directory must refuse, not succeed")
elseif(NOT "${rewrite_out}${rewrite_err}" MATCHES "refusing to overwrite")
    nfsu_fail("a second --write-fixtures did not print why it refused:\n${rewrite_out}${rewrite_err}")
endif()

set(caught 0)
set(passed 0)

# Runs CHECKER over ${work}/<nro_name> and ${elf}, and returns its exit
# status and combined output. Every case below shares the one valid ELF
# WRITER wrote, so a case's outcome is about the NRO corruption alone.
#
# CMake's own message() wraps a long line at a column width and indents the
# continuation, so a several-word needle a case looks for can straddle a
# wrap point that has nothing to do with the checker's own logic (the same
# hazard tests/switch_nro_synthetic_tests.cpp's normalize_whitespace() used
# to guard against, back when this file ran the checker as a subprocess and
# read its output the same way). Every run of whitespace - the wrap's
# newline and indentation included - collapses to one space before any case
# below searches the output, so a wrap never turns a real match into a miss.
function(nfsu_run nro_name out_status out_text)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DNRO=${work}/${nro_name} -DELF=${elf}
                -DTITLE=${title} -P ${CHECKER}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text)
    set(text "${stdout_text}${stderr_text}")
    string(REGEX REPLACE "[ \t\r\n]+" " " text "${text}")
    string(STRIP "${text}" text)
    set(${out_status} "${status}" PARENT_SCOPE)
    set(${out_text} "${text}" PARENT_SCOPE)
endfunction()

# CHECKER prints exactly one of these two closing lines (message(STATUS ...)
# for a pass, message(FATAL_ERROR ...) for a failure) whether or not it also
# found what a case expects, so their presence is what tells a real verdict
# apart from a crash or a refusal to run at all (CLAUDE.md, Required
# validation: "a non-zero exit with no failure line and no summary line is
# an unknown result, not a kill").
function(nfsu_closing text out)
    if(text MATCHES "switch NRO check failed:|switch NRO check: an AArch64")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# nfsu_case_pass(<label> <nro_name> [<needle>])
#
# Requires CHECKER to accept <nro_name>. With a <needle> the closing summary
# line must also contain it (used for the control fixture); without one only
# the exit status and the closing line are required (used for the bss known
# gap, which prints the same summary either way).
function(nfsu_case_pass label nro_name)
    nfsu_run("${nro_name}" status text)
    nfsu_closing("${text}" closing)
    if(NOT closing)
        nfsu_fail("${label}: UNKNOWN, no closing line in:\n${text}")
        return()
    endif()
    if(NOT status EQUAL 0)
        nfsu_fail("${label}: expected to pass, exit ${status}:\n${text}")
        return()
    endif()
    if(ARGC GREATER 2)
        set(needle "${ARGV2}")
        string(FIND "${text}" "${needle}" found)
        if(found EQUAL -1)
            nfsu_fail("${label}: expected the summary to contain `${needle}`:\n${text}")
            return()
        endif()
    endif()
    math(EXPR count "${passed}+1")
    set(passed "${count}" PARENT_SCOPE)
endfunction()

# nfsu_case_fail(<label> <nro_name> <needle> [<banned>])
#
# Requires CHECKER to refuse <nro_name> with a non-zero exit and a failure
# line containing <needle>. A <banned> substring, when given, must NOT
# appear anywhere in the output (used for N3, where the old bug's symptom
# was CMake's own "cannot evaluate" math error leaking through).
function(nfsu_case_fail label nro_name needle)
    nfsu_run("${nro_name}" status text)
    if(status EQUAL 0)
        nfsu_fail("${label}: MISSED, exit 0:\n${text}")
        return()
    endif()
    nfsu_closing("${text}" closing)
    if(NOT closing)
        nfsu_fail("${label}: UNKNOWN, non-zero exit ${status} with no failure line:\n${text}")
        return()
    endif()
    string(FIND "${text}" "${needle}" found)
    if(found EQUAL -1)
        nfsu_fail("${label}: WRONG, expected `${needle}` in:\n${text}")
        return()
    endif()
    if(ARGC GREATER 3)
        set(banned "${ARGV3}")
        string(FIND "${text}" "${banned}" banned_found)
        if(NOT banned_found EQUAL -1)
            nfsu_fail("${label}: the output must NOT contain `${banned}`:\n${text}")
            return()
        endif()
    endif()
    math(EXPR count "${caught}+1")
    set(caught "${count}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# The control case: the checker must accept the fixture built from the
# review's real measurements, exactly as it accepts a real build.
# ---------------------------------------------------------------------------

nfsu_case_pass("control" "control.nro" "switch NRO check:")

# ---------------------------------------------------------------------------
# The 5 header corruptions PART 2 already found caught, kept here as a
# regression check against the reordering review comment 2856's F3 fix did.
# ---------------------------------------------------------------------------

nfsu_case_fail("magic changed to NRO1" "magic_nro1.nro" "no NRO0 magic")
nfsu_case_fail("image size 0xFFFFFFFF" "image_size_ffffffff.nro" "is outside the file's")
nfsu_case_fail("image size - 0x1000" "image_size_minus_0x1000.nro" "the image size")
nfsu_case_fail("image size 0" "image_size_zero.nro" "is outside the file's")
nfsu_case_fail("first 0x80 bytes zeroed" "header_zeroed.nro" "no NRO0 magic")

# ---------------------------------------------------------------------------
# The 4 header corruptions F3 adds checks for (review comment 2856):
# version, the MOD0 offset, and the two segment-table corruptions PART 2
# recorded as missed.
# ---------------------------------------------------------------------------

nfsu_case_fail("version 7" "version_7.nro" "the NRO version is 7, not 0")
nfsu_case_fail("MOD0 offset 0xFFFFFFF0" "mod_offset_bad.nro"
    "the MOD0 offset 4294967280 is outside the text segment")
nfsu_case_fail("text size 0" "text_size_zero.nro" "the ro segment is at offset")
nfsu_case_fail("data offset 0xFFFFF000" "data_offset_bad.nro" "the data segment is at offset")

# bss size (0x38) is the fifth field PART 2 found unread, and it stays
# unread: there is no other value in the file to check it against, the same
# way the review's own suggested fix leaves it out. A known gap, not
# silently assumed: this asserts the corrupted file still passes, so a
# change that starts catching it is noticed here.
nfsu_case_pass("bss size 0xFFFFFFFF (known gap, not read)" "bss_size_ffffffff.nro")

# ---------------------------------------------------------------------------
# The asset and NACP corruptions PART 2 already found caught, kept here as a
# regression check.
# ---------------------------------------------------------------------------

nfsu_case_fail("icon size 0" "icon_size_zero.nro" "carries no icon")
nfsu_case_fail("NACP size 0x3000" "nacp_size_3000.nro" "is 12288 bytes, not 0x4000")
nfsu_case_fail("NACP offset + 1" "nacp_offset_plus1.nro" "runs past the file")
nfsu_case_fail("title nfsu_switch -> nfsu_swatch" "title_mismatch.nro"
    "the NACP's first title is not")

# ---------------------------------------------------------------------------
# N3: an asset offset of 2^63 or more used to make nfsu_le call math(EXPR)
# on a hex value it cannot parse, which stopped the whole script with
# "cannot evaluate the expression" rather than a failure line. The fix
# routes it to the same out-of-range sentinel a read past the end of the
# file already returns, so this must fail with an ordinary failure line and
# never print CMake's own math error.
# ---------------------------------------------------------------------------

nfsu_case_fail("N3 icon offset 2^63" "icon_offset_2pow63.nro"
    "runs past the file" "cannot evaluate")

# ---------------------------------------------------------------------------
# The ELF must still be refused as an NRO, and accepted as an ELF - unrelated
# to F3 or R2/R3, but the checker reads both files every run, and a change
# here would otherwise go unnoticed by every case above, which all pass the
# same valid ELF.
# ---------------------------------------------------------------------------

nfsu_run("control.elf" elf_as_nro_status elf_as_nro_text)
if(elf_as_nro_status EQUAL 0)
    nfsu_fail("the checker must still refuse the ELF as an NRO:\n${elf_as_nro_text}")
else()
    math(EXPR caught "${caught}+1")
endif()

# ---------------------------------------------------------------------------
# R3 (review comment 2942, note): a non-zero text segment offset had no case
# of its own, so a mutant that disabled just the "text segment is at offset
# ... not 0" check survived the sweep in that review. Every other fixture
# above keeps the text segment at offset 0, so only this one exercises it.
# ---------------------------------------------------------------------------

nfsu_case_fail("text offset 64, not 0" "text_offset_shifted.nro"
    "the text segment is at offset 64, not 0")

file(REMOVE_RECURSE "${work}")
if(NOT failures EQUAL 0)
    message(FATAL_ERROR "switch NRO synthetic guards: ${failures} failed, ${caught} caught, ${passed} passed")
endif()
message(STATUS "switch NRO synthetic guards: ${caught} corruptions caught, "
    "${passed} passed, WRITER refuses to overwrite, none missed")
