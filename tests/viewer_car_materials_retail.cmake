# The desktop viewer's stock-car path resolves every material texture for
# the cars whose lamp and window groups need the runtime name resolution
# (issue #315 round 4 F2).
#
# nfsu_viewer loads its geometry and texture sources itself, separately from
# prepare_stock_frame_impl, so this guard drives the real command line the
# reviewer used - a painted stock car at two angles - for the two cars that
# need the lamp aliases most (CELICA also binds second-layer windows) and
# one that needs none (240SX, the pinned baseline). It requires:
#
#   - exit 0 with nothing on standard error and a fresh frame file of the
#     pinned 3686418-byte TGA size;
#   - the report's "material groups" line to end with "unresolved 0", the
#     count the diagnostic checker draws (the stock-frame path pins the same
#     zero through nfsu_car_selection_retail);
#   - and, so the check can fail, the 240SX baseline hashes to equal the
#     viewer's own pinned painted frame, proving the runs render the real
#     assembly rather than an empty image.
#
#   cmake -DVIEWER=<nfsu_viewer> -DROOT=<retail root> -DWORK=<own directory>
#         -P tests/viewer_car_materials_retail.cmake

cmake_minimum_required(VERSION 3.20)

foreach(variable IN ITEMS VIEWER ROOT WORK)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be defined")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
if(EXISTS "${WORK}")
    message(FATAL_ERROR "the work directory could not be cleared")
endif()
file(MAKE_DIRECTORY "${WORK}")

set(viewer_painted_sha_240sx "FE7506F173A871A208F94451DFC71A3F3AEBF1EBE677C28AF2B73AD9CEA3AEE8")
# The stock-frame path's pinned CELICA frame (tests/car_selection_tests.cpp,
# round-4 row, default yaw 35). Requiring the viewer's own CELICA render to hash the same is
# the cross-path equivalence of include/nfsu/stock_frame.h, held for a car
# that needs the lamp aliases and the second-layer windows.
set(viewer_painted_sha_celica "F2B4DE7423EFC47EA469D2CCDE040435C041C4E789DBAD5EC22E8D1B8D0F00DC")

set(viewer_painted_sha_CELICA_0 "2640BBAD75B135BEBAD6B92F04AEB0BDB9DCE3F91192AD65687796A34F846B6E")
set(viewer_painted_sha_CELICA_180 "256C042237376A0FFEE8AAE3DD71274072611B98B181C8CF2D42A6ECE85475B2")
set(viewer_painted_sha_SUPRA_180 "91B0BCE30CC97449504EF83F2CF31D4E5461FD95100F451D9C68638A1AACF5AA")
set(viewer_painted_sha_240SX_0 "43BA2CE0D4940C65298EB2C3BD9135CCA59DA17FF90E4E5D0F8D00044456370B")
set(viewer_painted_sha_CELICA_35 "F2B4DE7423EFC47EA469D2CCDE040435C041C4E789DBAD5EC22E8D1B8D0F00DC")
set(viewer_painted_sha_240SX_35 "FE7506F173A871A208F94451DFC71A3F3AEBF1EBE677C28AF2B73AD9CEA3AEE8")

function(run_viewer car yaw out var_result var_output var_error)
    execute_process(
        COMMAND "${VIEWER}"
                "${ROOT}/Cars/${car}/GEOMETRY.BIN" "${ROOT}/Cars/${car}/TEXTURES.BIN"
                "${out}"
                --shared-textures "${ROOT}/Global/GLOBALB.BUN"
                --shared-textures "${ROOT}/Cars/TEXTURES.BIN"
                --stock-car ${car}
                --stock-parts "${ROOT}/Global/GLOBALB.BUN"
                --stock-geometry "${ROOT}/Cars/BRAKES/GEOMETRY.BIN"
                --stock-geometry "${ROOT}/Cars/PLATES/GEOMETRY.BIN"
                --place-wheels --yaw ${yaw}
                --skin-car ${car} --skin-doorline plain --skin-base 30 60 160 128
        RESULT_VARIABLE ${var_result}
        OUTPUT_VARIABLE ${var_output}
        ERROR_VARIABLE ${var_error})
    set(${var_result} ${${var_result}} PARENT_SCOPE)
    set(${var_output} ${${var_output}} PARENT_SCOPE)
    set(${var_error} ${${var_error}} PARENT_SCOPE)
endfunction()

