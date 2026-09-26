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
set(NFSU_SHADER_DERIVATIVES ${PROJECT_SOURCE_DIR}/tools/uam_derivatives.py)
set(NFSU_SHADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(NFSU_SHADER_COMPILED "")
set(NFSU_SHADER_STAMPS "")
set(NFSU_SHADER_PROGRAMS "")

foreach(manifest IN LISTS NFSU_SHADER_MANIFESTS)
    nfsu_shader_manifest_stages(${manifest} program vertex_source fragment_source)
    set(check_arguments "")
    set(check_inputs "")
    set(check_derivatives "")
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
                COMMAND ${NX_UAM_EXE} -s ${uam_stage} -o ${dksh}.raw -t ${tgsi} ${source}
                COMMAND ${Python3_EXECUTABLE} ${NFSU_SHADER_DERIVATIVES}
                    ${dksh}.raw ${tgsi} ${dksh}
                BYPRODUCTS ${dksh}.raw
                DEPENDS ${source} ${NX_UAM_EXE} ${NFSU_SHADER_DERIVATIVES}
                COMMENT "Compiling ${stem}.glsl with uam"
                VERBATIM)
        endif()
        string(TOUPPER ${stage} stage_upper)
        list(APPEND check_arguments
            -D${stage_upper}_DKSH=${dksh} -D${stage_upper}_TGSI=${tgsi})
        list(APPEND check_inputs ${dksh} ${tgsi})
        list(APPEND check_derivatives COMMAND ${Python3_EXECUTABLE}
            ${NFSU_SHADER_DERIVATIVES} --check ${dksh} ${tgsi})
    endforeach()

    # The stamp is removed before the check and written only when it finds
    # nothing, so a failed check runs again on the next build.
    set(stamp ${NFSU_SHADER_OUTPUT}/${program}.checked)
    add_custom_command(
        OUTPUT ${stamp}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${stamp}
        ${check_derivatives}
        COMMAND ${CMAKE_COMMAND} -DMODE=blob -DMANIFEST=${manifest}
                ${check_arguments} -DSTAMP=${stamp} -P ${NFSU_SHADER_CHECKER}
        DEPENDS ${manifest} ${check_inputs} ${NFSU_SHADER_CHECKER} ${NFSU_SHADER_DERIVATIVES}
        COMMENT "Checking ${program} against its compiled shaders"
        VERBATIM)
    list(APPEND NFSU_SHADER_STAMPS ${stamp})
    list(APPEND NFSU_SHADER_PROGRAMS ${program})
    set(NFSU_SHADER_CHECK_${program} -DMANIFEST=${manifest} ${check_arguments})
endforeach()

add_custom_target(nfsu_switch_shaders ALL DEPENDS ${NFSU_SHADER_STAMPS})

if(NFSU_BUILD_TESTS)
    add_test(NAME nfsu_uam_derivatives_tests
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/uam_derivatives_tests.py)
    foreach(stage IN ITEMS placed.vert textured.frag fallback.frag)
        add_test(NAME nfsu_switch_derivatives_${stage}
            COMMAND ${Python3_EXECUTABLE} ${NFSU_SHADER_DERIVATIVES} --check
                ${NFSU_SHADER_OUTPUT}/${stage}.dksh ${NFSU_SHADER_OUTPUT}/${stage}.tgsi)
    endforeach()
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

# Switch-only hardware code is an application, not a neutral library. The
# recorder/ledger remain in the classified neutral nfsu_draw_commands and
# nfsu_gpu_upload targets. Only checked offline blobs enter this RomFS.
add_executable(nfsu_switch_deko3d deko_main.cpp deko_renderer.cpp)
target_link_libraries(nfsu_switch_deko3d PRIVATE
    nfsu_draw_commands nfsu_stock_frame nfsu_screen_text deko3d)
set_target_properties(nfsu_switch_deko3d PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
nfsu_enable_warnings(nfsu_switch_deko3d)

set(NFSU_DEKO_ROMFS ${CMAKE_CURRENT_BINARY_DIR}/deko_romfs)
set(NFSU_DEKO_BLOBS "")
foreach(stage IN ITEMS placed.vert textured.frag fallback.frag)
    set(blob ${NFSU_DEKO_ROMFS}/shaders/${stage}.dksh)
    add_custom_command(OUTPUT ${blob}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NFSU_DEKO_ROMFS}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${NFSU_SHADER_OUTPUT}/${stage}.dksh ${blob}
        DEPENDS ${NFSU_SHADER_OUTPUT}/${stage}.dksh ${NFSU_SHADER_STAMPS}
        VERBATIM)
    list(APPEND NFSU_DEKO_BLOBS ${blob})
endforeach()
add_custom_target(nfsu_switch_deko_romfs DEPENDS ${NFSU_DEKO_BLOBS})
set_target_properties(nfsu_switch_deko_romfs PROPERTIES
    DKP_ASSET_FOLDER ${NFSU_DEKO_ROMFS} DKP_ASSET_FILES "${NFSU_DEKO_BLOBS}")
nx_generate_nacp(OUTPUT nfsu_switch_deko3d.nacp NAME "nfsu_switch_deko3d"
    AUTHOR "nfsu_switch contributors" VERSION "0.1.0")
nx_create_nro(nfsu_switch_deko3d NACP nfsu_switch_deko3d.nacp
    ROMFS nfsu_switch_deko_romfs)

