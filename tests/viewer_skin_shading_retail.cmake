# Pin the viewer's color-only skin path without --stock-car. Both frames use
# GLOBALB from --shared-textures for the swatch and material tables.
cmake_minimum_required(VERSION 3.20)
foreach(name IN ITEMS VIEWER ROOT WORK)
    if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
        message(FATAL_ERROR "${name} is required")
    endif()
endforeach()

cmake_path(IS_PREFIX CMAKE_CURRENT_BINARY_DIR "${WORK}" NORMALIZE work_in_build)
if(NOT work_in_build)
    message(FATAL_ERROR "WORK must be inside the build directory")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(WORK "${WORK}/run-${run_id}")
file(MAKE_DIRECTORY "${WORK}")
set(car "${ROOT}/Cars/240SX")
set(common
    --shared-textures "${ROOT}/Global/GLOBALB.BUN"
    --shared-textures "${ROOT}/Cars/TEXTURES.BIN"
    --skin-car 240SX --material-state retail --car-shading on)
foreach(name IN ITEMS blue red)
    if(name STREQUAL "blue")
        set(r 30)
        set(g 60)
        set(b 160)
        set(expected 28BCDAE87119D6D1C4D23E73CD622EDAA2C82931A27582902653E21606C16601)
    else()
        set(r 200)
        set(g 30)
        set(b 30)
        set(expected 26E55F473A02FCE0C3C06E0A82F17F880402EBB4DAAC4133BDF6D3525483BF50)
    endif()
    set(out "${WORK}/${name}.tga")
    execute_process(
        COMMAND "${VIEWER}" "${car}/GEOMETRY.BIN" "${car}/TEXTURES.BIN"
                "${out}" ${common} --skin-base ${r} ${g} ${b} 128
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0 OR NOT stderr STREQUAL "" OR NOT EXISTS "${out}")
        message(FATAL_ERROR "${name} skin-only viewer: exit ${result}: ${stderr}")
    endif()
    file(SHA256 "${out}" actual)
    string(TOUPPER "${actual}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${name} skin-only frame: ${actual}, expected ${expected}")
    endif()
endforeach()
message(STATUS "blue and red skin-only shading frames match")
file(REMOVE_RECURSE "${WORK}")
