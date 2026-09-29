# Issue #229: three CPU reference views of one explicit installed-part set.
# Run with VIEWER, TESTS, ROOT (retail installation) and WORK (scratch output).
cmake_minimum_required(VERSION 3.20)
foreach(name IN ITEMS VIEWER TESTS ROOT WORK)
    if(NOT DEFINED ${name})
        message(FATAL_ERROR "${name} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK}")
set(car "${ROOT}/Cars/240SX")
set(common
    --shared-textures "${ROOT}/Global/GLOBALB.BUN"
    --shared-textures "${ROOT}/Cars/TEXTURES.BIN"
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

# These are frame pins, not observations copied into documentation. The
# default changes because KIT01 bumpers and skirt, BBS wheels, red paint and
# TRIBAL5 replace the stock appearance. Yaw 120 exposes the side skirt and
# both wheels; yaw 230 exposes the rear bumper and the opposite wheel pair.
set(pins
    "32568614B0CF67D752F0399C4849EED84BEB23F24C6D7B334DBB9AC0108AA216"
    "5FBAEC6B6AD1694AA6B57FD38A5DA21E1F9B8DA7B3848D59A55972550B6B3228"
    "22AD985449F7C174998B9D94D3F1620FFB3223963A7A6A6D31FFC8CFF1D0AF2D")
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
    file(SHA256 "${out}" actual)
    string(TOUPPER "${actual}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "tuned ${angle}: hash ${actual}, expected ${expected}")
    endif()
endforeach()

execute_process(COMMAND "${TESTS}" --tuned-frame "${ROOT}"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE stderr)
if(NOT result EQUAL 0 OR NOT stderr STREQUAL "" OR
   NOT output MATCHES "sha256 32568614B0CF67D752F0399C4849EED84BEB23F24C6D7B334DBB9AC0108AA216")
    message(FATAL_ERROR "Switch CPU path differs: exit ${result}: ${output} ${stderr}")
endif()
message(STATUS "tuned 240SX: default and two orbit pins match; Switch CPU path matches")