if(NFSU_BUILD_TESTS)
    add_test(NAME nfsu_switch_deko3d_nro
        COMMAND ${CMAKE_COMMAND}
            -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_deko3d.nro
            -DELF=$<TARGET_FILE:nfsu_switch_deko3d>
            -DTITLE=nfsu_switch_deko3d -DSHADERS=${NFSU_SHADER_OUTPUT}
            -P ${PROJECT_SOURCE_DIR}/tests/switch_deko_romfs_check.cmake)
    set_tests_properties(nfsu_switch_deko3d_nro PROPERTIES TIMEOUT 60)
endif()

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

# The driving controls and chase camera of issue #59 inherit the global
# floating-point policy; the whole-tree census covers their objects.
# The application that runs them on the console: the built-in demonstration
# with its hash against the desktop pin, then the pad driving the same
# session live on the text console. It reads no file and writes none.
add_executable(nfsu_switch_drive
    drive_main.cpp
)

target_link_libraries(nfsu_switch_drive PRIVATE nfsu_drive_session nfsu_stock_view)
# gnu++17, as the libraries it links are compiled on the console; the
# comment at the end of cmake/car_libraries.cmake says why it is named.
set_target_properties(nfsu_switch_drive PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
nfsu_enable_warnings(nfsu_switch_drive)

nx_generate_nacp(
    OUTPUT nfsu_switch_drive.nacp
    NAME "nfsu_switch_drive"
    AUTHOR "nfsu_switch contributors"
    VERSION "0.1.0")

nx_create_nro(nfsu_switch_drive
    NACP nfsu_switch_drive.nacp)

if(NFSU_BUILD_TESTS)
    # The NRO and its ELF, as for the other applications.
    add_test(NAME nfsu_switch_drive_nro
        COMMAND ${CMAKE_COMMAND}
                -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_drive.nro
                -DELF=$<TARGET_FILE:nfsu_switch_drive>
                -DTITLE=nfsu_switch_drive
                -P ${PROJECT_SOURCE_DIR}/tests/switch_nro_check.cmake)
    set_tests_properties(nfsu_switch_drive_nro PROPERTIES TIMEOUT 60)
endif()

# Same deko3d draw path, offscreen readback and on-screen CPU comparison.
# No game asset is packaged; only the already-checked offline shaders.
add_executable(nfsu_switch_compare compare_main.cpp deko_renderer.cpp)
target_link_libraries(nfsu_switch_compare PRIVATE
    nfsu_draw_commands nfsu_stock_frame nfsu_screen_text nfsu_frame_compare deko3d)
set_target_properties(nfsu_switch_compare PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
nfsu_enable_warnings(nfsu_switch_compare)
nx_generate_nacp(OUTPUT nfsu_switch_compare.nacp NAME "nfsu_switch_compare"
    AUTHOR "nfsu_switch contributors" VERSION "0.1.0")
nx_create_nro(nfsu_switch_compare NACP nfsu_switch_compare.nacp
    ROMFS nfsu_switch_deko_romfs)
if(NFSU_BUILD_TESTS)
    add_test(NAME nfsu_switch_compare_nro
        COMMAND ${CMAKE_COMMAND}
            -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_compare.nro
            -DELF=$<TARGET_FILE:nfsu_switch_compare>
            -DTITLE=nfsu_switch_compare -DSHADERS=${NFSU_SHADER_OUTPUT}
            -P ${PROJECT_SOURCE_DIR}/tests/switch_deko_romfs_check.cmake)
    set_tests_properties(nfsu_switch_compare_nro PROPERTIES TIMEOUT 60)
endif()

# The vehicle on the track on the console (issue #60): runs every built-in
# scenario of docs/VEHICLE_TRACK.md through nfsu_vehicle_track - declared
# for both configures in cmake/vehicle_track_libraries.cmake, with the
# track contact queries it steps on - over the proving ground, and over
# event 1's circuit when its TRACKB file is on the card, and reports each
# telemetry hash and whether it is the one pinned on the desktop. Appended
# as one block so that nothing above is reordered.
add_executable(nfsu_switch_vehicle_track
    vehicle_track_main.cpp
)

target_link_libraries(nfsu_switch_vehicle_track PRIVATE nfsu_vehicle_track)
# gnu++17, as the libraries it links are compiled on the console; the
# comment at the end of cmake/car_libraries.cmake says why it is named.
set_target_properties(nfsu_switch_vehicle_track PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
nfsu_enable_warnings(nfsu_switch_vehicle_track)

nx_generate_nacp(
    OUTPUT nfsu_switch_vehicle_track.nacp
    NAME "nfsu_switch_vehicle_track"
    AUTHOR "nfsu_switch contributors"
    VERSION "0.1.0")

nx_create_nro(nfsu_switch_vehicle_track
    NACP nfsu_switch_vehicle_track.nacp)

if(NFSU_BUILD_TESTS)
    # The NRO and its ELF, as for the other applications.
    add_test(NAME nfsu_switch_vehicle_track_nro
        COMMAND ${CMAKE_COMMAND}
                -DNRO=${CMAKE_CURRENT_BINARY_DIR}/nfsu_switch_vehicle_track.nro
                -DELF=$<TARGET_FILE:nfsu_switch_vehicle_track>
                -DTITLE=nfsu_switch_vehicle_track
                -P ${PROJECT_SOURCE_DIR}/tests/switch_nro_check.cmake)

    set_tests_properties(nfsu_switch_vehicle_track_nro
        PROPERTIES TIMEOUT 60)
endif()
