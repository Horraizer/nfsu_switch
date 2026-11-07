# The read-only rule for the retail installation, as a CTest fixture (issue
# #233). CMakeLists.txt registers this script twice around every retail
# test: once with MODE=before as the fixture's setup and once with
# MODE=after as its cleanup.
#
#   -DMODE=before|after   which half to run
#   -DROOT=<dir>          the retail root, NFSU_RETAIL_ROOT
#   -DMANIFEST=<file>     the before manifest; it lives in the build
#                         directory, so two build directories never share it
#
# A manifest line is the path relative to ROOT, then "dir" or the size in
# bytes, then the modification time in UTC to the microsecond, for every file
# and directory under ROOT. Neither half writes anything under ROOT.
#
# MODE=before writes the manifest. MODE=after lists ROOT again, deletes the
# manifest by its exact name, and fails on any difference: a file or
# directory added, removed, resized or touched. Content is not compared.
#
# The name is unique per build directory, not per process: the two halves
# are two processes that CTest starts separately and passes nothing between,
# so the only name both can find is one fixed in the build tree. Two ctest
# runs in one build directory, or a manifest an interrupted run left behind,
# are handled so that neither can hide a change:
# - MODE=before that finds a manifest already there does not replace it. It
#   compares the root with it instead and fails on any difference, so a
#   change made after the first run's setup fails the second run's setup.
# - MODE=after consumes the manifest. A second run whose cleanup comes after
#   the first run's finds none, and fails, saying so, rather than passing.
# A stale manifest identical to the root is reused; one that differs fails
# the setup until it is deleted by hand, which the message says.

cmake_minimum_required(VERSION 3.20)

foreach(required MODE ROOT MANIFEST)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "FAIL: retail_manifest.cmake needs -D${required}")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${ROOT}")
    message(FATAL_ERROR "FAIL: retail root is not a directory: ${ROOT}")
endif()

function(nfsu_retail_listing root out_lines out_count)
    file(GLOB_RECURSE entries LIST_DIRECTORIES true "${root}/*")
    list(SORT entries)
    set(lines "")
    set(files 0)
    foreach(entry IN LISTS entries)
        file(RELATIVE_PATH relative "${root}" "${entry}")
        file(TIMESTAMP "${entry}" stamp "%Y-%m-%dT%H:%M:%S.%f" UTC)
        if(IS_DIRECTORY "${entry}")
            set(kind "dir")
        else()
            file(SIZE "${entry}" kind)
            math(EXPR files "${files} + 1")
        endif()
        string(APPEND lines "${relative}\t${kind}\t${stamp}\n")
    endforeach()
    set(${out_lines} "${lines}" PARENT_SCOPE)
    set(${out_count} "${files}" PARENT_SCOPE)
endfunction()

# Sets `out` to one line per entry that is in only one of the two listings.
function(nfsu_retail_difference before after out)
    string(REPLACE ";" "\\;" before_escaped "${before}")
    string(REPLACE ";" "\\;" after_escaped "${after}")
    string(REPLACE "\n" ";" before_list "${before_escaped}")
    string(REPLACE "\n" ";" after_list "${after_escaped}")
    set(report "")
    foreach(line IN LISTS before_list)
        if(NOT line STREQUAL "" AND NOT line IN_LIST after_list)
            string(APPEND report "  before only: ${line}\n")
        endif()
    endforeach()
    foreach(line IN LISTS after_list)
        if(NOT line STREQUAL "" AND NOT line IN_LIST before_list)
            string(APPEND report "  after only:  ${line}\n")
        endif()
    endforeach()
    set(${out} "${report}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "before")
    nfsu_retail_listing("${ROOT}" listing count)
    if(count EQUAL 0)
        message(FATAL_ERROR "FAIL: no files under the retail root ${ROOT}")
    endif()
    if(EXISTS "${MANIFEST}")
        file(READ "${MANIFEST}" earlier)
        if(NOT earlier STREQUAL listing)
            nfsu_retail_difference("${earlier}" "${listing}" report)
            message(FATAL_ERROR "FAIL: the retail installation differs from the "
                                "manifest another ctest run in this build "
                                "directory, or an interrupted one, took at "
                                "${MANIFEST}:\n${report}If no other run is "
                                "live, delete that file and rerun.")
        endif()
        message("PASS: retail manifest already taken and identical, ${count} "
                "files under ${ROOT}; kept")
        return()
    endif()
    get_filename_component(parent "${MANIFEST}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${MANIFEST}" "${listing}")
    message("PASS: retail manifest taken, ${count} files under ${ROOT}")
elseif(MODE STREQUAL "after")
    if(NOT EXISTS "${MANIFEST}")
        message(FATAL_ERROR "FAIL: no before manifest at ${MANIFEST}; the "
                            "setup test did not run, or another ctest run in "
                            "this build directory consumed it first")
    endif()
    file(READ "${MANIFEST}" before)
    nfsu_retail_listing("${ROOT}" after count)
    file(REMOVE "${MANIFEST}")
    if(NOT before STREQUAL after)
        nfsu_retail_difference("${before}" "${after}" report)
        message(FATAL_ERROR "FAIL: the retail installation changed while the "
                            "retail tests ran:\n${report}")
    endif()
    message("PASS: retail manifest unchanged, ${count} files under ${ROOT}")
else()
    message(FATAL_ERROR "FAIL: MODE must be before or after, not ${MODE}")
endif()