foreach(case IN ITEMS "CELICA;0" "CELICA;180" "SUPRA;180" "240SX;0" "CELICA;35" "240SX;35")
    list(GET case 0 car)
    list(GET case 1 yaw)
    set(out "${WORK}/${car}_${yaw}.tga")
    run_viewer(${car} ${yaw} "${out}" result output error_out)
    string(REPLACE "\r\n" "\n" output "${output}")
    string(REPLACE "\r\n" "\n" error_out "${error_out}")
    if(NOT output MATCHES "\ncamera: yaw ${yaw}([.]0+)? pitch ")
        message(FATAL_ERROR "${car}@${yaw}: viewer did not use the requested yaw:\n${output}")
    endif()
    # Exit 0 is a clean run. Exit 3 is the viewer's own "issues were
    # reported with the inputs" status, which SUPRA takes for its
    # documented pre-existing mesh rejections (docs/CAR_SCENE.md: three
    # *_HEADLIGHT_C meshes with a short vertex payload); the frame is still
    # rendered. Every standard-error line must be one of those documented
    # parser notes, so a new kind of problem still fails this gate.
    if(NOT result EQUAL 0 AND NOT result EQUAL 3)
        message(FATAL_ERROR "${car}@${yaw}: the viewer exited ${result}:\n${error_out}")
    endif()
    # Every standard-error line must be one of those documented parser notes
    # (the messages themselves contain semicolons, so the lines are checked
    # by subtracting the allowed ones rather than by splitting a list).
    string(REGEX REPLACE
        "(^|\n)(geometry issue|stock geometry [0-9]+ issue) at [^\n]*" "\\1"
        residual "${error_out}")
    string(REPLACE "\n" "" residual "${residual}")
    string(STRIP "${residual}" residual)
    if(NOT residual STREQUAL "")
        message(FATAL_ERROR
            "${car}@${yaw}: unexpected standard error \"${residual}\" (exit ${result})")
    endif()
    if(NOT EXISTS "${out}")
        message(FATAL_ERROR "${car}@${yaw}: the viewer wrote no frame")
    endif()
    file(SIZE "${out}" size)
    if(NOT size EQUAL 3686418)
        message(FATAL_ERROR "${car}@${yaw}: ${size} bytes, a 1280x720 TGA is 3686418")
    endif()
    if(NOT output MATCHES "\nmaterial groups: ([0-9]+) resolved ([0-9]+) unresolved ([0-9]+)")
        message(FATAL_ERROR "${car}@${yaw}: no material groups line:\n${output}")
    endif()
    if(NOT CMAKE_MATCH_3 EQUAL 0)
        message(FATAL_ERROR
            "${car}@${yaw}: ${CMAKE_MATCH_3} unresolved material groups still draw the "
            "diagnostic checker:\n${output}")
    endif()
    if(CMAKE_MATCH_2 EQUAL 0)
        message(FATAL_ERROR "${car}@${yaw}: no material group resolved at all:\n${output}")
    endif()
    file(SHA256 "${out}" hash)
    string(TOUPPER "${hash}" hash)
    if(car STREQUAL "240SX" AND yaw EQUAL 35)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "240SX@35: the baseline exited ${result}, not 0:\n${error_out}")
        endif()
        if(NOT hash STREQUAL "${viewer_painted_sha_240sx}")
            message(FATAL_ERROR
                "240SX@35: the viewer's painted frame is ${hash}, pinned ${viewer_painted_sha_240sx}")
        endif()
    endif()
    if(car STREQUAL "CELICA" AND yaw EQUAL 35)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "CELICA@35: exited ${result}, not 0:\n${error_out}")
        endif()
        if(NOT hash STREQUAL "${viewer_painted_sha_celica}")
            message(FATAL_ERROR
                "CELICA@35: the viewer's painted frame is ${hash}, but the stock-frame path "
                "pins ${viewer_painted_sha_celica}; the two paths diverged")
        endif()
    endif()
    set(pin_name "viewer_painted_sha_${car}_${yaw}")
    if(NOT hash STREQUAL "${${pin_name}}")
        message(FATAL_ERROR "${car}@${yaw}: frame ${hash}, pinned ${${pin_name}}")
    endif()
    message(STATUS "viewer materials: ${car}@${yaw}: ${CMAKE_MATCH_2} groups resolved, 0 unresolved, sha256 ${hash}")
endforeach()

file(REMOVE_RECURSE "${WORK}")
