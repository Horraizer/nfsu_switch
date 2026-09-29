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
# and directory under ROOT. MODE=before deletes any earlier manifest by its
# exact name before it writes the new one, so a stale file from an earlier
# run can never stand in for this one. MODE=after lists ROOT again and fails
# on any difference: a file or directory added, removed, resized or touched.
# Neither half writes anything under ROOT.

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

if(MODE STREQUAL "before")
    file(REMOVE "${MANIFEST}")
    if(EXISTS "${MANIFEST}")
        message(FATAL_ERROR "FAIL: could not remove the earlier manifest ${MANIFEST}")
    endif()
    nfsu_retail_listing("${ROOT}" listing count)
    if(count EQUAL 0)
        message(FATAL_ERROR "FAIL: no files under the retail root ${ROOT}")
    endif()
    get_filename_component(parent "${MANIFEST}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${MANIFEST}" "${listing}")
    message("PASS: retail manifest taken, ${count} files under ${ROOT}")
elseif(MODE STREQUAL "after")
    if(NOT EXISTS "${MANIFEST}")
        message(FATAL_ERROR "FAIL: no before manifest at ${MANIFEST}; "
                            "the setup test did not run or did not finish")
    endif()
    file(READ "${MANIFEST}" before)
    nfsu_retail_listing("${ROOT}" after count)
    if(NOT before STREQUAL after)
        # Name what changed, one line per differing entry, before failing.
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
        message(FATAL_ERROR "FAIL: the retail installation changed while the "
                            "retail tests ran:\n${report}")
    endif()
    message("PASS: retail manifest unchanged, ${count} files under ${ROOT}")
else()
    message(FATAL_ERROR "FAIL: MODE must be before or after, not ${MODE}")
endif()
