add_library(nfsu_eagl_animation src/eagl_animation.cpp src/world_motion.cpp src/world_obstacle.cpp)
target_include_directories(nfsu_eagl_animation PUBLIC include)
target_compile_features(nfsu_eagl_animation PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_eagl_animation)
add_library(nfsu_race_session src/race_session.cpp src/crash_camera.cpp src/race_crash_road.cpp src/race_particles.cpp src/race_setup.cpp
    src/handling_telemetry.cpp src/intro_ai.cpp src/race_leaderboard.cpp src/race_style.cpp src/race_drift.cpp src/traffic_motion.cpp src/race_traffic.cpp src/race_props.cpp src/race_world_motion.cpp)
target_link_libraries(nfsu_race_session PUBLIC nfsu_drive_session nfsu_vehicle_track nfsu_race_timing
    nfsu_vehicle_tuning nfsu_core nfsu_ai_driver nfsu_race_events nfsu_eagl_animation)
target_compile_features(nfsu_race_session PUBLIC cxx_std_17)
nfsu_enable_warnings(nfsu_race_session)
# Inherits cmake/floating_point.cmake; the whole-tree Switch census includes
# this neutral library even though no console application links it yet.
if(NINTENDO_SWITCH)
    set_target_properties(nfsu_race_session PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)
endif()

if(NFSU_BUILD_TESTS AND NOT NINTENDO_SWITCH)
    add_executable(nfsu_crash_camera_tests tests/crash_camera_tests.cpp)
    target_link_libraries(nfsu_crash_camera_tests PRIVATE nfsu_race_session nfsu_race_hud)
    nfsu_enable_warnings(nfsu_crash_camera_tests)
    add_test(NAME nfsu_crash_camera_tests COMMAND nfsu_crash_camera_tests)
endif()
