# Enumerate CMake's own targets after all Switch subdirectories are declared.
# Custom packaging/shader targets and imported SDK libraries have no project
# objects. Every compiled target, including unlinked libraries, participates.
function(nfsu_collect_compiled_targets directory output)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    set(result "")
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            list(APPEND result ${target})
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        nfsu_collect_compiled_targets("${child}" child_targets)
        list(APPEND result ${child_targets})
    endforeach()
    set(${output} "${result}" PARENT_SCOPE)
endfunction()

function(nfsu_switch_fma_census)
    if(NOT CMAKE_OBJDUMP)
        message(FATAL_ERROR "no objdump was found for the Switch toolchain")
    endif()
    nfsu_collect_compiled_targets("${PROJECT_SOURCE_DIR}" targets)
    set(content "")
    foreach(target IN LISTS targets)
        string(APPEND content "${target}|$<JOIN:$<TARGET_OBJECTS:${target}>,\n${target}|>\n")
    endforeach()
    set(manifest "${PROJECT_BINARY_DIR}/switch-objects-$<CONFIG>.txt")
    file(GENERATE OUTPUT "${manifest}" CONTENT "${content}")
    add_test(NAME nfsu_switch_fma_census
        COMMAND ${CMAKE_COMMAND} -DOBJDUMP=${CMAKE_OBJDUMP}
            -DMANIFEST=${manifest} -DTREE=${PROJECT_BINARY_DIR}
            -P ${PROJECT_SOURCE_DIR}/tests/switch_fma_census.cmake)
    add_test(NAME nfsu_switch_fma_census_guards
        COMMAND ${CMAKE_COMMAND} -DOBJDUMP=${CMAKE_OBJDUMP}
            -DCOMPILER=${CMAKE_CXX_COMPILER}
            -DCHECKER=${PROJECT_SOURCE_DIR}/tests/switch_fma_census.cmake
            -DWORK=${PROJECT_BINARY_DIR}/fma-census-guards
            -P ${PROJECT_SOURCE_DIR}/tests/switch_fma_census_guards.cmake)
    set_tests_properties(nfsu_switch_fma_census nfsu_switch_fma_census_guards
        PROPERTIES TIMEOUT 120)
endfunction()