# Issue #229: three CPU reference views of one explicit installed-part set.
# Run with VIEWER, TESTS, WORK and ROOT. CMakeLists.txt registers it as
# nfsu_tuned_frame_retail only when NFSU_RETAIL_ROOT is configured, and passes
# the root as -DROOT (issue #233); without it the script fails, never passes.
cmake_minimum_required(VERSION 3.20)
foreach(name IN ITEMS VIEWER TESTS WORK ROOT)
    if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
        message(FATAL_ERROR "${name} is required")
    endif()
endforeach()

# A directory of this process's own, so a second ctest run cannot remove or
# replace a frame this one is about to hash (issue #122).
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef run_id)
set(WORK "${WORK}/run-${run_id}")
file(MAKE_DIRECTORY "${WORK}")
set(car "${ROOT}/Cars/240SX")
set(common
    --shared-textures "${ROOT}/Global/GLOBALB.BUN"
    --shared-textures "${ROOT}/Cars/TEXTURES.BIN"
    --shared-textures "${ROOT}/Cars/WHEELS/TEXTURES.BIN"
    --stock-car 240SX --stock-parts "${ROOT}/Global/GLOBALB.BUN"
    --stock-geometry "${ROOT}/Cars/BRAKES/GEOMETRY.BIN"
    --stock-geometry "${ROOT}/Cars/PLATES/GEOMETRY.BIN"
    --stock-geometry "${ROOT}/Cars/WHEELS/GEOMETRY.BIN"
    --place-wheels
    --stock-part FRONT_BUMPER 240SX_KIT01_FRONT_BUMPER
    --stock-part REAR_BUMPER 240SX_KIT01_REAR_BUMPER
    --stock-part SKIRT 240SX_KIT01_SKIRT
    --stock-part WHEEL BBS_STYLE01_15_23
    --skin-car 240SX --skin-vinyls "${car}/VINYLS.BIN"
    --skin-doorline kit --skin-base 200 30 30 128
    --skin-vinyl 0 TRIBAL5 16 no-remap)

# These are frame pins, not observations copied into documentation.
set(pins
    # Default: WHEELS/TEXTURES.BIN resolves BBS tread; #225 resolves
    # window aliases, decals and per-copy brake materials.
    "21F0757A7B345EF63BDEE28EB8963B5C83D491258C6CB93B420A444AD59DCD00"
    # Yaw 120: WHEELS/TEXTURES.BIN side tread beside the KIT01 skirt;
    # #225 resolves its windows, decals and per-copy brake materials.
    "62B32EBFE1872C3AD387E2D79A909A69354C6BAA04A5D60F2F38C0DE4AC4EDFB"
    # Yaw 230: WHEELS/TEXTURES.BIN opposite tread and #225 rear glass,
    # decal and per-copy brake materials.
    "F29FD421F796E0286B3588F12147870204071BF9CC71312412BE4F640B377DC0")
set(angles default 120 230)
foreach(i RANGE 0 2)
    list(GET angles ${i} angle)
    list(GET pins ${i} expected)
    set(out "${WORK}/tuned-${angle}.tga")
    file(REMOVE "${out}")
    if(angle STREQUAL "default")
        set(camera)
    else()
        set(camera --yaw "${angle}")
    endif()
    execute_process(
        COMMAND "${VIEWER}" "${car}/GEOMETRY.BIN" "${car}/TEXTURES.BIN"
                "${out}" ${common} ${camera}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0 OR NOT stderr STREQUAL "" OR NOT EXISTS "${out}")
        message(FATAL_ERROR "tuned ${angle}: exit ${result}: ${stderr}")
    endif()
    if(NOT output MATCHES "stock slots: installed 25 with_model 24 drawn 24 [^\n]* overridden 4")
        message(FATAL_ERROR "tuned ${angle}: override report missing")
    endif()
    string(REGEX MATCH "triangles submitted: [0-9]+ textured [0-9]+ fallback ([0-9]+)"
           fallback_line "${output}")
    if(fallback_line STREQUAL "")
        message(FATAL_ERROR "tuned ${angle}: fallback triangle report missing")
    endif()
    set(fallback_triangles "${CMAKE_MATCH_1}")
    # After #225, the stock painted frame submits zero fallback triangles.
    # A tuned frame above that bound likely omitted the wheel texture source.
    if(fallback_triangles GREATER 0)
        message(FATAL_ERROR "tuned ${angle}: ${fallback_triangles} fallback triangles exceed stock painted 0")
    endif()
    file(SHA256 "${out}" actual)
    string(TOUPPER "${actual}" actual)
    if(NOT actual STREQUAL expected)
        list(APPEND pin_errors "tuned ${angle}: hash ${actual}, expected ${expected}")
    endif()
endforeach()
if(pin_errors)
    message(FATAL_ERROR "${pin_errors}")
endif()

execute_process(COMMAND "${TESTS}" --tuned-frame "${ROOT}"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE stderr)
list(GET pins 0 default_pin)
if(NOT result EQUAL 0 OR NOT stderr STREQUAL "" OR
   NOT output MATCHES "sha256 ${default_pin}")
    message(FATAL_ERROR "Switch CPU path differs: exit ${result}: ${output} ${stderr}")
endif()
message(STATUS "tuned 240SX: default and two orbit pins match; Switch CPU path matches")
# This process's own directory, removed after a pass; kept after a failure.
file(REMOVE_RECURSE "${WORK}")
