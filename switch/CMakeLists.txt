# The Nintendo Switch targets. This directory is configured only through
# cmake/switch-toolchain.cmake, in place of the desktop build; see the branch
# in the top-level CMakeLists.txt and docs/SWITCH_BUILD.md.

# The minimal application (issue #45). devkitPro's platform file adds libnx
# to every link, and nx_create_nro packages the ELF with the libnx default
# icon and the NACP generated here. It is the proof that the toolchain
# works, not a feature.
add_executable(nfsu_switch_app
    main.cpp
)

target_compile_features(nfsu_switch_app PRIVATE cxx_std_17)
nfsu_enable_warnings(nfsu_switch_app)

nx_generate_nacp(
    OUTPUT nfsu_switch_app.nacp
    NAME "nfsu_switch"
    AUTHOR "nfsu_switch contributors"
    VERSION "0.1.0")

nx_create_nro(nfsu_switch_app
    NACP nfsu_switch_app.nacp)

# The stock 240SX drawn by the CPU reference renderer on the console (issue
# #141), painted with the runtime body skin and orbited with the left stick
# (issue #148). It links the car-path libraries the top-level CMakeLists.txt
# declares for both configures (cmake/car_libraries.cmake), so its scene,
# skin, camera, frame and hash come from the same sources, compiled with the
# same options, as the desktop's. It reads six files from the SD card, shows
# the frame through a libnx framebuffer and reports on stdout, which nxlink
# carries to the PC; it writes no file.
add_executable(nfsu_switch_car
    car_main.cpp
)

target_link_libraries(nfsu_switch_car
    PRIVATE nfsu_stock_frame nfsu_stock_view nfsu_screen_text)
