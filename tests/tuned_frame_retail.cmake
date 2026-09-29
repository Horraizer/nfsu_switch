# Issue #229: three CPU reference views of one explicit installed-part set.
# Run with VIEWER, TESTS, WORK and NFSU_RETAIL_ROOT (or explicit ROOT).
cmake_minimum_required(VERSION 3.20)
foreach(name IN ITEMS VIEWER TESTS WORK)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "${name} is required")
    endif()
endforeach()
if(NOT DEFINED ROOT OR ROOT STREQUAL "")
    if(DEFINED ENV{NFSU_RETAIL_ROOT} AND NOT "$ENV{NFSU_RETAIL_ROOT}" STREQUAL "")
        set(ROOT "$ENV{NFSU_RETAIL_ROOT}")
    else()
        message("SKIPPED: set NFSU_RETAIL_ROOT to run the tuned retail frame pins")
        return()
    endif()
endif()

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
    --skin-vinyl 0 TRIBAL5 16 no-remap
    --material-state off)

# These are frame pins, not observations copied into documentation.
set(pins
    # Default: WHEELS/TEXTURES.BIN resolves BBS tread; #225 resolves
    # window aliases, decals and per-copy brake materials.
    "C60371D49654443E0F7CE264224FE5A6142BBD3CCA6B9342AB7477793909BFAF"
    # Yaw 120: WHEELS/TEXTURES.BIN side tread beside the KIT01 skirt;
    # #225 resolves its windows, decals and per-copy brake materials.
    "D46E827BD18DEFBC5AF0BD8D9C728516D08F35FD6294F9A11E07591879231FCC"
    # Yaw 230: WHEELS/TEXTURES.BIN opposite tread and #225 rear glass,
    # decal and per-copy brake materials.
    "49EDEB280EC45B9C70A0CE5BFAA594EEB39FC16108F82B2A159F266FE4BD79C7")
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
