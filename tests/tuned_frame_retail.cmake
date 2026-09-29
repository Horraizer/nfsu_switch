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
    --skin-vinyl 0 TRIBAL5 16 no-remap)

# These are frame pins, not observations copied into documentation.
set(pins
    # Default: front wheel now has its BBS rim and black treaded tyre.
    "3A190BBF3B224B6F096B2C4A8127A407AC47AEC00B8C15C41AAEFD464F54379F"
    # Yaw 120: the side pair resolves beside the KIT01 skirt.
    "9596E10366EB47E9460B770E22321D316DEBA557B2550A31A267CE236B16595D"
    # Yaw 230: the opposite pair resolves beside the KIT01 rear bumper.
    "FAA4DBFD23B5CC27E6C078A4DA5DE44A1D3FB4E4FEB8DB3C567BB80E9B38E88B")
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
    # The stock painted frame submits 233 fallback triangles. A tuned frame
    # exceeding that bound likely omitted the wheel texture source.
    if(fallback_triangles GREATER 233)
        message(FATAL_ERROR "tuned ${angle}: ${fallback_triangles} fallback triangles exceed stock painted 233")
    endif()
    file(SHA256 "${out}" actual)
    string(TOUPPER "${actual}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "tuned ${angle}: hash ${actual}, expected ${expected}")
    endif()
endforeach()

execute_process(COMMAND "${TESTS}" --tuned-frame "${ROOT}"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE stderr)
if(NOT result EQUAL 0 OR NOT stderr STREQUAL "" OR
   NOT output MATCHES "sha256 3A190BBF3B224B6F096B2C4A8127A407AC47AEC00B8C15C41AAEFD464F54379F")
    message(FATAL_ERROR "Switch CPU path differs: exit ${result}: ${output} ${stderr}")
endif()
message(STATUS "tuned 240SX: default and two orbit pins match; Switch CPU path matches")