# gnu++17, as the libraries it links are compiled on the console; the
# comment at the end of cmake/car_libraries.cmake says why it is named.
set_target_properties(nfsu_switch_car PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
nfsu_enable_warnings(nfsu_switch_car)

nx_generate_nacp(
    OUTPUT nfsu_switch_car.nacp
    NAME "nfsu_switch_car"
    AUTHOR "nfsu_switch contributors"
    VERSION "0.1.0")

nx_create_nro(nfsu_switch_car
    NACP nfsu_switch_car.nacp)

# The offline shader pipeline (issue #49). Every stage source a manifest
# names is compiled once by uam into a .dksh blob, with the TGSI uam writes
# beside it, and every manifest is then checked against the blobs of its two
# stages; a finding fails the build. Nothing compiles a shader at run time:
# deko3d has no shader compiler, and nothing here links one.
# docs/SHADER_PIPELINE.md.
include(${PROJECT_SOURCE_DIR}/cmake/shader_manifest.cmake)

# cmake/switch-toolchain.cmake has already refused an SDK without uam; this
# is for a configure made with devkitPro's own toolchain file instead.
if(NOT NX_UAM_EXE)
    message(FATAL_ERROR
        "uam, the offline deko3d shader compiler, was not found. Configure with "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/switch-toolchain.cmake, which names every "
        "missing SDK piece and where it looked (docs/SWITCH_BUILD.md).")
endif()

file(GLOB NFSU_SHADER_MANIFESTS CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/shaders/*.manifest)
set(NFSU_SHADER_CHECKER ${PROJECT_SOURCE_DIR}/cmake/shader_manifest.cmake)
set(NFSU_SHADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(NFSU_SHADER_COMPILED "")
set(NFSU_SHADER_STAMPS "")
set(NFSU_SHADER_PROGRAMS "")

foreach(manifest IN LISTS NFSU_SHADER_MANIFESTS)
    nfsu_shader_manifest_stages(${manifest} program vertex_source fragment_source)
    set(check_arguments "")
    set(check_inputs "")
    foreach(stage IN ITEMS vertex fragment)
        set(source ${${stage}_source})
        get_filename_component(stem ${source} NAME)
        string(REGEX REPLACE "[.]glsl$" "" stem ${stem})
        set(dksh ${NFSU_SHADER_OUTPUT}/${stem}.dksh)
        set(tgsi ${NFSU_SHADER_OUTPUT}/${stem}.tgsi)
        if(NOT stem IN_LIST NFSU_SHADER_COMPILED)
            list(APPEND NFSU_SHADER_COMPILED ${stem})
            if(stage STREQUAL "vertex")
                set(uam_stage vert)
            else()
                set(uam_stage frag)
            endif()
            add_custom_command(
                OUTPUT ${dksh} ${tgsi}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${NFSU_SHADER_OUTPUT}
                COMMAND ${NX_UAM_EXE} -s ${uam_stage} -o ${dksh} -t ${tgsi} ${source}
                DEPENDS ${source} ${NX_UAM_EXE}
                COMMENT "Compiling ${stem}.glsl with uam"
                VERBATIM)
        endif()
        string(TOUPPER ${stage} stage_upper)
        list(APPEND check_arguments
            -D${stage_upper}_DKSH=${dksh} -D${stage_upper}_TGSI=${tgsi})
        list(APPEND check_inputs ${dksh} ${tgsi})
    endforeach()

    # The stamp is removed before the check and written only when it finds
    # nothing, so a failed check runs again on the next build.
    set(stamp ${NFSU_SHADER_OUTPUT}/${program}.checked)
    add_custom_command(
        OUTPUT ${stamp}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${stamp}
        COMMAND ${CMAKE_COMMAND} -DMODE=blob -DMANIFEST=${manifest}
                ${check_arguments} -DSTAMP=${stamp} -P ${NFSU_SHADER_CHECKER}
        DEPENDS ${manifest} ${check_inputs} ${NFSU_SHADER_CHECKER}
        COMMENT "Checking ${program} against its compiled shaders"
        VERBATIM)
    list(APPEND NFSU_SHADER_STAMPS ${stamp})
    list(APPEND NFSU_SHADER_PROGRAMS ${program})
    set(NFSU_SHADER_CHECK_${program} -DMANIFEST=${manifest} ${check_arguments})
endforeach()

add_custom_target(nfsu_switch_shaders ALL DEPENDS ${NFSU_SHADER_STAMPS})

if(NFSU_BUILD_TESTS)
    # Appended to below, once per program; never set anywhere before this,
    # so without this a stray -DNFSU_SWITCH_TESTS from the invoking
    # environment would reach set_tests_properties along with the real
    # names (review comment 2856, N4).
    set(NFSU_SWITCH_TESTS "")
    # The build step's check again, as a test over the blobs the build
    # wrote, so ctest over this tree reports it by program.
    foreach(program IN LISTS NFSU_SHADER_PROGRAMS)
        add_test(NAME nfsu_switch_shader_${program}
            COMMAND ${CMAKE_COMMAND} -DMODE=blob ${NFSU_SHADER_CHECK_${program}}
                    -P ${NFSU_SHADER_CHECKER})
        list(APPEND NFSU_SWITCH_TESTS nfsu_switch_shader_${program})
    endforeach()

    # The compiled-shader check made to fail: each case compiles copies of
    # the stages with one declaration changed, and the check must name it.
    add_test(NAME nfsu_switch_shader_blob_guards
        COMMAND ${CMAKE_COMMAND} -DUAM=${NX_UAM_EXE}
                -DCHECKER=${NFSU_SHADER_CHECKER}
                -DSHADERS=${PROJECT_SOURCE_DIR}/shaders
                -P ${PROJECT_SOURCE_DIR}/tests/shader_blob_guards.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    # What elf2nro wrote: the NRO header, the asset section and the NACP in
    # it, and the AArch64 ELF it was made from.
    add_test(NAME nfsu_switch_nro
        COMMAND ${CMAKE_COMMAND}
                -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_app.nro
                -DELF=$<TARGET_FILE:nfsu_switch_app>
                -DTITLE=nfsu_switch
                -P ${PROJECT_SOURCE_DIR}/tests/switch_nro_check.cmake)

    # The same for the stock-car application.
    add_test(NAME nfsu_switch_car_nro
        COMMAND ${CMAKE_COMMAND}
                -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_car.nro
                -DELF=$<TARGET_FILE:nfsu_switch_car>
                -DTITLE=nfsu_switch_car
                -P ${PROJECT_SOURCE_DIR}/tests/switch_nro_check.cmake)

    set_tests_properties(${NFSU_SWITCH_TESTS}
        nfsu_switch_shader_blob_guards nfsu_switch_nro nfsu_switch_car_nro
        PROPERTIES TIMEOUT 60)
endif()

# The fused multiply-add censuses. The race route of issue #61 and the
# section streamer of issue #157, which cmake/race_libraries.cmake and
# cmake/residency_libraries.cmake declare for both configures, and the
# vehicle model of issue #58, which cmake/vehicle_libraries.cmake declares.
# Nothing on the console links the first two yet; building them here is
# what lets the tree check the precondition #159 rests on, and the
# equivalent for the streamer, that each compiles on the console to the same
# float arithmetic as on the desktop: their objects hold no fused
# multiply-add. The vehicle's census asserts the precondition its desktop
# telemetry pin rests on, since this tree cannot run an AArch64 program to
# compare the hash itself. Each census keeps its own script and label.
# Appended as one block so that nothing above is reordered.
if(NFSU_BUILD_TESTS)
    if(NOT CMAKE_OBJDUMP)
        message(FATAL_ERROR "no objdump was found for the Switch toolchain")
    endif()
    add_test(NAME nfsu_switch_vehicle_fma_census
        COMMAND ${CMAKE_COMMAND}
                -DOBJDUMP=${CMAKE_OBJDUMP}
                "-DOBJECTS=$<TARGET_OBJECTS:nfsu_vehicle>"
                -DLABEL=nfsu_vehicle
                -P ${PROJECT_SOURCE_DIR}/tests/switch_fma_census.cmake)
    set_tests_properties(nfsu_switch_vehicle_fma_census PROPERTIES TIMEOUT 60)
    add_test(NAME nfsu_switch_race_route_fma_census
        COMMAND ${CMAKE_COMMAND}
                -DOBJDUMP=${CMAKE_OBJDUMP}
                "-DOBJECTS=$<TARGET_OBJECTS:nfsu_race_route>;$<TARGET_OBJECTS:nfsu_track_zones>"
                -DLABEL=nfsu_race_route
                -P ${PROJECT_SOURCE_DIR}/tests/race_route_fma_census.cmake)
    set_tests_properties(nfsu_switch_race_route_fma_census PROPERTIES TIMEOUT 60)
    add_test(NAME nfsu_switch_track_residency_fma_census
        COMMAND ${CMAKE_COMMAND}
                -DOBJDUMP=${CMAKE_OBJDUMP}
                "-DOBJECTS=$<TARGET_OBJECTS:nfsu_track_residency>"
                -DLABEL=nfsu_track_residency
                -P ${PROJECT_SOURCE_DIR}/tests/track_residency_fma_census.cmake)
    set_tests_properties(nfsu_switch_track_residency_fma_census
        PROPERTIES TIMEOUT 60)
endif()

# The asset check of issue #47: whether the SD card holds the retail files
# the first lap needs, shown on screen. It links the manifest and the check
# cmake/asset_libraries.cmake declares for both configures, the console's
# file system service beside them and the screen font, and nothing that
# renders. It reads the config file and the files the set names, for
# reading only, and writes no file. Appended as one block, so that nothing
# above is reordered. docs/SWITCH_ASSETS.md.
add_executable(nfsu_switch_assets
    assets_main.cpp
)

target_link_libraries(nfsu_switch_assets
    PRIVATE nfsu_assets nfsu_posix_file_system nfsu_screen_text)
# gnu++17, as the libraries it links are compiled on the console.
set_target_properties(nfsu_switch_assets PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
nfsu_enable_warnings(nfsu_switch_assets)

nx_generate_nacp(
    OUTPUT nfsu_switch_assets.nacp
    NAME "nfsu_switch_assets"
    AUTHOR "nfsu_switch contributors"
    VERSION "0.1.0")

nx_create_nro(nfsu_switch_assets
    NACP nfsu_switch_assets.nacp)

if(NFSU_BUILD_TESTS)
    # What elf2nro wrote, as for the other two applications.
    add_test(NAME nfsu_switch_assets_nro
        COMMAND ${CMAKE_COMMAND}
                -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_assets.nro
                -DELF=$<TARGET_FILE:nfsu_switch_assets>
                -DTITLE=nfsu_switch_assets
                -P ${PROJECT_SOURCE_DIR}/tests/switch_nro_check.cmake)
    set_tests_properties(nfsu_switch_assets_nro PROPERTIES TIMEOUT 60)
endif()

# The vehicle model on the console (issue #58): runs every built-in scripted
# input sequence of docs/VEHICLE.md through nfsu_vehicle - declared for both
# configures in cmake/vehicle_libraries.cmake - and reports each telemetry
# hash and whether "mixed" is the one pinned on the desktop. Appended as one
# block so that nothing above is reordered; nfsu_vehicle's fused
# multiply-add census is in the census block above.
add_executable(nfsu_switch_vehicle
    vehicle_main.cpp
)

target_link_libraries(nfsu_switch_vehicle PRIVATE nfsu_vehicle)
# gnu++17, as the libraries it links are compiled on the console; the
# comment at the end of cmake/car_libraries.cmake says why it is named.
set_target_properties(nfsu_switch_vehicle PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
nfsu_enable_warnings(nfsu_switch_vehicle)

nx_generate_nacp(
    OUTPUT nfsu_switch_vehicle.nacp
    NAME "nfsu_switch_vehicle"
    AUTHOR "nfsu_switch contributors"
    VERSION "0.1.0")

nx_create_nro(nfsu_switch_vehicle
    NACP nfsu_switch_vehicle.nacp)

if(NFSU_BUILD_TESTS)
    # The NRO and its ELF, as for the other two applications.
    add_test(NAME nfsu_switch_vehicle_nro
        COMMAND ${CMAKE_COMMAND}
                -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_vehicle.nro
                -DELF=$<TARGET_FILE:nfsu_switch_vehicle>
                -DTITLE=nfsu_switch_vehicle
                -P ${PROJECT_SOURCE_DIR}/tests/switch_nro_check.cmake)

    set_tests_properties(nfsu_switch_vehicle_nro PROPERTIES TIMEOUT 60)
endif()

# The race state machine of issue #62, which cmake/race_libraries.cmake
# declares for both configures: its console object holds no fused
# multiply-add, so that it rounds every time exactly as the desktop does.
# The census is issue #91's own script, over this library's objects.
# Appended as one block, so that nothing above is reordered.
if(NFSU_BUILD_TESTS)
    add_test(NAME nfsu_switch_race_timing_fma_census
        COMMAND ${CMAKE_COMMAND}
                -DOBJDUMP=${CMAKE_OBJDUMP}
                "-DOBJECTS=$<TARGET_OBJECTS:nfsu_race_timing>"
                -DLABEL=nfsu_race_timing
                -P ${PROJECT_SOURCE_DIR}/tests/switch_fma_census.cmake)
    set_tests_properties(nfsu_switch_race_timing_fma_census PROPERTIES TIMEOUT 60)
endif()
